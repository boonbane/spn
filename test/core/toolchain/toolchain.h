#ifndef SPN_TEST_TOOLCHAIN_FIXTURE_H
#define SPN_TEST_TOOLCHAIN_FIXTURE_H

#include "spn_test.h"
#include "arg.h"
#include "triples.h"
#include "hash/digest/digest.h"
#include "paths/paths.h"
#include "enum/enum.h"
#include "intern/intern.h"
#include "toolchain/msvc.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

#define FIXTURE_MAX_ARGS 2
#define FIXTURE_MAX_TARGETS 16
#define FIXTURE_MAX_SDKS 2

#define SAN_GCC_LINUX   (SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK)
#define SAN_CLANG_LINUX (SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_MEMORY | SPN_SANITIZER_LEAK)
#define SAN_GCC_MACOS   (SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED)
#define SAN_CLANG_MACOS (SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK)
#define SAN_ZIG_UT      (SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_THREAD)

#define rt_static (si_runtime_bit(SPN_RUNTIME_STATIC))
#define rt_shared (si_runtime_bit(SPN_RUNTIME_SHARED))
#define rt_both   (rt_static | rt_shared)

typedef struct {
  const c8* name;
  const c8* path;
  spn_path_root_t root;
  const c8* args [FIXTURE_MAX_ARGS];
} fixture_launcher_t;

typedef struct {
  spn_triple_t triple;
  test_arg_t sdk;
  spn_sanitizer_set_t sanitizers;
  spn_runtime_set_t runtimes;
} fixture_target_t;

typedef struct {
  spn_sdk_kind_t kind;
  test_path_t root;
  spn_arch_t arch;
  test_path_t bin;
  const c8* version;
} fixture_sdk_t;

typedef struct {
  spn_sdk_kind_t kind;
  test_path_t root;
} fixture_sdk_expect_t;

typedef struct {
  const c8* c [FIXTURE_MAX_ARGS];
  const c8* cxx [FIXTURE_MAX_ARGS];
  struct {
    spn_sanitizer_t sanitizer;
    const c8* files [FIXTURE_MAX_ARGS];
  } sanitizers [si_sanitizer_count];
} fixture_files_t;

typedef struct {
  spn_triple_t triple;
  fixture_sdk_expect_t sdk;
  spn_sanitizer_set_t sanitizers;
  fixture_files_t files;
} fixture_row_t;

typedef struct {
  test_path_t root;
  spn_arch_t arch;
  const c8* version;
  bool bare;
  const c8* redist [FIXTURE_MAX_ARGS];
} fixture_msvc_t;

typedef struct {
  test_path_t macos;
  fixture_msvc_t msvc [FIXTURE_MAX_SDKS];
} fixture_sdks_t;

static bool fixture_triple_empty(spn_triple_t triple) {
  return !triple.arch && !triple.os && !triple.abi;
}

static bool fixture_target_empty(fixture_target_t target) {
  return fixture_triple_empty(target.triple);
}

static spn_arg_t fixture_arg(fixture_launcher_t launcher) {
  if (launcher.path) {
    return spn_arg_path((spn_path_t) { .root = launcher.root, .sub = sp_cstr_as_str(launcher.path) });
  }
  return spn_arg_lit(sp_cstr_as_str(launcher.name));
}

static spn_path_t fixture_path(test_path_t path) {
  if (!path.path) {
    return sp_zero_struct(spn_path_t);
  }
  return (spn_path_t) { .root = path.root, .sub = sp_cstr_as_str(path.path) };
}

static spn_sdk_t fixture_sdk(sp_mem_t mem, fixture_sdk_t sdk) {
  switch (sdk.kind) {
    case SPN_SDK_NONE: return sp_zero_struct(spn_sdk_t);
    case SPN_SDK_HOST: return (spn_sdk_t) { .kind = SPN_SDK_HOST };
    case SPN_SDK_SYSROOT: return spn_sdk_at(mem, (spn_triple_t) HOST_X64_LINUX, fixture_path(sdk.root));
    case SPN_SDK_MACOS: return spn_sdk_at(mem, (spn_triple_t) HOST_ARM_MACOS, fixture_path(sdk.root));
    case SPN_SDK_MSVC: {
      spn_sdk_t msvc = spn_sdk_at(mem, (spn_triple_t) { sdk.arch, SPN_OS_WINDOWS, SPN_ABI_MSVC }, fixture_path(sdk.root));
      msvc.msvc.bin = fixture_path(sdk.bin);
      msvc.msvc.version = sp_cstr_as_str(sdk.version);
      return msvc;
    }
  }
  sp_unreachable_return(sp_zero_struct(spn_sdk_t));
}

static sp_msvc_path_t fixture_msvc_path(const c8* str) {
  sp_msvc_path_t path = sp_zero;
  sp_str_copy_to(sp_cstr_as_str(str), path.data, SP_MSVC_PATH_MAX);
  path.len = (u32)sp_cstr_len(str);
  return path;
}

static sp_msvc_version_t fixture_msvc_version(const c8* str) {
  sp_msvc_version_t version = sp_zero;
  sp_str_copy_to(sp_cstr_as_str(str), version.str, SP_MSVC_VERSION_MAX);
  version.str_len = (u32)sp_cstr_len(str);
  return version;
}

static spn_sdk_host_t fixture_sdks(sp_mem_t mem, fixture_sdks_t sdks) {
  spn_sdk_host_t host = sp_zero;
  if (sdks.macos.path) {
    host.macos = spn_sdk_at(mem, (spn_triple_t) HOST_ARM_MACOS, fixture_path(sdks.macos)).macos;
  }
  sp_carr_for(sdks.msvc, it) {
    if (!sdks.msvc[it].root.path) {
      break;
    }
    sp_msvc_arch_t arch = si_msvc_arch(sdks.msvc[it].arch);
    sp_msvc_path_t root = fixture_msvc_path(sdks.msvc[it].root.path);
    spn_msvc_toolset_t toolset = {
      .vs = { .version.tools = fixture_msvc_version(sdks.msvc[it].version ? sdks.msvc[it].version : "14.0"), .install_path = root, .target = arch, .preferred = true, .compiler = !sdks.msvc[it].bare },
      .redist.dir = spn_path_join(mem, fixture_path(sdks.msvc[it].root), sp_str_lit("R")),
    };
    sp_carr_for(sdks.msvc[it].redist, at) {
      if (!sdks.msvc[it].redist[at]) {
        break;
      }
      si_da_push(mem, toolset.redist.files, ((spn_path_t) { .sub = sp_cstr_as_str(sdks.msvc[it].redist[at]) }));
    }
    si_da_push(mem, host.msvc.toolsets, toolset);
    si_da_push(mem, host.msvc.kits, ((sp_msvc_sdk_t) { .version = fixture_msvc_version("10.0"), .root = root, .arch = arch }));
  }
  return host;
}

static sp_err_t fixture_check_sdk(sp_test_t* t, spn_sdk_t sdk, fixture_sdk_expect_t expect) {
  sp_must_eq(t, (u32)expect.kind, (u32)sdk.kind);
  switch (sdk.kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_HOST: {
      return SP_OK;
    }
    case SPN_SDK_SYSROOT: {
      return test_check_path(t, sdk.root, expect.root);
    }
    case SPN_SDK_MACOS: {
      return test_check_path(t, sdk.macos.root, expect.root);
    }
    case SPN_SDK_MSVC: {
      spn_path_t root = spn_path_empty(sdk.msvc.install.vs) ? spn_path_parent(spn_path_parent(sdk.msvc.include.vc)) : sdk.msvc.install.vs;
      return test_check_path(t, root, expect.root);
    }
  }
  sp_unreachable_return(SP_ERR);
}

static spn_arg_t fixture_sdk_arg(test_arg_t sdk) {
  if (sdk.path) {
    return spn_arg_path((spn_path_t) { .root = sdk.root, .sub = sp_cstr_as_str(sdk.path) });
  }
  if (sdk.name) {
    return spn_arg_lit(sp_cstr_as_str(sdk.name));
  }
  return sp_zero_struct(spn_arg_t);
}

static spn_toolchain_target_t fixture_target(fixture_target_t target) {
  return (spn_toolchain_target_t) { .triple = target.triple, .sdk = fixture_sdk_arg(target.sdk), .runtime.supported = target.runtimes, .sanitizers.supported = target.sanitizers };
}

static sp_err_t fixture_check_triples(sp_test_t* t, sp_da(spn_triple_t) triples, const spn_triple_t* expect, u32 count) {
  sp_must_eq(t, count, (u32)sp_da_size(triples));
  sp_for(it, count) {
    sp_expect(t, spn_triple_equal(expect[it], triples[it]));
  }
  return SP_OK;
}

static sp_err_t fixture_check_paths(sp_test_t* t, si_da(spn_path_t) paths, const c8* const* expect) {
  u32 count = 0;
  while (count < FIXTURE_MAX_ARGS && expect[count]) {
    count++;
  }
  sp_must_eq(t, count, (u32)si_da_size(paths));
  sp_for(it, count) {
    sp_expect_str_eq_c(t, paths[it].sub, expect[it]);
  }
  return SP_OK;
}

static sp_err_t fixture_check_files(sp_test_t* t, const spn_toolchain_files_t* files, const fixture_files_t* expect) {
  static const c8* const none [FIXTURE_MAX_ARGS] = sp_zero;
  sp_try(fixture_check_paths(t, files->c, expect->c));
  sp_try(fixture_check_paths(t, files->cxx, expect->cxx));
  sp_for(slot, si_sanitizer_count) {
    const c8* const* listed = none;
    sp_carr_for(expect->sanitizers, at) {
      if (expect->sanitizers[at].sanitizer == si_sanitizer_at(slot)) {
        listed = expect->sanitizers[at].files;
      }
    }
    sp_try(fixture_check_paths(t, files->sanitizers[slot], listed));
  }
  return SP_OK;
}

static sp_err_t fixture_check_rows(sp_test_t* t, sp_da(spn_toolchain_row_t) rows, const fixture_row_t* expect) {
  sp_mem_t mem = sp_test_arena(t);
  u32 count = 0;
  while (count < FIXTURE_MAX_TARGETS && !fixture_triple_empty(expect[count].triple)) {
    count++;
  }
  sp_must_eq(t, count, (u32)sp_da_size(rows));
  sp_for(it, count) {
    sp_expect_str_eq(t, spn_triple_to_str(mem, rows[it].triple), spn_triple_to_str(mem, expect[it].triple));
    sp_expect_eq(t, expect[it].sanitizers, rows[it].sanitizers);
    if (fixture_check_sdk(t, rows[it].sdk, expect[it].sdk)) {
      return SP_ERR;
    }
    sp_try(fixture_check_files(t, &rows[it].files, &expect[it].files));
  }
  return SP_OK;
}

static sp_err_t fixture_decls(sp_test_t* t, const c8* file, sp_da(spn_toolchain_decl_t)* decls, sp_da(spn_toml_issue_t)* issues) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t path = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(TOOLCHAINS_DIR)), sp_cstr_as_str(file));
  sp_str_t toml = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, path, &toml));
  spn_test_lower_toolchains(t, toml, SPN_PATH_ROOT_NONE, decls, issues);
  return SP_OK;
}

static sp_err_t fixture_catalog(sp_test_t* t, spn_toolchain_catalog_t* catalog, const c8* file, spn_triple_t host, spn_sdk_host_t sdks) {
  sp_da(spn_toolchain_decl_t) decls = SP_NULLPTR;
  sp_da(spn_toml_issue_t) issues = SP_NULLPTR;
  if (fixture_decls(t, file, &decls, &issues)) {
    return SP_ERR;
  }
  sp_must(t, sp_da_empty(issues));
  spn_toolchain_catalog_init(catalog, host, sdks, sp_test_arena(t));
  sp_da_for(decls, it) {
    spn_toolchain_catalog_add(catalog, decls[it]);
  }
  return SP_OK;
}

static spn_toolchain_decl_t fixture_local_toolchain(const c8* name, fixture_launcher_t compiler) {
  sp_intern_str_t interned = spn_intern_cstr(name);
  return (spn_toolchain_decl_t) {
    .id = interned.id,
    .name = interned,
    .driver = SPN_CC_DRIVER_GCC,
    .compiler = { .program = fixture_arg(compiler) },
    .archiver = { .program = spn_arg_lit(sp_cstr_as_str("ar")) },
    .host_row = true,
  };
}

#endif
