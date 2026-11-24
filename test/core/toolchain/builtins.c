#include "toolchain.h"

static bool builtins_is_sha256(sp_str_t str) {
  return str.len == 64 && test_str_is_hex(str);
}

static sp_err_t builtins_decls(sp_test_t* t, sp_da(spn_toolchain_decl_t)* decls) {
  sp_str_t toml = sp_zero;
  if (spn_test_builtin_toml(t, &toml)) {
    return SP_ERR;
  }
  sp_da(spn_toml_issue_t) issues = SP_NULLPTR;
  spn_test_lower_toolchains(t, toml, SPN_PATH_ROOT_NONE, decls, &issues);
  sp_must(t, sp_da_empty(issues));
  return SP_OK;
}

typedef struct {
  const c8* name;
  const c8* toolchain;
  spn_runtime_set_t expect;
} runtime_test_t;

static const runtime_test_t runtime_tests [] = {
  { "zig_windows_msvc",  "zig",  rt_both },
  { "msvc",              "msvc", rt_both },
  { "llvm_windows_msvc", "llvm", rt_both },
};

sp_test_each(builtins, runtimes, runtime_test_t, runtime_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_da(spn_toolchain_decl_t) decls = SP_NULLPTR;
  if (builtins_decls(t, &decls)) {
    return SP_ERR;
  }
  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, (spn_triple_t) HOST_X64_WINDOWS, fixture_sdks(mem, (fixture_sdks_t) { .msvc = { { { "/X" }, SPN_ARCH_X64 } } }), mem);
  sp_da_for(decls, at) {
    spn_toolchain_catalog_add(&catalog, decls[at]);
  }

  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, spn_intern_cstr(it->toolchain).id);
  sp_must(t, info);
  const spn_toolchain_row_t* row = spn_toolchain_row(info, (spn_triple_t) TARGET_WIN_MSVC);
  sp_must(t, row);
  sp_expect_eq(t, it->expect, row->runtimes);
  return SP_OK;
}

sp_test(builtins, well_formed, .setup = spn_test_ctx_setup) {
  sp_da(spn_toolchain_decl_t) decls = SP_NULLPTR;
  if (builtins_decls(t, &decls)) {
    return SP_ERR;
  }
  sp_must(t, !sp_da_empty(decls));

  sp_da_for(decls, it) {
    spn_toolchain_decl_t* decl = &decls[it];
    sp_expect(t, !spn_arg_empty(decl->cxx.program));

    switch (decl->source) {
      case SPN_TOOLCHAIN_SOURCE_LOCAL: {
        break;
      }
      case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION: {
        sp_expect(t, !sp_str_empty(decl->version));
        sp_da_for(decl->hosts, ht) {
          spn_artifact_t artifact = decl->hosts[ht].artifact;
          sp_expect(t, sp_str_starts_with(artifact.url, sp_str_lit("https://")));
          sp_expect(t, builtins_is_sha256(artifact.sha256));
        }
        break;
      }
      case SPN_TOOLCHAIN_SOURCE_DETECTED: {
        break;
      }
    }
  }

  return SP_OK;
}
