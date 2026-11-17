#include "harness.h"

typedef struct {
  const c8* holds;
  path_t is;
} expect_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [SPN_TEST_MAX_COPY];
  const c8* args [SPN_TEST_COMMAND_MAX_ARGS];
  expect_t expect;
} query_t;

static const query_t queries [] = {
  {
    .name = "root_source",
    .project = "test/integration/fixtures/path/source",
    .args = { "path", "R" },
    .expect.holds = "R.txt",
  },
  {
    .name = "package_dep_source",
    .project = "test/integration/fixtures/path/source",
    .args = { "path", "A" },
    .expect.holds = "A.txt",
  },
  {
    .name = "tool_dep_source",
    .project = "test/integration/fixtures/path/source",
    .args = { "path", "--tool-dep", "B" },
    .expect.holds = "B.txt",
  },
  {
    .name = "package_dep_named_like_a_tool_dep",
    .project = "test/integration/fixtures/path/same_name",
    .args = { "path", "A" },
    .expect.holds = "P.txt",
  },
  {
    .name = "tool_dep_named_like_a_package_dep",
    .project = "test/integration/fixtures/path/same_name",
    .args = { "path", "--tool-dep", "A" },
    .expect.holds = "T.txt",
  },
  {
    .name = "file_package_dep_named_like_a_tool_dep",
    .project = "test/integration/fixtures/path/same_name_file",
    .copy = { "P/*", "T/*" },
    .args = { "path", "A" },
    .expect.holds = "P.txt",
  },
  {
    .name = "file_tool_dep_named_like_a_package_dep",
    .project = "test/integration/fixtures/path/same_name_file",
    .copy = { "P/*", "T/*" },
    .args = { "path", "--tool-dep", "A" },
    .expect.holds = "T.txt",
  },
  {
    .name = "tool_exe",
    .project = "test/integration/fixtures/path/tool",
    .args = { "path", "--tool", "T" },
    .expect.is = exe("T"),
  },
};

sp_test_each(path, located, query_t, queries) {
  fixture_t fixture = sp_zero;
  sp_try(fixture_init(t, &fixture));
  sp_try(test_when(t, sp_zero_s(test_when_t)));
  sp_try(prepare_test(t, &fixture, it->project, it->copy));

  sp_ps_output_t output = run_spn(t, &fixture, it->args, SP_NULLPTR);
  sp_must_eq(t, 0, output.status.exit_code);

  sp_str_t located = sp_str_trim_right(output.out);
  if (it->expect.holds) {
    expect_exists(t, &fixture, sp_fs_join_path(fixture.mem, located, sp_cstr_as_str(it->expect.holds)), true, __FILE__, __LINE__);
  }
  if (it->expect.is.kind != PATH_NONE) {
    sp_expect_str_eq(t, located, fixture_path(&fixture, path_render(&fixture, it->expect.is)));
    expect_exists(t, &fixture, located, true, __FILE__, __LINE__);
  }
  return SP_OK;
}

static const command_test_t commands [] = {
  {
    .name = "source_builds_nothing",
    .project = "test/integration/fixtures/path/source",
    .args = { "path", "A" },
    .expect.events = { { .event = SPN_EVENT_INIT_BUILD_GRAPH, .absent = true } },
  },
  {
    .name = "unknown_package",
    .project = "test/integration/fixtures/path/source",
    .args = { "path", "C" },
    .expect = { .rc = 1, .err = SPN_ERR_PKG_ABSENT },
  },
  {
    .name = "unknown_tool",
    .project = "test/integration/fixtures/path/tool",
    .args = { "path", "--tool", "U" },
    .expect = { .rc = 1, .err = SPN_ERR_TARGET_SELECTION },
  },
  {
    .name = "tool_without_sources",
    .project = "test/integration/fixtures/path/sourceless",
    .args = { "path", "--tool", "T" },
    .expect = { .rc = 1, .err = SPN_ERR_TOOL_MISSING },
  },
};

sp_test_each(path, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}
