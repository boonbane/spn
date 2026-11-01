#include "harness.h"

static const opt_test_t tests [] = {
  {
    .name = "when",
    .project = "test/integration/fixtures/options/when",
    .copy = { "src/*", "packages/*" },
    .builds = {
      { .present = true },
      { .alternate = true },
    },
  },
  {
    .name = "public_define",
    .project = "test/integration/fixtures/options/public_define",
    .builds = { { .present = true } },
  },
  {
    .name = "gates_dep",
    .project = "test/integration/fixtures/options/gates_dep",
    .builds = {
      { .present = true },
      { .profile = "tracing" },
    },
  },
  {
    .name = "additive",
    .project = "test/integration/fixtures/options/additive",
    .copy = { "main.off.c", "spn.off.toml" },
    .builds = {
      { .present = true },
      { .manifest = "spn.off.toml" },
    },
  },
  {
    .name = "edge_gates_dep",
    .project = "test/integration/fixtures/options/edge_gates_dep",
    .copy = { "vendor/*" },
    .builds = { { .present = true } },
  },
  {
    .name = "index_gated_dep",
    .project = "test/integration/fixtures/options/index_gated_dep",
    .builds = { { .present = true } },
  },
  {
    .name = "index_eager_gate",
    .project = "test/integration/fixtures/options/index_eager_gate",
    .builds = { { .present = true } },
  },
  {
    .name = "edge_gates_build_dep",
    .project = "test/integration/fixtures/options/edge_gates_build_dep",
    .copy = { "vendor/*" },
    .builds = { { .present = true } },
  },
  {
    .name = "dep_rebuild",
    .project = "test/integration/fixtures/options/dep_rebuild",
    .copy = { "main.on.c", "spn.on.toml", "spn.off.toml" },
    .builds = {
      { .present = true },
      { .manifest = "spn.on.toml" },
      { .manifest = "spn.off.toml" },
    },
  },
  {
    .name = "fact_identity",
    .project = "test/integration/fixtures/options/fact_identity",
    .builds = {
      { .present = true },
      { .profile = "release" },
      { .present = true },
    },
  },
  {
    .name = "default_identity",
    .project = "test/integration/fixtures/options/default_identity",
    .copy = { "main.on.c", "spn.on.toml" },
    .builds = {
      { .present = true },
      { .manifest = "spn.on.toml" },
    },
  },
  {
    .name = "private_versions",
    .project = "test/integration/fixtures/options/private_versions",
    .builds = { { .present = true } },
  },
  {
    .name = "bool_demand_on_gated_edge",
    .project = "test/integration/fixtures/worlds/bool_demand_on_gated",
    .builds = { { .expect.cc = { { .args = { "-DSPUM_GRUM", "/DSPUM_GRUM" } } } } },
  },
};

sp_test_each(options, cases, opt_test_t, tests) {
  return run_opt_test(t, *it);
}
