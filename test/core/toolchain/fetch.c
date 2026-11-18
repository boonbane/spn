#include "spn_test.h"

#include "event/event.h"
#include "intern/intern.h"
#include "model/sync.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

#define max_sources 2
#define max_replies 4
#define max_requests 8

typedef enum {
  REPLY_MISSING,
  REPLY_TARBALL,
  REPLY_CORRUPT,
  REPLY_DROPPED,
  REPLY_UNTRUSTED,
  REPLY_CANCELLED,
  REPLY_UNWRITABLE,
} reply_t;

typedef struct {
  const c8* name;
  reply_t replies [max_replies];
} source_t;

typedef struct {
  spn_err_t err;
  bool silent;
  const c8* requests;
  u32 waits [spn_net_max_retries];
  const c8* url;
  const c8* actual;
} expect_t;

typedef struct {
  const c8* name;
  source_t sources [max_sources];
  const c8* live [max_sources + 1];
  const c8* snapshot [max_sources + 1];
  const c8* mirror;
  expect_t expect;
} test_t;

typedef struct {
  const test_t* test;
  sp_str_t tarball;
  sp_str_t listing;
  u32 served [max_sources];
  c8 requests [max_requests + 1];
  u32 requested;
  u32 waits [spn_net_max_retries];
  u32 waited;
} script_t;

sp_test_suite(fetch, .serial = true);

static const test_t tests [] = {
  {
    .name = "origin_serves_without_mirrors",
    .sources = { { "O", { REPLY_TARBALL } } },
    .expect = { .requests = "O", .url = "O/T.tar.xz" },
  },
  {
    .name = "listed_mirror_is_tried_before_origin",
    .sources = { { "A", { REPLY_TARBALL } }, { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .requests = "LA", .url = "A/T.tar.xz?source=spn" },
  },
  {
    .name = "corrupt_mirror_falls_through_to_origin",
    .sources = { { "A", { REPLY_CORRUPT } }, { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .requests = "LAO", .url = "O/T.tar.xz" },
  },
  {
    .name = "missing_mirror_falls_through_to_origin",
    .sources = { { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .requests = "LAO", .url = "O/T.tar.xz" },
  },
  {
    .name = "dropped_mirror_is_not_retried",
    .sources = { { "A", { REPLY_DROPPED } }, { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .requests = "LAO", .url = "O/T.tar.xz" },
  },
  {
    .name = "unreachable_list_uses_the_snapshot",
    .sources = { { "A", { REPLY_TARBALL } } },
    .snapshot = { "A" },
    .expect = { .requests = "LA", .url = "A/T.tar.xz?source=spn" },
  },
  {
    .name = "reachable_list_ignores_the_snapshot",
    .sources = { { "B", { REPLY_TARBALL } } },
    .live = { "A" },
    .snapshot = { "B" },
    .expect = { .err = SPN_ERR_TOOLCHAIN_FETCH, .requests = "LAO", .url = "O/T.tar.xz" },
  },
  {
    .name = "explicit_mirror_is_strict",
    .sources = { { "B", { REPLY_TARBALL } }, { "O", { REPLY_TARBALL } } },
    .live = { "B" },
    .mirror = "A",
    .expect = { .err = SPN_ERR_TOOLCHAIN_FETCH, .requests = "A", .url = "A/T.tar.xz" },
  },
  {
    .name = "corrupt_origin_reports_both_hashes",
    .sources = { { "O", { REPLY_CORRUPT } } },
    .expect = {
      .err = SPN_ERR_TOOLCHAIN_SHA,
      .requests = "O",
      .url = "O/T.tar.xz",
      .actual = "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
    },
  },
  {
    .name = "dropped_origin_is_retried",
    .sources = { { "O", { REPLY_DROPPED, REPLY_DROPPED, REPLY_TARBALL } } },
    .expect = { .requests = "OOO", .waits = { 1, 2 }, .url = "O/T.tar.xz" },
  },
  {
    .name = "dropped_origin_fails_after_the_schedule",
    .sources = { { "O", { REPLY_DROPPED, REPLY_DROPPED, REPLY_DROPPED, REPLY_DROPPED } } },
    .expect = { .err = SPN_ERR_TOOLCHAIN_FETCH, .requests = "OOOO", .waits = { 1, 2, 3 }, .url = "O/T.tar.xz" },
  },
  {
    .name = "untrusted_origin_reports_the_certificate",
    .sources = { { "O", { REPLY_UNTRUSTED } } },
    .expect = { .err = SPN_ERR_CERT_UNTRUSTED, .requests = "O" },
  },
  {
    .name = "cancelled_mirror_ends_the_walk",
    .sources = { { "A", { REPLY_CANCELLED } }, { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .err = SPN_ERR_CANCELLED, .silent = true, .requests = "LA" },
  },
  {
    .name = "unwritable_mirror_ends_the_walk",
    .sources = { { "A", { REPLY_UNWRITABLE } }, { "O", { REPLY_TARBALL } } },
    .live = { "A" },
    .expect = { .err = SPN_ERR_FS_WRITE, .requests = "LA" },
  },
};

static spn_err_t download(void* user, const spn_http_config_t* config, sp_str_t url, sp_path_t dest, spn_http_failure_t* failure) {
  script_t* script = (script_t*)user;
  *failure = (spn_http_failure_t) { .kind = SPN_HTTP_FAILED, .message = "A" };
  sp_assert(script->requested < max_requests);
  script->requests[script->requested++] = url.data[0];

  if (url.data[0] == 'L') {
    return sp_str_empty(script->listing) || sp_fs_write_at(dest, script->listing, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms) ? SPN_ERROR : SPN_OK;
  }

  reply_t reply = REPLY_MISSING;
  sp_carr_for(script->test->sources, it) {
    const source_t* source = &script->test->sources[it];
    if (source->name && source->name[0] == url.data[0] && script->served[it] < max_replies) {
      reply = source->replies[script->served[it]++];
    }
  }

  switch (reply) {
    case REPLY_MISSING: {
      return SPN_ERROR;
    }
    case REPLY_TARBALL: {
      return sp_fs_write_at(dest, script->tarball, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms) ? SPN_ERROR : SPN_OK;
    }
    case REPLY_CORRUPT: {
      return sp_fs_write_at(dest, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms) ? SPN_ERROR : SPN_OK;
    }
    case REPLY_DROPPED: {
      failure->kind = SPN_HTTP_DROPPED;
      return SPN_ERROR;
    }
    case REPLY_UNTRUSTED: {
      failure->kind = SPN_HTTP_UNTRUSTED;
      return SPN_ERROR;
    }
    case REPLY_CANCELLED: {
      failure->kind = SPN_HTTP_CANCELLED;
      return SPN_ERROR;
    }
    case REPLY_UNWRITABLE: {
      failure->kind = SPN_HTTP_UNWRITABLE;
      return SPN_ERROR;
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static void record_wait(void* user, u32 ms, sp_atomic_s32_t* cancelled) {
  script_t* script = (script_t*)user;
  sp_assert(script->waited < spn_net_max_retries);
  script->waits[script->waited++] = ms;
}

static sp_str_t list(sp_mem_t mem, const c8* const* mirrors) {
  sp_io_dyn_mem_writer_t listing = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &listing);
  while (*mirrors) {
    sp_fmt_io(&listing.base, "{}\n", sp_fmt_cstr(*mirrors));
    mirrors++;
  }
  return sp_io_dyn_mem_writer_as_str(&listing);
}

sp_test_each(fetch, sources, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  script_t script = { .test = it, .listing = list(mem, it->live) };
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_str_lit("test/core/toolchain/archives/file.tar.xz")), &script.tarball));

  spn_artifact_t artifact = {
    .url = sp_str_lit("O/T.tar.xz"),
    .sha256 = test_sha256_hex(mem, script.tarball),
    .mirror_snapshot = list(mem, it->snapshot),
  };
  if (it->live[0] || it->snapshot[0]) {
    artifact.mirror_list = sp_str_lit("L");
  }

  sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"))));
  spn_path_roots_set(&spn.roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &spn.roots,
    .http = { .download = download, .user = &script },
    .backoff = { .wait_ms = { 1, 2, 3 }, .wait = record_wait, .user = &script },
  };
  if (it->mirror) {
    store.mirror = sp_cstr_as_str(it->mirror);
  }

  spn_toolchain_info_t info = {
    .name = sp_intern(spn.intern, sp_str_lit("T")),
    .driver = SPN_CC_DRIVER_GCC,
    .support = {
      .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT,
      .artifact = artifact,
    },
  };
  spn_toolchain_unit_t unit = { .info = &info };
  sp_atomic_s32_t cancelled = sp_zero;
  sp_expect_eq(t, (u32)spn_sync_toolchain(&store, &unit, &cancelled), (u32)it->expect.err);

  sp_str_t url = sp_zero;
  spn_err_union_t failure = sp_zero;
  sp_da(spn_event_t) events = spn_event_buffer_drain(mem, spn.events);
  sp_da_for(events, at) {
    switch (events[at].kind) {
      case SPN_EVENT_SYNC_PACKAGE: {
        url = events[at].sync_pkg.url;
        break;
      }
      case SPN_EVENT_ERR: {
        failure = events[at].err;
        url = failure.artifact.url;
        break;
      }
      default: {
        break;
      }
    }
  }
  sp_expect_eq(t, (u32)failure.kind, (u32)(it->expect.silent ? SPN_OK : it->expect.err));
  sp_expect_str_eq_c(t, sp_cstr_as_str(script.requests), it->expect.requests);
  sp_for(at, spn_net_max_retries) {
    sp_expect_eq(t, script.waits[at], it->expect.waits[at]);
  }
  if (it->expect.url) {
    sp_expect_str_eq_c(t, url, it->expect.url);
  }
  if (it->expect.actual) {
    sp_expect_str_eq(t, failure.artifact.expected, artifact.sha256);
    sp_expect_str_eq_c(t, failure.artifact.actual, it->expect.actual);
  }

  sp_path_t root = spn_path_at(&spn.roots, spn_toolchain_artifact_root(mem, artifact));
  sp_sys_file_meta_t meta = sp_zero;
  sp_expect_eq(t, !sp_fs_get_link_metadata_at(sp_path_join(mem, root, sp_str_lit("B")), &meta) && meta.kind == SP_FS_KIND_FILE, !it->expect.err);

  u32 entries = 0;
  sp_sys_dir_t stored = sp_zero;
  sp_must_ok(t, sp_fs_open_dir_at(spn_path_at(&spn.roots, spn_path_from_id(SPN_DIR_ID_TOOLCHAIN_STORE)), &stored));
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF] = sp_zero;
  sp_fs_dir_it_t walk = sp_fs_dir_it(stored);
  sp_fs_dir_entry_t entry = sp_zero;
  while (sp_fs_dir_it_next(&walk, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    entries++;
  }
  sp_fs_dir_it_close(&walk);
  sp_fs_close_dir(stored);
  sp_expect_eq(t, entries, it->expect.err ? 0u : 1u);

  spn_path_roots_close(&spn.roots);
  return SP_OK;
}
