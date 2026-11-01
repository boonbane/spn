#include "closure.h"

#include "unit/closure/closure.h"

static const target_list_test_t tests [] = {
  // Runtime collection crosses shared boundaries the link closure stops at: a
  // shared lib's own shared deps must still sit next to the executable
  {
    .name = "chase_through_shared",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .kind = SPN_LIB_KIND_SHARED, .deps = { { "D2" } } },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D3" } } },
        { .name = "D3", .kind = SPN_LIB_KIND_SHARED },
      },
    },
    .expect = { "D1", "D2", "D3" },
  },
  {
    .name = "chase_sibling_shared",
    .graph = {
      .root = "P1",
      .root_deps = { "L1" },
      .pkgs = {
        {
          .name = "P1",
          .libs = {
            { "L1", SPN_LIB_KIND_SHARED, .deps = { "L2" } },
            { "L2", SPN_LIB_KIND_STATIC },
          }
        },
      },
    },
    .expect = { "L1", "L2" },
  },
};

sp_test_each(chase, shared, target_list_test_t, tests, .setup = spn_test_ctx_setup) {
  closure_graph_t g = build_graph(&it->graph);

  return expect_lib_names(t, closure_entries(g.mem, g.root, SI_CLOSURE_CHASE_SHARED, SI_CLOSURE_YIELD_LIB), it->expect);
}
