#include "unit.h"

sp_test_suite(target_kind, .serial = true);

typedef struct {
  spn_err_t err;
  spn_linkage_requester_t requester;
} expect_t;

typedef struct {
  const c8* name;
  unit_graph_test_t graph;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  // Nothing in the profile asked for static linkage; the target's row derived it
  {
    .name = "derived_static_blames_the_target",
    .graph = {
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } } },
        { .name = "D", .libs = { { "D", SHARED_ONLY, .source = { "d.c" } } } },
      },
    },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_TARGET },
  },
  {
    .name = "static_libc_blames_libc",
    .graph = {
      .request = { .libc = SPN_RUNTIME_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } } },
        { .name = "D", .libs = { { "D", SHARED_ONLY, .source = { "d.c" } } } },
      },
    },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_LIBC },
  },
  {
    .name = "profile_linkage_blames_the_profile",
    .graph = {
      .request = { .linkage = SPN_LIB_KIND_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } } },
        { .name = "D", .libs = { { "D", SHARED_ONLY, .source = { "d.c" } } } },
      },
    },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_PROFILE },
  },
  {
    .name = "derived_shared_admits_a_shared_lib",
    .graph = {
      .linkage = SPN_LIB_KIND_SHARED,
      .pkgs = {
        { .name = "P", .deps = { { "D" } } },
        { .name = "D", .libs = { { "D", SHARED_ONLY, .source = { "d.c" } } } },
      },
    },
  },
};

sp_test_each(target_kind, kind, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_session_t* s = build_session(mem, &it->graph);

  spn_loaded_pkg_t* loaded = sp_ht_getp(s->packages, find_pkg_id(s, &it->graph, it->graph.pkgs[0].name));
  const c8* source [] = { "app.c", SP_NULLPTR };
  spn_target_info_t app = {
    .name = sp_str_lit("app"),
    .kind = SPN_TARGET_KIND_EXE,
    .source = test_source_list(mem, loaded->roots, source, sp_carr_len(source)),
  };
  sp_str_om_insert(s->pkg->exes, app.name, app);

  sp_must_eq(t, SPN_OK, spn_units_add_packages(s));
  spn_err_t err = spn_units_add_targets(s, SPN_UNIT_SCOPE_TARGET);
  sp_must_eq(t, (u32)it->expect.err, (u32)err);
  if (!err) {
    return SP_OK;
  }

  sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
  sp_must_eq(t, 1, sp_da_size(errs));
  sp_expect_eq(t, (u32)it->expect.err, (u32)errs[0].err.kind);
  sp_expect_eq(t, (u32)it->expect.requester, (u32)errs[0].err.target.requester);
  return SP_OK;
}
