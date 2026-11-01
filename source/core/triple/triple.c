#include "triple/triple.h"

spn_arch_t spn_arch_from_str(sp_str_t str) {
  if (sp_str_equal_cstr(str, "x86_64")) {
    return SPN_ARCH_X64;
  }
  if (sp_str_equal_cstr(str, "aarch64")) {
    return SPN_ARCH_ARM64;
  }
  if (sp_str_equal_cstr(str, "wasm32")) {
    return SPN_ARCH_WASM32;
  }
  return SPN_ARCH_NONE;
}

sp_str_t spn_arch_to_str(spn_arch_t arch) {
  switch (arch) {
    case SPN_ARCH_X64:    return sp_str_lit("x86_64");
    case SPN_ARCH_ARM64:  return sp_str_lit("aarch64");
    case SPN_ARCH_WASM32: return sp_str_lit("wasm32");
    case SPN_ARCH_NONE:   return sp_str_lit("");
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

spn_os_t spn_os_from_str(sp_str_t str) {
  if (sp_str_equal_cstr(str, "windows")) {
    return SPN_OS_WINDOWS;
  }
  if (sp_str_equal_cstr(str, "linux")) {
    return SPN_OS_LINUX;
  }
  if (sp_str_equal_cstr(str, "macos")) {
    return SPN_OS_MACOS;
  }
  if (sp_str_equal_cstr(str, "wasi")) {
    return SPN_OS_WASI;
  }
  if (sp_str_equal_cstr(str, "freestanding")) {
    return SPN_OS_FREESTANDING;
  }
  return SPN_OS_NONE;
}

sp_str_t spn_os_to_str(spn_os_t os) {
  switch (os) {
    case SPN_OS_LINUX:        return sp_str_lit("linux");
    case SPN_OS_WINDOWS:      return sp_str_lit("windows");
    case SPN_OS_MACOS:        return sp_str_lit("macos");
    case SPN_OS_WASI:         return sp_str_lit("wasi");
    case SPN_OS_FREESTANDING: return sp_str_lit("freestanding");
    case SPN_OS_NONE:         return sp_str_lit("");
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

spn_abi_t spn_abi_from_str(sp_str_t str) {
  if (sp_str_equal_cstr(str, "gnu")) {
    return SPN_ABI_GNU;
  }
  if (sp_str_equal_cstr(str, "musl")) {
    return SPN_ABI_MUSL;
  }
  if (sp_str_equal_cstr(str, "msvc")) {
    return SPN_ABI_MSVC;
  }
  if (sp_str_equal_cstr(str, "apple")) {
    return SPN_ABI_APPLE;
  }
  if (sp_str_equal_cstr(str, "none")) {
    return SPN_ABI_BARE;
  }
  if (sp_str_equal_cstr(str, "elf")) {
    return SPN_ABI_ELF;
  }
  return SPN_ABI_NONE;
}

sp_str_t spn_abi_to_str(spn_abi_t abi) {
  switch (abi) {
    case SPN_ABI_GNU:   return sp_str_lit("gnu");
    case SPN_ABI_MUSL:  return sp_str_lit("musl");
    case SPN_ABI_MSVC:  return sp_str_lit("msvc");
    case SPN_ABI_APPLE: return sp_str_lit("apple");
    case SPN_ABI_BARE:  return sp_str_lit("none");
    case SPN_ABI_ELF:   return sp_str_lit("elf");
    case SPN_ABI_NONE:  return sp_str_lit("");
    case SPN_ABI_COUNT: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

typedef struct {
  const spn_abi_t* abis;
  u32 count;
  u32 completions;
} os_abis_t;

static os_abis_t os_abis(spn_os_t os) {
  static const spn_abi_t linux_abis [] = { SPN_ABI_GNU, SPN_ABI_MUSL, SPN_ABI_BARE };
  static const spn_abi_t windows_abis [] = { SPN_ABI_GNU, SPN_ABI_MSVC };
  static const spn_abi_t macos_abis [] = { SPN_ABI_APPLE };
  static const spn_abi_t wasi_abis [] = { SPN_ABI_MUSL };
  static const spn_abi_t freestanding_abis [] = { SPN_ABI_BARE, SPN_ABI_ELF };

  switch (os) {
    case SPN_OS_LINUX: {
      return (os_abis_t) { .abis = linux_abis, .count = sp_carr_len(linux_abis), .completions = 2 };
    }
    case SPN_OS_WINDOWS: {
      return (os_abis_t) { .abis = windows_abis, .count = sp_carr_len(windows_abis), .completions = sp_carr_len(windows_abis) };
    }
    case SPN_OS_MACOS: {
      return (os_abis_t) { .abis = macos_abis, .count = sp_carr_len(macos_abis), .completions = sp_carr_len(macos_abis) };
    }
    case SPN_OS_WASI: {
      return (os_abis_t) { .abis = wasi_abis, .count = sp_carr_len(wasi_abis), .completions = sp_carr_len(wasi_abis) };
    }
    case SPN_OS_FREESTANDING: {
      return (os_abis_t) { .abis = freestanding_abis, .count = sp_carr_len(freestanding_abis), .completions = sp_carr_len(freestanding_abis) };
    }
    case SPN_OS_NONE: {
      return sp_zero_s(os_abis_t);
    }
  }
  sp_unreachable_return(sp_zero_s(os_abis_t));
}

u32 spn_os_abis(spn_os_t os, const spn_abi_t** abis) {
  os_abis_t row = os_abis(os);
  *abis = row.abis;
  return row.count;
}

u32 spn_os_completions(spn_os_t os, const spn_abi_t** abis) {
  os_abis_t row = os_abis(os);
  *abis = row.abis;
  return row.completions;
}

u32 spn_os_archs(spn_os_t os, const spn_arch_t** archs) {
  static const spn_arch_t machine_archs [] = { SPN_ARCH_X64, SPN_ARCH_ARM64 };
  static const spn_arch_t wasi_archs [] = { SPN_ARCH_WASM32 };

  switch (os) {
    case SPN_OS_LINUX:
    case SPN_OS_WINDOWS:
    case SPN_OS_MACOS:
    case SPN_OS_FREESTANDING: {
      *archs = machine_archs;
      return sp_carr_len(machine_archs);
    }
    case SPN_OS_WASI: {
      *archs = wasi_archs;
      return sp_carr_len(wasi_archs);
    }
    case SPN_OS_NONE: {
      *archs = SP_NULLPTR;
      return 0;
    }
  }
  SP_UNREACHABLE_RETURN(0);
}

static bool os_has_arch(spn_os_t os, spn_arch_t arch) {
  const spn_arch_t* archs = SP_NULLPTR;
  u32 count = spn_os_archs(os, &archs);
  sp_for(it, count) {
    if (archs[it] == arch) {
      return true;
    }
  }
  return false;
}

static bool os_has_abi(spn_os_t os, spn_abi_t abi) {
  os_abis_t row = os_abis(os);
  sp_for(it, row.count) {
    if (row.abis[it] == abi) {
      return true;
    }
  }
  return false;
}

spn_obj_format_t spn_os_to_native_object_format(spn_os_t os) {
  switch (os) {
    case SPN_OS_LINUX:
    case SPN_OS_FREESTANDING: return SPN_OBJ_ELF;
    case SPN_OS_WINDOWS: return SPN_OBJ_COFF;
    case SPN_OS_MACOS: return SPN_OBJ_MACHO;
    case SPN_OS_WASI: return SPN_OBJ_WASM;
    case SPN_OS_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_OBJ_ELF);
}

spn_err_t spn_triple_parse(sp_str_t str, spn_triple_t* triple) {
  *triple = sp_zero_s(spn_triple_t);

  sp_str_t segments [3] = sp_zero;
  u32 num_segments = 0;
  sp_str_t remaining = str;
  while (true) {
    s32 sep = sp_str_find_c8(remaining, '-');
    sp_str_t segment = sep < 0 ? remaining : sp_str_prefix(remaining, sep);
    if (sp_str_empty(segment) || num_segments == sp_carr_len(segments)) {
      return SPN_ERR_TRIPLE_INVALID;
    }
    segments[num_segments++] = segment;
    if (sep < 0) {
      break;
    }
    remaining = sp_str_suffix(remaining, remaining.len - sep - 1);
  }

  triple->arch = spn_arch_from_str(segments[0]);
  if (!triple->arch) {
    return SPN_ERR_TRIPLE_INVALID;
  }
  if (num_segments > 1) {
    triple->os = spn_os_from_str(segments[1]);
    if (!triple->os) {
      return SPN_ERR_TRIPLE_INVALID;
    }
  }
  if (num_segments > 2) {
    triple->abi = spn_abi_from_str(segments[2]);
    if (!triple->abi) {
      return SPN_ERR_TRIPLE_INVALID;
    }
  }
  return SPN_OK;
}

sp_str_t spn_triple_to_str(sp_mem_t mem, spn_triple_t triple) {
  sp_str_t arch = spn_arch_to_str(triple.arch);
  sp_str_t os = spn_os_to_str(triple.os);
  sp_str_t abi = spn_abi_to_str(triple.abi);

  if (triple.abi) {
    return sp_fmt(mem, "{}-{}-{}", sp_fmt_str(arch), sp_fmt_str(os), sp_fmt_str(abi)).value;
  }
  if (triple.os) {
    return sp_fmt(mem, "{}-{}", sp_fmt_str(arch), sp_fmt_str(os)).value;
  }
  return arch;
}

bool spn_triple_equal(spn_triple_t a, spn_triple_t b) {
  return a.arch == b.arch && a.os == b.os && a.abi == b.abi;
}

spn_triple_t spn_triple_merge(spn_triple_t base, spn_triple_t partial) {
  return (spn_triple_t) {
    .arch = partial.arch ? partial.arch : base.arch,
    .os   = partial.os   ? partial.os   : base.os,
    .abi  = partial.abi  ? partial.abi  : base.abi,
  };
}

spn_triple_entry_t spn_triple_entry(spn_triple_t partial, spn_triple_t* full) {
  *full = sp_zero_s(spn_triple_t);
  if (!partial.arch) {
    return SPN_TRIPLE_ENTRY_MISSING_ARCH;
  }
  if (!partial.os) {
    return SPN_TRIPLE_ENTRY_MISSING_OS;
  }
  if (!os_has_arch(partial.os, partial.arch)) {
    return SPN_TRIPLE_ENTRY_FOREIGN_ARCH;
  }

  if (partial.abi) {
    if (!os_has_abi(partial.os, partial.abi)) {
      return SPN_TRIPLE_ENTRY_FOREIGN_ABI;
    }
    *full = partial;
    return SPN_TRIPLE_ENTRY_OK;
  }

  const spn_abi_t* abis = SP_NULLPTR;
  u32 count = spn_os_completions(partial.os, &abis);
  if (count != 1) {
    return SPN_TRIPLE_ENTRY_MISSING_ABI;
  }
  *full = (spn_triple_t) { partial.arch, partial.os, abis[0] };
  return SPN_TRIPLE_ENTRY_OK;
}
