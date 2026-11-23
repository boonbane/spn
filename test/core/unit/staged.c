#include "unit.h"

#define STAGED_TEST_MAX_LIBS 4

#define c_and_cxx { .c = { "T/C" }, .cxx = { "T/C", "T/X" } }

typedef struct {
  const c8* exe;
  const c8* libs [STAGED_TEST_MAX_LIBS];
  const c8* files [UNIT_TEST_MAX_FILES];
} closure_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  const c8* present [UNIT_TEST_MAX_FILES];
  closure_t expect [UNIT_TEST_MAX_ROOTS];
} test_t;

static const test_t tests [] = {
  {
    .name = "bin_stages_shared_deps_beside_it",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libB.so" } } },
  },
  {
    .name = "static_dep_not_staged",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A" } },
  },
  {
    .name = "test_stages_under_test_dir",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "T", .kind = SPN_TARGET_KIND_TEST, .source = { "t.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "T", SPN_TARGET_KIND_TEST } },
    .expect = { { "build/debug/test/T", { "build/debug/test/libB.so" } } },
  },
  {
    .name = "example_stages_under_example_dir",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "E", .kind = SPN_TARGET_KIND_EXAMPLE, .source = { "e.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "E", SPN_TARGET_KIND_EXAMPLE } },
    .expect = { { "build/debug/example/E", { "build/debug/example/libB.so" } } },
  },
  {
    .name = "tool_stages_at_the_root",
    .graph.pkgs = {
      { .name = "P", .targets = { { "S", .kind = SPN_TARGET_KIND_TOOL, .source = { "s.c" } } } },
    },
    .roots = { { "S", SPN_TARGET_KIND_TOOL } },
    .expect = { { "build/debug/S" } },
  },
  {
    .name = "shared_sibling_staged",
    .graph.pkgs = {
      { .name = "P", .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .deps = { "L" } },
        { "L", shared_only, .source = { "l.c" } },
      } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libL.so" } } },
  },
  // Staging chases through shared boundaries the link closure stops at: a
  // shared lib's own shared deps must sit next to the executable too
  {
    .name = "chase_through_shared",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .deps = { { "C" } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
      { .name = "C", .deps = { { "D" } }, .targets = { { "C", static_only, .source = { "c.c" } } } },
      { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libD.so", "build/debug/libB.so" } } },
  },
  {
    .name = "no_link_shared_not_staged",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .no_link = true, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A" } },
  },
  {
    .name = "stub_lib_not_staged",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .shared = { { .stub = "B.tbd" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A" } },
  },
  {
    .name = "shared_file_staged_under_its_own_name",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .shared = { { .path = "C/D.so" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/D.so" } } },
  },
  {
    .name = "lib_root_not_staged",
    .graph.pkgs = {
      { .name = "P", .targets = { { "L", static_only, .source = { "l.c" } } } },
    },
    .roots = { { "L", SPN_TARGET_KIND_LIB } },
  },
  {
    .name = "two_bins_each_list_a_shared_lib",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
        { "C", .kind = SPN_TARGET_KIND_EXE, .source = { "c.c" } },
      } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE }, { "C", SPN_TARGET_KIND_EXE } },
    .expect = {
      { "build/debug/A", { "build/debug/libB.so" } },
      { "build/debug/C", { "build/debug/libB.so" } },
    },
  },
  {
    .name = "c_exe_stages_the_c_files",
    .graph = {
      .staged = c_and_cxx,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .present = { "T/C" },
    .expect = { { "build/debug/A", .files = { "build/debug/C" } } },
  },
  {
    .name = "cxx_exe_stages_the_cxx_files",
    .graph = {
      .staged = c_and_cxx,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .present = { "T/C", "T/X" },
    .expect = { { "build/debug/A", .files = { "build/debug/C", "build/debug/X" } } },
  },
  // C++ reached only through a shared lib the exe loads still needs the C++
  // runtime beside the exe
  {
    .name = "cxx_behind_a_shared_lib_stages_the_cxx_files",
    .graph = {
      .staged = c_and_cxx,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "B", .deps = { { "E" } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
        { .name = "E", .targets = { { "E", static_only, .source = { "e.cpp" } } } },
      },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .present = { "T/C", "T/X" },
    .expect = { { "build/debug/A", { "build/debug/libB.so" }, { "build/debug/C", "build/debug/X" } } },
  },
  {
    .name = "test_stages_toolchain_files_under_test_dir",
    .graph = {
      .staged.c = { "T/D" },
      .pkgs = { { .name = "P", .targets = { { "T", .kind = SPN_TARGET_KIND_TEST, .source = { "t.c" } } } } },
    },
    .roots = { { "T", SPN_TARGET_KIND_TEST } },
    .present = { "T/D" },
    .expect = { { "build/debug/test/T", .files = { "build/debug/test/D" } } },
  },
  {
    .name = "two_bins_each_list_a_toolchain_file",
    .graph = {
      .staged.c = { "T/D" },
      .pkgs = {
        { .name = "P", .targets = {
          { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
          { "C", .kind = SPN_TARGET_KIND_EXE, .source = { "c.c" } },
        } },
      },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE }, { "C", SPN_TARGET_KIND_EXE } },
    .present = { "T/D" },
    .expect = {
      { "build/debug/A", .files = { "build/debug/D" } },
      { "build/debug/C", .files = { "build/debug/D" } },
    },
  },
  {
    .name = "shared_lib_root_stages_no_toolchain_file",
    .graph = {
      .staged.c = { "T/D" },
      .pkgs = { { .name = "P", .targets = { { "L", shared_only, .source = { "l.c" } } } } },
    },
    .roots = { { "L", SPN_TARGET_KIND_LIB } },
  },
};

sp_test_each(unit_staged, closure, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));
  sp_try(units_write_files(t, u, it->present, UNIT_TEST_MAX_FILES));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].exe);
  sp_must_eq(t, expected, (u32)si_da_size(u->plan.closures));
  sp_for(st, expected) {
    const closure_t* expect = &it->expect[st];
    spn_stage_closure_t* staged = &u->plan.closures[st];
    sp_test_kv_c(t, "exe", expect->exe);
    sp_expect_str_eq_c(t, staged->exe.path.sub, expect->exe);

    u32 libs = 0;
    sp_carr_detect_len(expect->libs, libs, expect->libs[libs]);
    sp_must_eq(t, libs, (u32)si_da_size(staged->libs));
    sp_for(lt, libs) {
      sp_expect_str_eq_c(t, staged->libs[lt].path.sub, expect->libs[lt]);
    }

    u32 files = 0;
    sp_carr_detect_len(expect->files, files, expect->files[files]);
    sp_must_eq(t, files, (u32)si_da_size(staged->files));
    sp_for(ft, files) {
      sp_expect_str_eq_c(t, staged->files[ft].path.sub, expect->files[ft]);
    }
  }
  return SP_OK;
}
