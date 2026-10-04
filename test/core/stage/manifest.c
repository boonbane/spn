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
  record_t expect [STAGE_TEST_MAX];
} test_t;

static const test_t tests [] = {
  {
    .name = "empty",
    .manifest = "",
  },
  {
    .name = "one_record",
    .manifest = "a\ta\n",
    .expect = { { "a", "a" } },
  },
  {
    .name = "owner_with_two_entries",
    .manifest = "a\ta\na\tlib.so\n",
    .expect = { { "a", "a" }, { "a", "lib.so" } },
  },
  {
    .name = "no_trailing_newline",
    .manifest = "a\ta\nb\tb",
    .expect = { { "a", "a" }, { "b", "b" } },
  },
  {
    .name = "leading_lines_without_a_tab_are_skipped",
    .manifest = "junk\n\na\ta\n",
    .expect = { { "a", "a" } },
  },
  {
    .name = "trailing_lines_without_a_tab_are_skipped",
    .manifest = "a\ta\njunk\n",
    .expect = { { "a", "a" } },
  },
};

sp_test_each(stage, records, test_t, tests) {
  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].owner);

  u32 count = 0;
  spn_stage_for(sp_cstr_as_str(it->manifest), rt) {
    sp_must(t, count < expected);
    sp_expect_str_eq_c(t, rt.record.owner, it->expect[count].owner);
    sp_expect_str_eq_c(t, rt.record.path, it->expect[count].path);
    count++;
  }
  sp_expect_eq(t, expected, count);
  return SP_OK;
}

sp_test(stage, write_roundtrip) {
  sp_mem_t mem = sp_test_arena(t);

  sp_io_dyn_mem_writer_t sink = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &sink);
  spn_stage_write(&sink.base, sp_str_lit("a"), sp_str_lit("a"));
  spn_stage_write(&sink.base, sp_str_lit("a"), sp_str_lit("lib.so"));
  sp_str_t manifest = sp_io_dyn_mem_writer_as_str(&sink);
  sp_expect_str_eq_c(t, manifest, "a\ta\na\tlib.so\n");

  spn_stage_it_t rt = spn_stage_it_begin(manifest);
  sp_must(t, spn_stage_it_valid(&rt));
  sp_expect_str_eq_c(t, rt.record.owner, "a");
  sp_expect_str_eq_c(t, rt.record.path, "a");
  spn_stage_it_next(&rt);
  sp_must(t, spn_stage_it_valid(&rt));
  sp_expect_str_eq_c(t, rt.record.path, "lib.so");
  spn_stage_it_next(&rt);
  sp_expect(t, !spn_stage_it_valid(&rt));
  return SP_OK;
}
