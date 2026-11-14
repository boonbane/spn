#include "spn_test.h"

#include "event/event.h"
#include "intern/intern.h"
#include "model/sync.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

#define max_sources 2

typedef struct {
  spn_err_t err;
  const c8* url;
  const c8* actual;
} expect_t;

typedef struct {
  const c8* name;
  const c8* serves [max_sources + 1];
  const c8* corrupt [max_sources + 1];
  const c8* live [max_sources + 1];
  const c8* snapshot [max_sources + 1];
  const c8* mirror;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "origin_serves_without_mirrors",
    .serves = { "O" },
    .expect = { .url = "O/T.tar.xz" },
  },
  {
    .name = "listed_mirror_is_tried_before_origin",
    .serves = { "A", "O" },
    .live = { "A" },
    .expect = { .url = "A/T.tar.xz?source=spn" },
  },
  {
    .name = "corrupt_mirror_falls_through_to_origin",
    .serves = { "O" },
    .corrupt = { "A" },
    .live = { "A" },
    .expect = { .url = "O/T.tar.xz" },
  },
  {
    .name = "missing_mirror_falls_through_to_origin",
    .serves = { "O" },
    .live = { "A" },
    .expect = { .url = "O/T.tar.xz" },
  },
  {
    .name = "unreachable_list_uses_the_snapshot",
    .serves = { "A" },
    .snapshot = { "A" },
    .expect = { .url = "A/T.tar.xz?source=spn" },
  },
  {
    .name = "reachable_list_ignores_the_snapshot",
    .serves = { "B" },
    .live = { "A" },
    .snapshot = { "B" },
    .expect = { .err = SPN_ERR_TOOLCHAIN_FETCH, .url = "O/T.tar.xz" },
  },
  {
    .name = "explicit_mirror_is_strict",
    .serves = { "B", "O" },
    .live = { "B" },
    .mirror = "A",
    .expect = { .err = SPN_ERR_TOOLCHAIN_FETCH, .url = "A/T.tar.xz" },
  },
  {
    .name = "corrupt_origin_reports_both_hashes",
    .corrupt = { "O" },
    .expect = {
      .err = SPN_ERR_TOOLCHAIN_SHA,
      .url = "O/T.tar.xz",
      .actual = "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
    },
  },
};

static void place(sp_mem_t mem, sp_str_t dir, const c8* const* sources, sp_str_t content) {
  while (*sources) {
    sp_str_t path = sp_fmt(mem, "{}/{}/T.tar.xz", sp_fmt_str(dir), sp_fmt_cstr(*sources)).value;
    sp_fs_create_dir(sp_fs_parent_path(path));
    sp_fs_write(path, content, SP_FS_WRITE_TRUNCATE);
    sources++;
  }
}

static sp_str_t list(sp_mem_t mem, sp_str_t dir, const c8* const* mirrors) {
  sp_io_dyn_mem_writer_t listing = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &listing);
  while (*mirrors) {
    sp_fmt_io(&listing.base, "file://{}/{}\n", sp_fmt_str(dir), sp_fmt_cstr(*mirrors));
    mirrors++;
  }
  return sp_io_dyn_mem_writer_as_str(&listing);
}

sp_test_each(fetch, sources, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);

  sp_str_t tarball = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_str_lit("test/core/toolchain/archives/file.tar.xz")), &tarball));

  place(mem, dir, it->serves, tarball);
  place(mem, dir, it->corrupt, sp_str_lit("A"));

  spn_artifact_t artifact = {
    .url = sp_fmt(mem, "file://{}/O/T.tar.xz", sp_fmt_str(dir)).value,
    .sha256 = test_sha256_hex(mem, tarball),
    .mirror_snapshot = list(mem, dir, it->snapshot),
  };
  if (it->live[0] || it->snapshot[0]) {
    artifact.mirror_list = sp_fmt(mem, "file://{}/L", sp_fmt_str(dir)).value;
  }
  if (it->live[0]) {
    sp_fs_write(sp_fs_join_path(mem, dir, sp_str_lit("L")), list(mem, dir, it->live), SP_FS_WRITE_TRUNCATE);
  }

  sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"))));
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &roots,
  };
  if (it->mirror) {
    store.mirror = sp_fmt(mem, "file://{}/{}", sp_fmt_str(dir), sp_fmt_cstr(it->mirror)).value;
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
  sp_expect_eq(t, (u32)failure.kind, (u32)it->expect.err);
  sp_expect_str_eq(t, url, sp_fmt(mem, "file://{}/{}", sp_fmt_str(dir), sp_fmt_cstr(it->expect.url)).value);
  if (it->expect.actual) {
    sp_expect_str_eq(t, failure.artifact.expected, artifact.sha256);
    sp_expect_str_eq_c(t, failure.artifact.actual, it->expect.actual);
  }

  sp_path_t root = spn_path_at(&roots, spn_toolchain_artifact_root(mem, artifact));
  sp_sys_file_meta_t meta = sp_zero;
  sp_expect_eq(t, !sp_fs_get_link_metadata_at(sp_path_join(mem, root, sp_str_lit("B")), &meta) && meta.kind == SP_FS_KIND_FILE, !it->expect.err);

  u32 entries = 0;
  sp_sys_dir_t stored = sp_zero;
  sp_must_ok(t, sp_fs_open_dir_at(spn_path_at(&roots, spn_path_from_id(SPN_DIR_ID_TOOLCHAIN_STORE)), &stored));
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF] = sp_zero;
  sp_fs_dir_it_t walk = sp_fs_dir_it(stored);
  sp_fs_dir_entry_t entry = sp_zero;
  while (sp_fs_dir_it_next(&walk, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    entries++;
  }
  sp_fs_dir_it_close(&walk);
  sp_fs_close_dir(stored);
  sp_expect_eq(t, entries, it->expect.err ? 1u : 2u);

  spn_path_roots_close(&roots);
  return SP_OK;
}
