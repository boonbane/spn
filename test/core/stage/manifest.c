#include "unit.h"

#include "stage/stage.h"

#define STAGE_TEST_MAX 4

typedef struct {
  spn_stage_declarer_t declarer;
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
    .manifest = "stage\tA\tA\n",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "A", "A" } },
  },
  {
    .name = "declarers",
    .manifest = "stage\tA\tA\nexe\tB\tB\ncompile_commands\tC\tC\n",
    .expect = {
      { SPN_STAGE_DECLARER_STAGE, "A", "A" },
      { SPN_STAGE_DECLARER_EXE, "B", "B" },
      { SPN_STAGE_DECLARER_COMPILE_COMMANDS, "C", "C" },
    },
  },
  {
    .name = "owner_with_two_entries",
    .manifest = "exe\tA\tA\nexe\tA\tB\n",
    .expect = { { SPN_STAGE_DECLARER_EXE, "A", "A" }, { SPN_STAGE_DECLARER_EXE, "A", "B" } },
  },
  {
    .name = "no_trailing_newline",
    .manifest = "stage\tA\tA\nstage\tB\tB",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "A", "A" }, { SPN_STAGE_DECLARER_STAGE, "B", "B" } },
  },
  {
    .name = "leading_lines_without_a_record_are_skipped",
    .manifest = "A\n\nstage\tA\tA\n",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "A", "A" } },
  },
  {
    .name = "trailing_lines_without_a_record_are_skipped",
    .manifest = "stage\tA\tA\nA\n",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "A", "A" } },
  },
  {
    .name = "unknown_declarer_is_skipped",
    .manifest = "A\tA\tA\nstage\tB\tB\n",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "B", "B" } },
  },
  {
    .name = "record_without_a_path_is_skipped",
    .manifest = "stage\tA\nstage\tB\tB\n",
    .expect = { { SPN_STAGE_DECLARER_STAGE, "B", "B" } },
  },
};

sp_test_each(stage, records, test_t, tests) {
  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].owner);

  u32 count = 0;
  spn_stage_for(sp_cstr_as_str(it->manifest), rt) {
    sp_must(t, count < expected);
    sp_expect_eq(t, (u32)it->expect[count].declarer, (u32)rt.record.declarer);
    sp_expect_str_eq_c(t, rt.record.owner, it->expect[count].owner);
    sp_expect_str_eq_c(t, rt.record.path, it->expect[count].path);
    count++;
  }
  sp_expect_eq(t, expected, count);
  return SP_OK;
}

static const record_t written [] = {
  { SPN_STAGE_DECLARER_STAGE, "A", "A" },
  { SPN_STAGE_DECLARER_EXE, "B", "B" },
  { SPN_STAGE_DECLARER_EXE, "B", "C" },
  { SPN_STAGE_DECLARER_COMPILE_COMMANDS, "D", "D" },
};

sp_test(stage, write_roundtrip) {
  sp_io_dyn_mem_writer_t sink = sp_zero;
  sp_io_dyn_mem_writer_init(sp_test_arena(t), &sink);
  sp_carr_for(written, it) {
    spn_stage_write(&sink.base, (spn_stage_record_t) {
      .declarer = written[it].declarer,
      .owner = sp_cstr_as_str(written[it].owner),
      .path = sp_cstr_as_str(written[it].path),
    });
  }
  sp_str_t manifest = sp_io_dyn_mem_writer_as_str(&sink);
  sp_expect_str_eq_c(t, manifest, "stage\tA\tA\nexe\tB\tB\nexe\tB\tC\ncompile_commands\tD\tD\n");

  u32 count = 0;
  spn_stage_for(manifest, rt) {
    sp_must(t, count < sp_carr_len(written));
    sp_expect_eq(t, (u32)written[count].declarer, (u32)rt.record.declarer);
    sp_expect_str_eq_c(t, rt.record.owner, written[count].owner);
    sp_expect_str_eq_c(t, rt.record.path, written[count].path);
    count++;
  }
  sp_expect_eq(t, (u32)sp_carr_len(written), count);
  return SP_OK;
}
