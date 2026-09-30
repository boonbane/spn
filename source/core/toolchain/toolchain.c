#include "sp.h"
#include "macro/macro.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

spn_path_t spn_toolchain_artifact_root(spn_artifact_t artifact) {
  return (spn_path_t) { .root = SPN_PATH_ROOT_TOOLCHAIN, .sub = artifact.sha256 };
}

spn_toolchain_launcher_t spn_toolchain_launcher_with_root(sp_mem_t mem, spn_toolchain_launcher_t launcher, spn_path_t root) {
  sp_assert(spn_path_empty(launcher.program.path));
  sp_str_t name = launcher.program.prefix;
#if defined(SP_WIN32)
  name = sp_fmt(mem, "{}.exe", sp_fmt_str(name)).value;
#endif

  spn_toolchain_launcher_t result = launcher;
  result.program = spn_arg_path(spn_path_join(mem, root, name));
  return result;
}

static spn_path_check_t local_arg(spn_path_root_t base, sp_str_t str, spn_arg_t* arg) {
  if (sp_fs_is_absolute(str)) {
    *arg = spn_arg_path((spn_path_t) { .sub = str });
    return SPN_PATH_OK;
  }
  if (base == SPN_PATH_ROOT_NONE) {
    return SPN_PATH_UNROOTED;
  }
  *arg = spn_arg_path((spn_path_t) { .root = base, .sub = str });
  return SPN_PATH_OK;
}

spn_path_check_t spn_toolchain_sdk_path(spn_toolchain_source_t source, spn_path_root_t base, sp_str_t str, spn_arg_t* sdk) {
  if (!spn_path_normal(str)) {
    return SPN_PATH_MALFORMED;
  }
  switch (source) {
    case SPN_TOOLCHAIN_SOURCE_LOCAL: {
      return local_arg(base, str, sdk);
    }
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION:
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      *sdk = sp_fs_is_absolute(str) ? spn_arg_path((spn_path_t) { .sub = str }) : spn_arg_lit(str);
      return SPN_PATH_OK;
    }
  }
  SP_UNREACHABLE_RETURN(SPN_PATH_MALFORMED);
}

spn_path_check_t spn_toolchain_program(spn_toolchain_source_t source, spn_path_root_t base, sp_str_t program, spn_arg_t* arg) {
  if (!spn_path_normal(program)) {
    return SPN_PATH_MALFORMED;
  }
  switch (source) {
    case SPN_TOOLCHAIN_SOURCE_LOCAL: {
      if (sp_fs_get_name(program).len == program.len) {
        *arg = spn_arg_lit(program);
        return SPN_PATH_OK;
      }
      return local_arg(base, program, arg);
    }
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION:
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      if (sp_fs_is_absolute(program)) {
        return SPN_PATH_ABSOLUTE;
      }
      *arg = spn_arg_lit(program);
      return SPN_PATH_OK;
    }
  }
  SP_UNREACHABLE_RETURN(SPN_PATH_MALFORMED);
}

spn_wasi_spelling_t spn_toolchain_wasi_spelling(const spn_path_roots_t* roots, sp_mem_t mem, const spn_toolchain_info_t* toolchain) {
  sp_da_for(toolchain->rows, it) {
    if (toolchain->rows[it].triple.os == SPN_OS_WASI) {
      return spn_sdk_wasi_spelling(roots, mem, &toolchain->rows[it].sdk);
    }
  }
  return SPN_WASI_SPELLING_WASI;
}

bool spn_toolchain_has_cxx(spn_toolchain_info_t* toolchain) {
  return !spn_arg_empty(toolchain->cxx.program);
}

spn_toolchain_ref_t spn_toolchain_ref_from_str(sp_str_t str) {
  if (sp_str_empty(str)) {
    return (spn_toolchain_ref_t) { .kind = SPN_TOOLCHAIN_REF_NONE };
  }
  if (sp_str_equal_cstr(str, "auto")) {
    return (spn_toolchain_ref_t) { .kind = SPN_TOOLCHAIN_REF_AUTO };
  }
  return (spn_toolchain_ref_t) { .kind = SPN_TOOLCHAIN_REF_NAMED, .name = str };
}

spn_cc_cap_set_t spn_toolchain_driver_caps(spn_cc_driver_t driver) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_BARE | SPN_CC_CAP_GNU_RUNTIME;
    case SPN_CC_DRIVER_CLANG: return SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_LLVM_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_GNU_RUNTIME;
    case SPN_CC_DRIVER_ZIG: return SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_CODEVIEW | SPN_CC_CAP_LIBC_FILE | SPN_CC_CAP_DEFAULT_UBSAN | SPN_CC_CAP_BARE;
    case SPN_CC_DRIVER_MSVC: return 0;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(0);
}

bool spn_toolchain_driver_retargets(spn_cc_driver_t driver) {
  return spn_toolchain_driver_caps(driver) & SPN_CC_CAP_TARGET_TRIPLE;
}

static spn_sanitizer_set_t gcc_stock_sanitizers(spn_triple_t host) {
  switch (host.os) {
    case SPN_OS_LINUX: return host.abi == SPN_ABI_MUSL ? 0 : SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK;
    case SPN_OS_MACOS: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED;
    case SPN_OS_WINDOWS:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: return 0;
  }
  SP_UNREACHABLE_RETURN(0);
}

static spn_sanitizer_set_t clang_stock_sanitizers(spn_triple_t host) {
  switch (host.os) {
    case SPN_OS_LINUX: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_MEMORY | SPN_SANITIZER_LEAK;
    case SPN_OS_MACOS: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK;
    case SPN_OS_WINDOWS:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: return 0;
  }
  SP_UNREACHABLE_RETURN(0);
}

spn_sanitizer_set_t spn_toolchain_stock_sanitizers(spn_cc_driver_t driver, spn_triple_t host) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return gcc_stock_sanitizers(host);
    case SPN_CC_DRIVER_CLANG: return clang_stock_sanitizers(host);
    case SPN_CC_DRIVER_ZIG:
    case SPN_CC_DRIVER_MSVC: return 0;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(0);
}

spn_abi_t spn_default_abi(spn_cc_driver_t driver, spn_os_t os) {
  switch (os) {
    case SPN_OS_LINUX:
    case SPN_OS_FREESTANDING: return SPN_ABI_NONE;
    case SPN_OS_MACOS: return SPN_ABI_APPLE;
    case SPN_OS_WASI: return SPN_ABI_MUSL;
    case SPN_OS_WINDOWS: {
      switch (driver) {
        case SPN_CC_DRIVER_GCC:
        case SPN_CC_DRIVER_ZIG: return SPN_ABI_GNU;
        case SPN_CC_DRIVER_MSVC: return SPN_ABI_MSVC;
        case SPN_CC_DRIVER_CLANG: return SPN_ABI_NONE;
        case SPN_CC_DRIVER_NONE: sp_unreachable_case();
      }
      sp_unreachable_return(SPN_ABI_NONE);
    }
    case SPN_OS_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_ABI_NONE);
}

bool spn_toolchain_driver_composes(spn_cc_driver_t driver, spn_ld_dialect_t dialect) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return dialect == SPN_LD_DIALECT_GNU || dialect == SPN_LD_DIALECT_DARWIN;
    case SPN_CC_DRIVER_MSVC: return dialect == SPN_LD_DIALECT_LINK;
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: return true;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(false);
}
