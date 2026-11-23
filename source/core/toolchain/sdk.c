#include "toolchain/sdk.h"

#include "ctx/types.h"
#include "enum/enum.h"
#include "error/error.h"
#include "paths/paths.h"
#include "sp/sp_glob.h"
#include "toolchain/msvc.h"
#include "triple/triple.h"

spn_sdk_need_t spn_sdk_need(spn_triple_t target) {
  if (target.abi == SPN_ABI_BARE) {
    return SPN_SDK_NEED_NONE;
  }
  switch (target.os) {
    case SPN_OS_MACOS: return SPN_SDK_NEED_MACOS;
    case SPN_OS_WINDOWS: return target.abi == SPN_ABI_MSVC ? SPN_SDK_NEED_MSVC : SPN_SDK_NEED_SYSROOT;
    case SPN_OS_LINUX:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING: return SPN_SDK_NEED_SYSROOT;
    case SPN_OS_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_SDK_NEED_NONE);
}

static spn_sdk_macos_t macos_layout(sp_mem_t mem, spn_path_t root) {
  return (spn_sdk_macos_t) {
    .root = root,
    .include = spn_path_join(mem, root, sp_str_lit("usr/include")),
    .lib = spn_path_join(mem, root, sp_str_lit("usr/lib")),
    .frameworks = spn_path_join(mem, root, sp_str_lit("System/Library/Frameworks")),
  };
}

static spn_sdk_msvc_t msvc_layout(sp_mem_t mem, spn_path_t root, spn_arch_t arch) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_str_t name = spn_arch_to_str(arch);
  spn_sdk_msvc_t msvc = {
    .arch = arch,
    .include = {
      .vc = spn_path_join(mem, root, sp_str_lit("crt/include")),
      .ucrt = spn_path_join(mem, root, sp_str_lit("sdk/include/ucrt")),
      .um = spn_path_join(mem, root, sp_str_lit("sdk/include/um")),
      .shared = spn_path_join(mem, root, sp_str_lit("sdk/include/shared")),
      .winrt = spn_path_join(mem, root, sp_str_lit("sdk/include/winrt")),
      .cppwinrt = spn_path_join(mem, root, sp_str_lit("sdk/include/cppwinrt")),
    },
    .lib = {
      .vc = spn_path_join(mem, root, sp_fmt(scratch.mem, "crt/lib/{}", sp_fmt_str(name)).value),
      .ucrt = spn_path_join(mem, root, sp_fmt(scratch.mem, "sdk/lib/ucrt/{}", sp_fmt_str(name)).value),
      .um = spn_path_join(mem, root, sp_fmt(scratch.mem, "sdk/lib/um/{}", sp_fmt_str(name)).value),
    },
  };
  sp_mem_end_scratch(scratch);
  return msvc;
}

static spn_path_t absolute(sp_str_t path) {
  return (spn_path_t) { .sub = path };
}

sp_str_t spn_msvc_arch_name(spn_arch_t arch) {
  switch (arch) {
    case SPN_ARCH_X64: return sp_str_lit("x64");
    case SPN_ARCH_ARM64: return sp_str_lit("arm64");
    case SPN_ARCH_WASM32:
    case SPN_ARCH_NONE: sp_unreachable_case();
  }
  sp_unreachable_return(sp_str_lit(""));
}

static sp_str_t dia_arch_name(spn_arch_t arch) {
  switch (arch) {
    case SPN_ARCH_X64: return sp_str_lit("amd64");
    case SPN_ARCH_ARM64: return sp_str_lit("arm64");
    case SPN_ARCH_WASM32:
    case SPN_ARCH_NONE: sp_unreachable_case();
  }
  sp_unreachable_return(sp_str_lit(""));
}

spn_sdk_msvc_t si_sdk_from_msvc(sp_mem_t mem, const sp_msvc_sdk_t* kits, const sp_msvc_vs_t* vs, spn_arch_t arch) {
  sp_msvc_sdk_paths_t kit = sp_msvc_sdk_render(mem, kits);
  sp_msvc_vs_paths_t tools = sp_msvc_vs_render(mem, vs);
  spn_arch_t host = SPN_ARCH_NONE;
  switch (vs->host) {
    case SP_MSVC_ARCH_X64: host = SPN_ARCH_X64; break;
    case SP_MSVC_ARCH_ARM64: host = SPN_ARCH_ARM64; break;
  }
  sp_str_t host_name = spn_msvc_arch_name(host);
  sp_str_t version = sp_str_copy(mem, sp_msvc_version_str(&vs->version.tools));
  sp_str_t kit_version = sp_str_copy(mem, sp_msvc_version_str(&kits->version));
  spn_path_t install = absolute(sp_str_copy(mem, sp_msvc_path_str(&vs->install_path)));
  spn_path_t root = absolute(sp_str_copy(mem, sp_msvc_path_str(&kits->root)));
  spn_path_t toolset = spn_path_join(mem, install, sp_fmt(mem, "VC/Tools/MSVC/{}", sp_fmt_str(version)).value);
  spn_path_t headers = spn_path_join(mem, root, sp_fmt(mem, "Include/{}", sp_fmt_str(kit_version)).value);

  spn_sdk_msvc_t msvc = {
    .arch = arch,
    .version = version,
    .bin = absolute(tools.bin),
    .tools = {
      .sdk = spn_path_join(mem, root, sp_fmt(mem, "bin/{}/{}", sp_fmt_str(kit_version), sp_fmt_str(host_name)).value),
    },
    .include = {
      .vc = absolute(tools.include),
      .atlmfc = spn_path_join(mem, toolset, sp_str_lit("atlmfc/include")),
      .vs = spn_path_join(mem, install, sp_str_lit("VC/Auxiliary/VS/include")),
      .ucrt = absolute(kit.include_ucrt),
      .um = absolute(kit.include_um),
      .shared = absolute(kit.include_shared),
      .winrt = spn_path_join(mem, headers, sp_str_lit("winrt")),
      .cppwinrt = spn_path_join(mem, headers, sp_str_lit("cppwinrt")),
      .dia = spn_path_join(mem, install, sp_str_lit("DIA SDK/include")),
    },
    .lib = {
      .vc = absolute(tools.lib),
      .atlmfc = spn_path_join(mem, toolset, sp_fmt(mem, "atlmfc/lib/{}", sp_fmt_str(spn_msvc_arch_name(arch))).value),
      .ucrt = absolute(kit.lib_ucrt),
      .um = absolute(kit.lib_um),
      .dia = spn_path_join(mem, install, sp_fmt(mem, "DIA SDK/lib/{}", sp_fmt_str(dia_arch_name(arch))).value),
    },
    .install = {
      .vs = install,
      .toolset = toolset,
      .host = host,
      .kit = {
        .root = root,
        .version = kit_version,
      },
    },
  };
  if (host != arch) {
    msvc.tools.host = spn_path_join(mem, toolset, sp_fmt(mem, "bin/Host{}/{}", sp_fmt_str(host_name), sp_fmt_str(host_name)).value);
  }
  if (arch == SPN_ARCH_X64) {
    msvc.runtime.asan = spn_path_join(mem, msvc.bin, sp_str_lit("clang_rt.asan_dynamic-x86_64.dll"));
  }
  return msvc;
}

static void push_dir(spn_dir_list_t* dirs, spn_path_t dir) {
  if (spn_path_empty(dir)) {
    return;
  }
  sp_assert(dirs->count < SPN_DIR_LIST_MAX);
  dirs->items[dirs->count++] = dir;
}

spn_dir_list_t spn_msvc_include_dirs(const spn_sdk_msvc_t* msvc) {
  spn_dir_list_t dirs = sp_zero;
  push_dir(&dirs, msvc->include.vc);
  push_dir(&dirs, msvc->include.atlmfc);
  push_dir(&dirs, msvc->include.vs);
  push_dir(&dirs, msvc->include.ucrt);
  push_dir(&dirs, msvc->include.um);
  push_dir(&dirs, msvc->include.shared);
  push_dir(&dirs, msvc->include.winrt);
  push_dir(&dirs, msvc->include.cppwinrt);
  push_dir(&dirs, msvc->include.dia);
  return dirs;
}

spn_dir_list_t spn_msvc_lib_dirs(const spn_sdk_msvc_t* msvc) {
  spn_dir_list_t dirs = sp_zero;
  push_dir(&dirs, msvc->lib.vc);
  push_dir(&dirs, msvc->lib.atlmfc);
  push_dir(&dirs, msvc->lib.ucrt);
  push_dir(&dirs, msvc->lib.um);
  push_dir(&dirs, msvc->lib.dia);
  return dirs;
}

static const c8* const msvc_shell_vars [] = {
  "__DOTNET_ADD_64BIT",
  "__DOTNET_PREFERRED_BITNESS",
  "__VSCMD_PREINIT_PATH",
  "__VSCMD_PREINIT_VCToolsVersion",
  "CommandPromptType",
  "DevEnvDir",
  "ExtensionSdkDir",
  "EXTERNAL_INCLUDE",
  "Framework40Version",
  "FrameworkDir",
  "FrameworkDir32",
  "FrameworkDir64",
  "FrameworkVersion",
  "FrameworkVersion32",
  "FrameworkVersion64",
  "INCLUDE",
  "LIB",
  "LIBPATH",
  "NETFXSDKDir",
  "Platform",
  "UCRTVersion",
  "UniversalCRTSdkDir",
  "VCIDEInstallDir",
  "VCINSTALLDIR",
  "VCPKG_ROOT",
  "VCToolsInstallDir",
  "VCToolsRedistDir",
  "VCToolsVersion",
  "VisualStudioVersion",
  "VS150COMNTOOLS",
  "VS160COMNTOOLS",
  "VS170COMNTOOLS",
  "VS180COMNTOOLS",
  "VSCMD_ARG_app_plat",
  "VSCMD_ARG_HOST_ARCH",
  "VSCMD_ARG_TGT_ARCH",
  "VSCMD_VER",
  "VSINSTALLDIR",
  "WindowsLibPath",
  "WindowsSDK_ExecutablePath_x64",
  "WindowsSDK_ExecutablePath_x86",
  "WindowsSdkBinPath",
  "WindowsSdkDir",
  "WindowsSDKLibVersion",
  "WindowsSdkVerBinPath",
  "WindowsSDKVersion",
};

u32 spn_msvc_shell_vars(const c8* const** vars) {
  *vars = msvc_shell_vars;
  return sp_carr_len(msvc_shell_vars);
}

bool spn_msvc_leave_shell(sp_env_t* env) {
  if (sp_str_empty(sp_env_get_c(env, "VSCMD_VER"))) {
    return false;
  }
  sp_carr_for(msvc_shell_vars, it) {
    sp_env_erase_c(env, msvc_shell_vars[it]);
  }
  return true;
}

spn_dir_list_t spn_sdk_tool_dirs(const spn_sdk_t* sdk) {
  spn_dir_list_t dirs = sp_zero;
  switch (sdk->kind) {
    case SPN_SDK_MSVC: {
      push_dir(&dirs, sdk->msvc.tools.host);
      push_dir(&dirs, sdk->msvc.tools.sdk);
      break;
    }
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_MACOS:
    case SPN_SDK_HOST: {
      break;
    }
  }
  return dirs;
}

spn_sdk_t spn_sdk_at(sp_mem_t mem, spn_triple_t target, spn_path_t root) {
  switch (spn_sdk_need(target)) {
    case SPN_SDK_NEED_SYSROOT: return (spn_sdk_t) { .kind = SPN_SDK_SYSROOT, .root = root };
    case SPN_SDK_NEED_MACOS: return (spn_sdk_t) { .kind = SPN_SDK_MACOS, .macos = macos_layout(mem, root) };
    case SPN_SDK_NEED_MSVC: return (spn_sdk_t) { .kind = SPN_SDK_MSVC, .msvc = msvc_layout(mem, root, target.arch) };
    case SPN_SDK_NEED_NONE: sp_unreachable_case();
  }
  sp_unreachable_return(sp_zero_struct(spn_sdk_t));
}

spn_sdk_source_t si_sdk_undeclared(spn_triple_t target) {
  switch (spn_sdk_need(target)) {
    case SPN_SDK_NEED_NONE:
    case SPN_SDK_NEED_SYSROOT:
    case SPN_SDK_NEED_MACOS: return SPN_SDK_SOURCE_HOST;
    case SPN_SDK_NEED_MSVC: return SPN_SDK_SOURCE_INSTALL;
  }
  sp_unreachable_return(SPN_SDK_SOURCE_HOST);
}

spn_sdk_t si_sdk_host(sp_mem_t mem, const spn_sdk_host_t* host, spn_triple_t target) {
  spn_sdk_t none = sp_zero;
  switch (spn_sdk_need(target)) {
    case SPN_SDK_NEED_NONE:
    case SPN_SDK_NEED_SYSROOT: {
      return none;
    }
    case SPN_SDK_NEED_MACOS: {
      return spn_path_empty(host->macos.root) ? none : (spn_sdk_t) { .kind = SPN_SDK_MACOS, .macos = host->macos };
    }
    case SPN_SDK_NEED_MSVC: {
      spn_msvc_pick_t pick = sp_zero;
      if (si_msvc_match(&host->msvc, target.arch, sp_zero_s(spn_msvc_request_t), false, &pick)) {
        return none;
      }
      return (spn_sdk_t) { .kind = SPN_SDK_MSVC, .msvc = si_sdk_from_msvc(mem, pick.kit, &pick.toolset->vs, target.arch) };
    }
  }
  sp_unreachable_return(none);
}

bool si_sdk_served(sp_mem_t mem, const spn_sdk_host_t* sdks, spn_triple_t host, spn_triple_t target, spn_sdk_t* sdk) {
  *sdk = si_sdk_host(mem, sdks, target);
  switch (spn_sdk_need(target)) {
    case SPN_SDK_NEED_NONE: return true;
    case SPN_SDK_NEED_SYSROOT: {
      if (!spn_triple_equal(target, host)) {
        return false;
      }
      *sdk = (spn_sdk_t) { .kind = SPN_SDK_HOST };
      return true;
    }
    case SPN_SDK_NEED_MACOS:
    case SPN_SDK_NEED_MSVC: return sdk->kind != SPN_SDK_NONE;
  }
  sp_unreachable_return(false);
}

static sp_str_t xcrun_sdk(sp_mem_t mem) {
  sp_ps_output_t result = sp_ps_run(mem, (sp_ps_config_t) {
    .command = sp_str_lit("xcrun"),
    .args = { sp_str_lit("--show-sdk-path") },
    .io = {
      .in = { .mode = SP_PS_IO_MODE_NULL },
      .err = { .mode = SP_PS_IO_MODE_NULL },
    },
  });
  return result.status.exit_code ? sp_str_lit("") : sp_str_trim(result.out);
}

spn_sdk_host_t si_sdk_detect(sp_mem_t mem, const spn_path_roots_t* roots, sp_env_t* env, spn_triple_t host) {
  spn_sdk_host_t sdks = sp_zero;
  sp_str_t macos = sp_env_get(env, sp_str_lit("SPN_MACOS_SDK"));
  if (sp_str_empty(macos) && host.os == SPN_OS_MACOS) {
    macos = xcrun_sdk(mem);
  }
  if (!sp_str_empty(macos)) {
    sdks.macos = macos_layout(mem, spn_path_from_cwd(mem, roots, macos));
  }
  if (host.os != SPN_OS_WINDOWS) {
    return sdks;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_msvc_t* found = sp_alloc_type(scratch.mem, sp_msvc_t);
  const spn_arch_t* arches = SP_NULLPTR;
  u32 count = spn_os_archs(SPN_OS_WINDOWS, &arches);
  sp_for(it, count) {
    if (sp_msvc_find_ex(si_msvc_arch(arches[it]), found) != SP_MSVC_OK) {
      continue;
    }
    sp_for(at, found->num_installations) {
      const sp_msvc_vs_t* vs = &found->installations[at];
      sp_str_t vc = sp_fs_join_path(scratch.mem, sp_msvc_path_str(&vs->install_path), sp_str_lit("VC"));
      sp_str_t version = sp_zero;
      sp_fs_read(scratch.mem, sp_fs_join_path(scratch.mem, vc, sp_str_lit("Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")), &version);
      sp_str_t root = sp_fs_join_path(scratch.mem, vc, sp_str_lit("Redist/MSVC"));
      root = sp_fs_join_path(scratch.mem, root, sp_str_trim(version));
      root = sp_fs_join_path(scratch.mem, root, spn_msvc_arch_name(arches[it]));
      spn_msvc_redist_t redist = { .dir = absolute(sp_str_copy(mem, root)) };
      sp_sys_dir_t dir = sp_zero;
      if (!sp_fs_open_dir_at(sp_path_from_str(root), &dir)) {
        sp_glob_t* dll = sp_glob_new(scratch.mem, "Microsoft.VC*.CRT/*.dll");
        sp_fs_it_t walk = sp_fs_it(scratch.mem, dir);
        sp_fs_it_entry_t entry = sp_zero;
        while (sp_fs_it_next(&walk, &entry)) {
          if (sp_glob_match(dll, entry.path)) {
            si_da_push(mem, redist.files, absolute(sp_fs_join_path(mem, root, entry.path)));
          }
        }
        if (walk.err) {
          si_da_clear(redist.files);
        }
        sp_fs_it_deinit(&walk);
        sp_fs_close_dir(dir);
      }
      si_da_push(mem, sdks.msvc.toolsets, ((spn_msvc_toolset_t) { .vs = *vs, .redist = redist }));
    }
    sp_for(at, found->num_sdks) {
      si_da_push(mem, sdks.msvc.kits, found->sdks[at]);
    }
  }
  sp_mem_end_scratch(scratch);
  return sdks;
}

static sp_hash_t hash_dirs(spn_dir_list_t dirs) {
  sp_hash_t hash = dirs.count;
  sp_for(it, dirs.count) {
    sp_hash_t parts [] = { hash, spn_path_hash(dirs.items[it]) };
    hash = sp_hash_combine(parts, sp_carr_len(parts));
  }
  return hash;
}

sp_hash_t spn_sdk_hash(const spn_sdk_t* sdk) {
  switch (sdk->kind) {
    case SPN_SDK_NONE: {
      return 0;
    }
    case SPN_SDK_HOST: {
      return (sp_hash_t)sdk->kind;
    }
    case SPN_SDK_SYSROOT: {
      sp_hash_t parts [] = { (sp_hash_t)sdk->kind, spn_path_hash(sdk->root) };
      return sp_hash_combine(parts, sp_carr_len(parts));
    }
    case SPN_SDK_MACOS: {
      sp_hash_t parts [] = { (sp_hash_t)sdk->kind, spn_path_hash(sdk->macos.root), spn_path_hash(sdk->macos.include), spn_path_hash(sdk->macos.lib), spn_path_hash(sdk->macos.frameworks) };
      return sp_hash_combine(parts, sp_carr_len(parts));
    }
    case SPN_SDK_MSVC: {
      sp_hash_t parts [] = {
        (sp_hash_t)sdk->kind,
        (sp_hash_t)sdk->msvc.arch,
        sp_hash_bytes(sdk->msvc.version.data, sdk->msvc.version.len, 0),
        spn_path_hash(sdk->msvc.bin),
        hash_dirs(spn_sdk_tool_dirs(sdk)),
        hash_dirs(spn_msvc_include_dirs(&sdk->msvc)),
        hash_dirs(spn_msvc_lib_dirs(&sdk->msvc)),
      };
      return sp_hash_combine(parts, sp_carr_len(parts));
    }
  }
  sp_unreachable_return(0);
}

bool spn_sdk_libc(spn_cc_cap_set_t caps, const spn_sdk_t* sdk, spn_libc_t* libc) {
  if (!(caps & SPN_CC_CAP_LIBC_FILE)) {
    return false;
  }
  switch (sdk->kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_HOST: {
      return false;
    }
    case SPN_SDK_MACOS: {
      *libc = (spn_libc_t) {
        .include = sdk->macos.include,
        .sys_include = sdk->macos.include,
        .frameworks = sdk->macos.frameworks,
      };
      push_dir(&libc->search.lib, sdk->macos.lib);
      return true;
    }
    case SPN_SDK_MSVC: {
      const spn_sdk_msvc_t* msvc = &sdk->msvc;
      *libc = (spn_libc_t) {
        .include = msvc->include.ucrt,
        .sys_include = msvc->include.vc,
        .crt = msvc->lib.ucrt,
        .msvc_lib = msvc->lib.vc,
        .kernel32_lib = msvc->lib.um,
      };
      push_dir(&libc->search.include, msvc->include.atlmfc);
      push_dir(&libc->search.include, msvc->include.vs);
      push_dir(&libc->search.include, msvc->include.winrt);
      push_dir(&libc->search.include, msvc->include.cppwinrt);
      push_dir(&libc->search.include, msvc->include.dia);
      push_dir(&libc->search.lib, msvc->lib.atlmfc);
      push_dir(&libc->search.lib, msvc->lib.dia);
      return true;
    }
  }
  sp_unreachable_return(false);
}
