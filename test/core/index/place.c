#include "index.h"

#include "event/event.h"
#include "git/cache.h"
#include "index/marker.h"
#include "index/tip.h"
#include "lock/lock.h"
#include "op/op.h"
#include "paths/paths.h"

typedef struct {
  spn_err_t err;
  const c8* placed;
  const c8* stamp;
  bool held;
  bool git;
} expect_t;

typedef struct {
  const c8* name;
  spn_index_format_t format;
  spn_index_kind_t kind;
  const c8* declared;
  const c8* cached;
  const c8* tip;
  bool cancelled;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "aged_tip_moves_to_the_remote_head",
    .cached = "A",
    .tip = "B",
    .expect = { .placed = "B", .stamp = "B" },
  },
  {
    .name = "unfetchable_tip_keeps_the_cached_rev",
    .cached = "A",
    .tip = "C",
    .expect = { .placed = "A", .stamp = "A", .held = true },
  },
  {
    .name = "unfetchable_tip_with_nothing_cached",
    .tip = "C",
    .expect = { .err = SPN_ERR_INDEX_SYNC },
  },
  {
    .name = "cancelled_refresh_sets_no_hold",
    .cached = "A",
    .tip = "B",
    .cancelled = true,
    .expect = { .err = SPN_ERR_CANCELLED, .stamp = "A" },
  },
  {
    .name = "declared_rev_reads_no_tip",
    .declared = "A",
    .expect = { .placed = "A" },
  },
  {
    .name = "releases_place_carries_no_rev",
    .tip = "A",
    .expect = { .placed = "A", .stamp = "A" },
  },
  {
    .name = "manifests_place_carries_its_rev",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .tip = "A",
    .expect = { .placed = "A", .stamp = "A", .git = true },
  },
  {
    .name = "user_manifests_index_needs_a_rev",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .kind = SPN_INDEX_KIND_USER,
    .tip = "A",
    .expect = { .err = SPN_ERR_INDEX_UNPINNED, .stamp = "A" },
  },
  {
    .name = "user_manifests_index_with_a_rev",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .kind = SPN_INDEX_KIND_USER,
    .declared = "A",
    .expect = { .placed = "A", .git = true },
  },
};

static sp_str_t rev_named(const sp_str_t* revs, const c8* name) {
  if (!name) {
    return sp_str_lit("");
  }
  if (sp_cstr_equal(name, "A")) {
    return revs[0];
  }
  if (sp_cstr_equal(name, "B")) {
    return revs[1];
  }
  return sp_cstr_as_str(name);
}

static spn_err_t answer(void* user, spn_git_route_t route, sp_mem_t mem, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  const sp_str_t* tip = (const sp_str_t*)user;
  *failure = sp_zero_s(spn_git_failure_t);
  if (sp_str_empty(*tip)) {
    return SPN_ERROR;
  }
  *rev = *tip;
  return SPN_OK;
}

sp_test_each(index_place, git, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_str_t remote = sp_fs_join_path(mem, dir, sp_str_lit("remote"));
  sp_str_t stamps = sp_fs_join_path(mem, dir, sp_str_lit("index"));
  spn_path_t store = { .sub = sp_fs_join_path(mem, dir, sp_str_lit("store")) };

  spn_ctx_t* ctx = sp_alloc_type(mem, spn_ctx_t);
  ctx->events = spn_event_buffer_new(mem);
  ctx->intern = spn.intern;
  spn_git_cache_init(&ctx->caches.git, mem, spn.intern, &ctx->roots, spn_path_join(mem, store, sp_str_lit("db")), spn_path_join(mem, store, sp_str_lit("src")), spn_path_join(mem, store, sp_str_lit("locks")));
  spn_op_t op = { .ctx = ctx, .mem = mem };

  sp_fs_create_dir(remote);
  git_repo_init(remote);
  si_index_marker_t marker = { .version = si_index_marker_version, .format = it->format };
  spn_err_union_t unwritten = sp_zero;
  sp_must_eq(t, SPN_OK, si_index_marker_save(mem, &ctx->roots, (spn_path_t) { .sub = remote }, marker, &unwritten));

  const c8* seeds [] = { "A", "B" };
  sp_str_t revs [2] = sp_zero;
  sp_carr_for(seeds, at) {
    sp_fs_write(sp_fs_join_path(mem, remote, sp_str_lit("S")), sp_cstr_as_str(seeds[at]), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
    git_repo_stage_all(remote);
    git_repo_commit(remote, sp_cstr_as_str(seeds[at]));
    revs[at] = git_repo_head(remote);
  }

  sp_str_t tip = sp_zero;
  sp_intern_str_t name = spn_intern_lit("I");
  spn_index_info_t index = {
    .id = name.id,
    .name = name,
    .kind = it->kind,
    .git = { .url = remote, .rev = rev_named(revs, it->declared) },
    .location = { .at = sp_path_from_str(stamps), .dir = stamps },
    .refresh = 600,
    .remote = { .tip = answer, .user = &tip },
  };
  spn_lock_t held = spn_lock_new(mem);
  si_index_place_t place = sp_zero;

  if (it->cached) {
    tip = rev_named(revs, it->cached);
    sp_must_eq(t, SPN_OK, si_place_index(&op, mem, &held, &index, &place));
    si_index_tip_expire(&index);
  }

  tip = rev_named(revs, it->tip);
  sp_atomic_s32_store(&op.cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);
  place = sp_zero_s(si_index_place_t);
  sp_expect_eq(t, it->expect.err, si_place_index(&op, mem, &held, &index, &place));

  si_index_tip_t after = si_index_tip_plan(mem, &index);
  sp_expect_str_eq(t, after.rev, rev_named(revs, it->expect.stamp));
  sp_expect_eq(t, it->expect.held, after.held);
  if (it->expect.err) {
    return SP_OK;
  }

  sp_expect_str_eq_c(t, test_read_file(mem, sp_fs_join_path(mem, place.dir, sp_str_lit("S"))), it->expect.placed);
  sp_expect_str_eq(t, place.git.url, it->expect.git ? remote : sp_str_lit(""));
  sp_expect_str_eq(t, place.git.rev, it->expect.git ? rev_named(revs, it->expect.placed) : sp_str_lit(""));
  return SP_OK;
}
