#include "toolchain/linker.h"

#include "macro/macro.h"
#include "spn/core.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

typedef struct {
  const spn_linking_t* items;
  u32 count;
} rows_t;

bool spn_ld_loader(spn_triple_t target, spn_linking_t linking) {
  return spn_triple_dynamic(target) && !(spn_os_format(target.os) == SPN_FORMAT_ELF && linking.libc == SPN_RUNTIME_STATIC);
}

static u32 first(spn_triple_t target, rows_t table, spn_linking_t request) {
  sp_for(it, table.count) {
    spn_linking_t row = table.items[it];
    if (request.runtime && row.runtime != request.runtime) {
      continue;
    }
    if (request.libc && row.libc != request.libc) {
      continue;
    }
    if (request.linkage == SPN_LIB_KIND_SHARED && !spn_ld_loader(target, row)) {
      continue;
    }
    return it;
  }
  return table.count;
}

spn_linking_refusal_t spn_ld_linking(spn_triple_t target, spn_linking_t request, spn_linking_t* linking) {
  static const spn_linking_t linux_gnu [] = {
    { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_SHARED },
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },
  };
  static const spn_linking_t linux_musl [] = {
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_STATIC },
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },
    { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_SHARED },
  };
  static const spn_linking_t windows_msvc [] = {
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_STATIC },
    { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_SHARED },
  };
  static const spn_linking_t windows_gnu [] = {
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },
    { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_SHARED },
  };
  static const spn_linking_t macos [] = {
    { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_SHARED },
  };
  static const spn_linking_t loaderless [] = {
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_STATIC },
  };

  rows_t table = sp_zero;
  switch (target.os) {
    case SPN_OS_LINUX: {
      switch (target.abi) {
        case SPN_ABI_GNU: table = (rows_t) { linux_gnu, sp_carr_len(linux_gnu) }; break;
        case SPN_ABI_MUSL: table = (rows_t) { linux_musl, sp_carr_len(linux_musl) }; break;
        case SPN_ABI_BARE: table = (rows_t) { loaderless, sp_carr_len(loaderless) }; break;
        case SPN_ABI_MSVC:
        case SPN_ABI_APPLE:
        case SPN_ABI_ELF:
        case SPN_ABI_NONE:
        case SPN_ABI_COUNT: sp_unreachable_case();
      }
      break;
    }
    case SPN_OS_WINDOWS: {
      switch (target.abi) {
        case SPN_ABI_MSVC: table = (rows_t) { windows_msvc, sp_carr_len(windows_msvc) }; break;
        case SPN_ABI_GNU: table = (rows_t) { windows_gnu, sp_carr_len(windows_gnu) }; break;
        case SPN_ABI_MUSL:
        case SPN_ABI_APPLE:
        case SPN_ABI_BARE:
        case SPN_ABI_ELF:
        case SPN_ABI_NONE:
        case SPN_ABI_COUNT: sp_unreachable_case();
      }
      break;
    }
    case SPN_OS_MACOS: table = (rows_t) { macos, sp_carr_len(macos) }; break;
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING: table = (rows_t) { loaderless, sp_carr_len(loaderless) }; break;
    case SPN_OS_NONE: sp_unreachable_case();
  }

  u32 index = first(target, table, request);
  if (index == table.count) {
    if (first(target, table, (spn_linking_t) { .linkage = SPN_LIB_KIND_SHARED }) == table.count) {
      return SPN_LINKING_REFUSAL_NO_LOADER;
    }
    if (request.libc == SPN_RUNTIME_STATIC && first(target, table, (spn_linking_t) { .libc = SPN_RUNTIME_STATIC }) == table.count) {
      return SPN_LINKING_REFUSAL_OS_LIBC;
    }
    if (request.runtime == SPN_RUNTIME_STATIC && first(target, table, (spn_linking_t) { .runtime = SPN_RUNTIME_STATIC }) == table.count) {
      return SPN_LINKING_REFUSAL_OS_RUNTIME;
    }
    if (request.libc == SPN_RUNTIME_STATIC && request.runtime == SPN_RUNTIME_SHARED) {
      return SPN_LINKING_REFUSAL_SHARED_RUNTIME;
    }
    if (request.runtime && request.libc && request.runtime != request.libc) {
      return SPN_LINKING_REFUSAL_HYBRID_CRT;
    }
    return SPN_LINKING_REFUSAL_SHARED_DEPS;
  }
  spn_linking_t resolved = table.items[index];
  resolved.linkage = request.linkage;
  if (!resolved.linkage) {
    resolved.linkage = spn_ld_loader(target, resolved) ? SPN_LIB_KIND_SHARED : SPN_LIB_KIND_STATIC;
  }
  *linking = resolved;
  return SPN_LINKING_REFUSAL_NONE;
}

spn_ld_dialect_t spn_ld_dialect(spn_triple_t target) {
  switch (target.os) {
    case SPN_OS_LINUX:
    case SPN_OS_FREESTANDING: return SPN_LD_DIALECT_GNU;
    case SPN_OS_MACOS: return SPN_LD_DIALECT_DARWIN;
    case SPN_OS_WASI: return SPN_LD_DIALECT_WASM;
    case SPN_OS_WINDOWS: {
      if (target.abi == SPN_ABI_MSVC) {
        return SPN_LD_DIALECT_LINK;
      } else {
        return SPN_LD_DIALECT_GNU;
      }
    }
    case SPN_OS_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_LD_DIALECT_GNU);
}

bool spn_ld_scripts(spn_ld_family_t family, spn_format_t format) {
  switch (family) {
    case SPN_LD_FAMILY_GNU: return format == SPN_FORMAT_ELF || format == SPN_FORMAT_COFF;
    case SPN_LD_FAMILY_LLD: return format == SPN_FORMAT_ELF;
    case SPN_LD_FAMILY_LD64:
    case SPN_LD_FAMILY_MSVC: return false;
    case SPN_LD_FAMILY_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(false);
}

static spn_ld_family_t native(spn_ld_dialect_t dialect) {
  switch (dialect) {
    case SPN_LD_DIALECT_GNU: return SPN_LD_FAMILY_GNU;
    case SPN_LD_DIALECT_LINK: return SPN_LD_FAMILY_MSVC;
    case SPN_LD_DIALECT_DARWIN: return SPN_LD_FAMILY_LD64;
    case SPN_LD_DIALECT_WASM: return SPN_LD_FAMILY_LLD;
    case SPN_LD_DIALECT_COUNT: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_LD_FAMILY_NONE);
}

bool spn_ld_accepts(spn_cc_driver_t driver, spn_ld_family_t declared) {
  switch (declared) {
    case SPN_LD_FAMILY_NONE: return true;
    case SPN_LD_FAMILY_LLD: return spn_toolchain_driver_caps(driver) & SPN_CC_CAP_FUSE_LD;
    case SPN_LD_FAMILY_GNU:
    case SPN_LD_FAMILY_LD64:
    case SPN_LD_FAMILY_MSVC: return false;
  }
  SP_UNREACHABLE_RETURN(false);
}

spn_ld_family_t spn_ld_native(spn_cc_driver_t driver, spn_triple_t target) {
  switch (driver) {
    case SPN_CC_DRIVER_ZIG: return SPN_LD_FAMILY_LLD;
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_MSVC: return native(spn_ld_dialect(target));
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_LD_FAMILY_NONE);
}
