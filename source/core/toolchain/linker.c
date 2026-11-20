#include "toolchain/linker.h"

#include "macro/macro.h"
#include "spn/core.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

typedef struct {
  const spn_linking_t* items;
  u32 count;
} rows_t;

bool spn_ld_dynamic(spn_triple_t target) {
  if (target.abi == SPN_ABI_BARE) {
    return false;
  }
  switch (target.os) {
    case SPN_OS_LINUX:
    case SPN_OS_WINDOWS:
    case SPN_OS_MACOS: {
      return true;
    }
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: {
      return false;
    }
  }
  SP_UNREACHABLE_RETURN(false);
}

bool spn_ld_loader(spn_triple_t target, spn_linking_t linking) {
  return spn_ld_dynamic(target) && !(spn_triple_object_format(target) == SPN_OBJ_ELF && linking.libc == SPN_RUNTIME_STATIC);
}

spn_linking_refusal_t si_ld_linkings(spn_triple_t target, spn_linking_t request, spn_linkage_t demand, spn_linkings_t* linkings) {
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
    { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },
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

  sp_assert(table.count <= si_linkings_max);

  spn_linkings_t preferred = sp_zero;
  spn_linkings_t unloadable = sp_zero;
  bool static_libc = false;
  sp_for(it, table.count) {
    spn_linking_t entry = table.items[it];
    static_libc |= entry.libc == SPN_RUNTIME_STATIC;
    if (request.runtime && entry.runtime != request.runtime) {
      continue;
    }
    if (request.libc && entry.libc != request.libc) {
      continue;
    }
    bool loader = spn_ld_loader(target, entry);
    entry.linkage = request.linkage;
    if (!entry.linkage) {
      entry.linkage = loader ? SPN_LIB_KIND_SHARED : SPN_LIB_KIND_STATIC;
    }
    spn_linkings_t* group = demand == SPN_LIB_KIND_SHARED && !loader ? &unloadable : &preferred;
    group->items[group->count++] = entry;
  }

  if (!preferred.count && !unloadable.count) {
    if (!spn_ld_dynamic(target)) {
      return SPN_LINKING_REFUSAL_NO_LOADER;
    }
    if (request.libc == SPN_RUNTIME_STATIC && !static_libc) {
      return SPN_LINKING_REFUSAL_OS_LIBC;
    }
    if (request.libc == SPN_RUNTIME_STATIC && request.runtime == SPN_RUNTIME_SHARED) {
      return SPN_LINKING_REFUSAL_SHARED_RUNTIME;
    }
    return SPN_LINKING_REFUSAL_HYBRID_CRT;
  }

  *linkings = preferred;
  sp_for(it, unloadable.count) {
    linkings->items[linkings->count++] = unloadable.items[it];
  }
  return SPN_LINKING_REFUSAL_NONE;
}

spn_ld_dialect_t si_ld_dialect(spn_cc_driver_t driver, spn_triple_t target) {
  switch (spn_triple_object_format(target)) {
    case SPN_OBJ_ELF: return SPN_LD_DIALECT_GNU;
    case SPN_OBJ_MACHO: return SPN_LD_DIALECT_DARWIN;
    case SPN_OBJ_WASM: return SPN_LD_DIALECT_WASM;
    case SPN_OBJ_COFF: {
      switch (driver) {
        case SPN_CC_DRIVER_ZIG: return SPN_LD_DIALECT_GNU;
        case SPN_CC_DRIVER_GCC:
        case SPN_CC_DRIVER_CLANG:
        case SPN_CC_DRIVER_MSVC: return target.abi == SPN_ABI_MSVC ? SPN_LD_DIALECT_LINK : SPN_LD_DIALECT_GNU;
        case SPN_CC_DRIVER_NONE: sp_unreachable_case();
      }
    }
  }
  SP_UNREACHABLE_RETURN(SPN_LD_DIALECT_GNU);
}

bool spn_ld_scripts(spn_ld_family_t family, spn_obj_format_t format) {
  switch (family) {
    case SPN_LD_FAMILY_GNU: return format == SPN_OBJ_ELF || format == SPN_OBJ_COFF;
    case SPN_LD_FAMILY_LLD: return format == SPN_OBJ_ELF;
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
    case SPN_LD_FAMILY_LLD: return si_toolchain_driver_caps(driver) & SPN_CC_CAP_FUSE_LD;
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
    case SPN_CC_DRIVER_MSVC: return native(si_ld_dialect(driver, target));
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_LD_FAMILY_NONE);
}
