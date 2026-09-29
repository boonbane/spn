#include "unit.h"

#include "stage/stage.h"

#define STAGE_TEST_MAX 4

typedef struct {
  const c8* owner;
  const c8* path;
} record_t;

typedef struct {
  const c8* name;
  const c8* manifest;
  record_t live [STAGE_TEST_MAX];
  struct {
    record_t records [STAGE_TEST_MAX];
    const c8* dropped [STAGE_TEST_MAX];
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "no_manifest",
    .manifest = "",
    .live = { { "a", "a" } },
    .expect.records = { { "a", "a" } },
  },
  {
    .name = "other_owner_carries_over",
    .manifest = "b\tb\n",
    .live = { { "a", "a" } },
    .expect.records = { { "a", "a" }, { "b", "b" } },
  },
  {
    .name = "live_owner_replaces_its_entries",
    .manifest = "a\ta\na\tlib.so\n",
    .live = { { "a", "a" } },
    .expect = { .records = { { "a", "a" } }, .dropped = { "lib.so" } },
  },
  {
    .name = "entry_kept_by_another_owner_is_not_dropped",
    .manifest = "a\tlib.so\nb\tlib.so\n",
    .live = { { "a", "a" } },
    .expect.records = { { "a", "a" }, { "b", "lib.so" } },
  },
  {
    .name = "same_path_under_two_owners_is_listed_twice",
    .manifest = "a\tlib.so\n",
    .live = { { "b", "lib.so" } },
    .expect.records = { { "b", "lib.so" }, { "a", "lib.so" } },
  },
  {
    .name = "no_live_keeps_everything",
    .manifest = "a\ta\nb\tb\n",
    .expect.records = { { "a", "a" }, { "b", "b" } },
  },
  {
    .name = "lines_without_a_tab_are_ignored",
    .manifest = "junk\n\na\ta\n",
    .live = { { "b", "b" } },
    .expect.records = { { "b", "b" }, { "a", "a" } },
  },
};

sp_test_each(stage, plan, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_da(spn_stage_record_t) live = sp_da_new(mem, spn_stage_record_t);
  u32 num_live = 0;
  sp_carr_detect_len(it->live, num_live, it->live[num_live].owner);
  sp_for(lt, num_live) {
    sp_da_push(live, ((spn_stage_record_t) {
      .owner = sp_cstr_as_str(it->live[lt].owner),
      .path = sp_cstr_as_str(it->live[lt].path),
    }));
  }

  spn_stage_plan_t plan = spn_stage_plan(mem, sp_cstr_as_str(it->manifest), live);

  u32 num_records = 0;
  sp_carr_detect_len(it->expect.records, num_records, it->expect.records[num_records].owner);
  sp_must_eq(t, num_records, (u32)sp_da_size(plan.records));
  sp_for(rt, num_records) {
    sp_expect_str_eq_c(t, plan.records[rt].owner, it->expect.records[rt].owner);
    sp_expect_str_eq_c(t, plan.records[rt].path, it->expect.records[rt].path);
  }

  u32 num_dropped = 0;
  sp_carr_detect_len(it->expect.dropped, num_dropped, it->expect.dropped[num_dropped]);
  sp_must_eq(t, num_dropped, (u32)sp_da_size(plan.dropped));
  sp_for(dt, num_dropped) {
    sp_expect_str_eq_c(t, plan.dropped[dt], it->expect.dropped[dt]);
  }
  return SP_OK;
}

sp_test(stage, render_roundtrip) {
  sp_mem_t mem = sp_test_arena(t);

  sp_da(spn_stage_record_t) records = sp_da_new(mem, spn_stage_record_t);
  sp_da_push(records, ((spn_stage_record_t) { .owner = sp_str_lit("a"), .path = sp_str_lit("a") }));
  sp_da_push(records, ((spn_stage_record_t) { .owner = sp_str_lit("a"), .path = sp_str_lit("lib.so") }));

  sp_io_dyn_mem_writer_t sink = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &sink);
  spn_stage_render(&sink.base, records);
  sp_str_t manifest = sp_io_dyn_mem_writer_as_str(&sink);
  sp_expect_str_eq_c(t, manifest, "a\ta\na\tlib.so\n");

  spn_stage_plan_t plan = spn_stage_plan(mem, manifest, sp_da_new(mem, spn_stage_record_t));
  sp_must_eq(t, (u32)2, (u32)sp_da_size(plan.records));
  sp_expect_str_eq_c(t, plan.records[1].path, "lib.so");
  sp_expect_eq(t, (u32)0, (u32)sp_da_size(plan.dropped));
  return SP_OK;
}
