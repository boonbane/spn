#include "index.h"

#include "index/tip.h"

typedef struct {
  const c8* rev;
  bool fetch;
  bool held;
} expect_t;

typedef struct {
  const c8* name;
  const c8* cached;
  bool aged;
  bool hold;
  const c8* save;
  bool expire;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "never_read",
    .expect = { .fetch = true },
  },
  {
    .name = "fresh_is_served",
    .cached = "A",
    .expect = { .rev = "A" },
  },
  {
    .name = "aged_is_read",
    .cached = "A",
    .aged = true,
    .expect = { .rev = "A", .fetch = true },
  },
  {
    .name = "failure_holds_an_aged_tip",
    .cached = "A",
    .aged = true,
    .hold = true,
    .expect = { .rev = "A", .held = true },
  },
  {
    .name = "save_replaces_an_aged_tip",
    .cached = "A",
    .aged = true,
    .save = "B",
    .expect = { .rev = "B" },
  },
  {
    .name = "expired_is_read",
    .cached = "A",
    .expire = true,
    .expect = { .rev = "A", .fetch = true },
  },
  {
    .name = "expire_ends_a_hold",
    .cached = "A",
    .aged = true,
    .hold = true,
    .expire = true,
    .expect = { .rev = "A", .fetch = true },
  },
};

sp_test_each(index_tip, plan, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);

  spn_index_info_t index = {
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .git.url = sp_str_lit("U"),
    .location.at = sp_path_from_str(dir),
    .refresh = 600,
  };

  if (it->cached) {
    si_index_tip_save(&index, sp_cstr_as_str(it->cached));
  }
  if (it->aged) {
    sp_path_t stamp = sp_path_from_str(sp_fs_join_path(mem, dir, sp_str_lit("spn-tip")));
    sp_sys_timespec_t old = { .tv_sec = 1 };
    sp_must_eq(t, SP_OK, sp_sys_set_times_s(stamp.dir, stamp.sub, old, old));
  }
  if (it->hold) {
    si_index_tip_hold(&index);
  }
  if (it->save) {
    si_index_tip_save(&index, sp_cstr_as_str(it->save));
  }
  if (it->expire) {
    si_index_tip_expire(&index);
  }

  si_index_tip_t tip = si_index_tip_plan(mem, &index);
  sp_expect_str_eq_c(t, tip.rev, it->expect.rev ? it->expect.rev : "");
  sp_expect_eq(t, it->expect.fetch, tip.fetch);
  sp_expect_eq(t, it->expect.held, tip.held);
  return SP_OK;
}
