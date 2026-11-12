#include "toolchain.h"

typedef struct {
  const c8* version;
  const c8* bin;
  const c8* tools [SPN_DIR_LIST_MAX];
  const c8* include [SPN_DIR_LIST_MAX];
  const c8* lib [SPN_DIR_LIST_MAX];
  struct {
    const c8* vs;
    const c8* toolset;
    spn_arch_t host;
    struct {
      const c8* root;
      const c8* version;
    } kit;
  } install;
} expect_t;

typedef struct {
  const c8* name;
  const c8* kits;
  const c8* kits_version;
  const c8* vs;
  const c8* tools_version;
  spn_arch_t arch;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "roles_map_to_install_dirs",
    .kits = "C:/K/10", .kits_version = "10.0.1.0", .vs = "C:/V", .tools_version = "14.1", .arch = SPN_ARCH_X64,
    .expect = {
      .version = "14.1",
      .bin = "C:/V/VC/Tools/MSVC/14.1/bin/Hostx64/x64",
      .tools = {
        "C:/K/10/bin/10.0.1.0/x64",
      },
      .include = {
        "C:/V/VC/Tools/MSVC/14.1/include",
        "C:/V/VC/Tools/MSVC/14.1/atlmfc/include",
        "C:/V/VC/Auxiliary/VS/include",
        "C:/K/10/Include/10.0.1.0/ucrt",
        "C:/K/10/Include/10.0.1.0/um",
        "C:/K/10/Include/10.0.1.0/shared",
        "C:/K/10/Include/10.0.1.0/winrt",
        "C:/K/10/Include/10.0.1.0/cppwinrt",
      },
      .lib = {
        "C:/V/VC/Tools/MSVC/14.1/Lib/x64",
        "C:/V/VC/Tools/MSVC/14.1/atlmfc/lib/x64",
        "C:/K/10/Lib/10.0.1.0/ucrt/x64",
        "C:/K/10/Lib/10.0.1.0/um/x64",
      },
      .install = { .vs = "C:/V", .toolset = "C:/V/VC/Tools/MSVC/14.1", .host = SPN_ARCH_X64, .kit = { "C:/K/10", "10.0.1.0" } },
    },
  },
  {
    .name = "cross_target_keeps_the_host_tools",
    .kits = "C:/K/10", .kits_version = "10.0.1.0", .vs = "C:/V", .tools_version = "14.1", .arch = SPN_ARCH_ARM64,
    .expect = {
      .version = "14.1",
      .bin = "C:/V/VC/Tools/MSVC/14.1/bin/Hostx64/arm64",
      .tools = {
        "C:/V/VC/Tools/MSVC/14.1/bin/Hostx64/x64",
        "C:/K/10/bin/10.0.1.0/x64",
      },
      .include = {
        "C:/V/VC/Tools/MSVC/14.1/include",
        "C:/V/VC/Tools/MSVC/14.1/atlmfc/include",
        "C:/V/VC/Auxiliary/VS/include",
        "C:/K/10/Include/10.0.1.0/ucrt",
        "C:/K/10/Include/10.0.1.0/um",
        "C:/K/10/Include/10.0.1.0/shared",
        "C:/K/10/Include/10.0.1.0/winrt",
        "C:/K/10/Include/10.0.1.0/cppwinrt",
      },
      .lib = {
        "C:/V/VC/Tools/MSVC/14.1/Lib/arm64",
        "C:/V/VC/Tools/MSVC/14.1/atlmfc/lib/arm64",
        "C:/K/10/Lib/10.0.1.0/ucrt/arm64",
        "C:/K/10/Lib/10.0.1.0/um/arm64",
      },
      .install = { .vs = "C:/V", .toolset = "C:/V/VC/Tools/MSVC/14.1", .host = SPN_ARCH_X64, .kit = { "C:/K/10", "10.0.1.0" } },
    },
  },
};

static sp_msvc_path_t msvc_path(const c8* str) {
  sp_msvc_path_t path = sp_zero;
  sp_str_copy_to(sp_cstr_as_str(str), path.data, SP_MSVC_PATH_MAX);
  path.len = (u32)sp_cstr_len(str);
  return path;
}

static sp_msvc_version_t msvc_version(const c8* str) {
  sp_msvc_version_t version = sp_zero;
  sp_str_copy_to(sp_cstr_as_str(str), version.str, SP_MSVC_VERSION_MAX);
  version.str_len = (u32)sp_cstr_len(str);
  return version;
}

static sp_msvc_arch_t msvc_arch(spn_arch_t arch) {
  return arch == SPN_ARCH_ARM64 ? SP_MSVC_ARCH_ARM64 : SP_MSVC_ARCH_X64;
}

static sp_err_t check_dirs(sp_test_t* t, spn_dir_list_t dirs, const c8* const* expect, u32 count) {
  sp_must_eq(t, count, dirs.count);
  sp_for(it, count) {
    if (test_check_path(t, dirs.items[it], (test_path_t) { expect[it] })) {
      return SP_ERR;
    }
  }
  return SP_OK;
}

sp_test_each(sdk_msvc, from_install, test_t, tests) {
  sp_msvc_sdk_t kits = { .version = msvc_version(it->kits_version), .root = msvc_path(it->kits), .arch = msvc_arch(it->arch) };
  sp_msvc_vs_t vs = { .version.tools = msvc_version(it->tools_version), .install_path = msvc_path(it->vs), .host = SP_MSVC_ARCH_X64, .target = msvc_arch(it->arch) };
  spn_sdk_t sdk = { .kind = SPN_SDK_MSVC, .msvc = spn_sdk_from_msvc(sp_test_arena(t), &kits, &vs, it->arch) };
  const spn_sdk_msvc_t* msvc = &sdk.msvc;
  sp_expect_eq(t, (u32)it->arch, (u32)msvc->arch);
  sp_expect_str_eq_c(t, msvc->version, it->expect.version);
  if (test_check_path(t, msvc->bin, (test_path_t) { it->expect.bin })) {
    return SP_ERR;
  }
  u32 tools = 0;
  sp_carr_detect_len(it->expect.tools, tools, it->expect.tools[tools]);
  u32 include = 0;
  sp_carr_detect_len(it->expect.include, include, it->expect.include[include]);
  u32 lib = 0;
  sp_carr_detect_len(it->expect.lib, lib, it->expect.lib[lib]);
  if (check_dirs(t, spn_sdk_tool_dirs(&sdk), it->expect.tools, tools) || check_dirs(t, spn_msvc_include_dirs(msvc), it->expect.include, include) || check_dirs(t, spn_msvc_lib_dirs(msvc), it->expect.lib, lib)) {
    return SP_ERR;
  }
  if (test_check_path(t, msvc->install.vs, (test_path_t) { it->expect.install.vs }) || test_check_path(t, msvc->install.toolset, (test_path_t) { it->expect.install.toolset }) || test_check_path(t, msvc->install.kit.root, (test_path_t) { it->expect.install.kit.root })) {
    return SP_ERR;
  }
  sp_expect_str_eq_c(t, msvc->install.kit.version, it->expect.install.kit.version);
  sp_expect_eq(t, (u32)it->expect.install.host, (u32)msvc->install.host);
  return SP_OK;
}
