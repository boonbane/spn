#include "unit.h"

typedef struct {
  const c8* name;
  graph_t a;
  graph_t b;
  bool equal;
} test_t;

static const test_t tests [] = {
  { .name = "same_sdk_same_fingerprint",        .a = { .sdk = "/S" },                                     .b = { .sdk = "/S" }, .equal = true },
  { .name = "sysroot_changes_fingerprint",      .a = { .sdk = "/S" },                                     .b = { .sdk = "/T" } },
  { .name = "absent_sysroot_differs",           .a = sp_zero,                                             .b = { .sdk = "/S" } },
  { .name = "msvc_sdk_changes_fingerprint",     .a = { .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .sdk = "/X" }, .b = { .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .sdk = "/Y" } },
  { .name = "macos_sdk_changes_fingerprint",    .a = { .os = SPN_OS_MACOS, .sdk = "/S" },                 .b = { .os = SPN_OS_MACOS, .sdk = "/T" } },
  { .name = "artifact_sha_changes_fingerprint", .a = { .sha256 = "A" },                                   .b = { .sha256 = "B" } },
  { .name = "runtime_changes_fingerprint",      .a = { .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .runtime = SPN_RUNTIME_STATIC }, .b = { .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .runtime = SPN_RUNTIME_SHARED } },
  { .name = "libc_changes_fingerprint",         .a = { .libc = SPN_RUNTIME_STATIC },                      .b = { .libc = SPN_RUNTIME_SHARED } },
  { .name = "mode_changes_fingerprint",         .a = { .mode = SPN_MODE_DEBUG },                          .b = { .mode = SPN_MODE_RELEASE } },
  { .name = "opt_level_changes_fingerprint",    .a = { .opt = SPN_OPT_LEVEL_0 },                          .b = { .opt = SPN_OPT_LEVEL_2 } },
  { .name = "standard_changes_fingerprint",     .a = { .standard = SPN_C11 },                             .b = { .standard = SPN_C99 } },
  { .name = "sanitizer_changes_fingerprint",    .a = sp_zero,                                             .b = { .sanitize = SPN_SANITIZER_ADDRESS } },
  { .name = "compiler_program_changes_fingerprint", .a = { .cc = "cc" },                                  .b = { .cc = "gcc" } },
  { .name = "link_args_change_fingerprint",     .a = sp_zero,                                             .b = { .link_args = { "-fuse-ld=lld" } } },
  // Default option values ride the manifest; only a non-default value is identity
  {
    .name = "default_option_is_inert",
    .a = { .pkgs = { { .name = "A", .options = { { "X", .value = true, .is_default = true } } } } },
    .b = { .pkgs = { { .name = "A" } } },
    .equal = true,
  },
  {
    .name = "option_value_changes_fingerprint",
    .a = { .pkgs = { { .name = "A", .options = { { "X", .value = true } } } } },
    .b = { .pkgs = { { .name = "A", .options = { { "X", .value = false } } } } },
  },
  {
    .name = "config_kind_changes_fingerprint",
    .a = { .pkgs = { { .name = "A" } } },
    .b = { .pkgs = { { .name = "A", .config = SPN_LIB_KIND_STATIC } } },
  },
  {
    .name = "dep_privacy_changes_fingerprint",
    .a = { .pkgs = { { .name = "A", .deps = { { "B" } } }, { .name = "B" } } },
    .b = { .pkgs = { { .name = "A", .deps = { { "B", .private = true } } }, { .name = "B" } } },
  },
  {
    .name = "dep_kind_changes_fingerprint",
    .a = { .pkgs = { { .name = "A", .deps = { { "B" } } }, { .name = "B" } } },
    .b = { .pkgs = { { .name = "A", .deps = { { "B", SPN_DEP_KIND_TEST } } }, { .name = "B" } } },
  },
  {
    .name = "dep_order_is_inert",
    .a = { .pkgs = { { .name = "A", .deps = { { "B" }, { "C" } } }, { .name = "B" }, { .name = "C" } } },
    .b = { .pkgs = { { .name = "A", .deps = { { "C" }, { "B" } } }, { .name = "B" }, { .name = "C" } } },
    .equal = true,
  },
};

static sp_hash_t fingerprint(sp_test_t* t, const graph_t* side) {
  graph_t graph = *side;
  if (!graph.pkgs[0].name) {
    graph.pkgs[0].name = "A";
  }
  units_t* u = units_new(t, &graph);
  return spn_unit_fingerprint(&u->units, u->session.builds.program, units_resolved(u, "A"));
}

sp_test_each(unit_fingerprint, input, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  sp_expect_eq(t, it->equal, fingerprint(t, &it->a) == fingerprint(t, &it->b));
  return SP_OK;
}
