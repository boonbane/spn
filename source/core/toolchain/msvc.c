#include "toolchain/msvc.h"

#define version_max_parts 4
#define toolset_major 14

typedef struct {
  u32 parts [version_max_parts];
  u32 count;
} version_t;

typedef struct {
  const c8* name;
  const c8* year;
  u32 first;
  u32 last;
} generation_t;

typedef enum {
  PIN_ANY,
  PIN_GENERATION,
  PIN_PREFIX,
  PIN_INVALID,
} pin_kind_t;

typedef struct {
  pin_kind_t kind;
  const generation_t* generation;
  version_t prefix;
} pin_t;

static const generation_t generations [] = {
  { "v141", "2017", 10, 16 },
  { "v142", "2019", 20, 29 },
  { "v143", "2022", 30, 44 },
  { "v145", "2026", 50, SP_LIMIT_U32_MAX },
};

sp_msvc_arch_t spn_msvc_arch(spn_arch_t arch) {
  switch (arch) {
    case SPN_ARCH_X64: return SP_MSVC_ARCH_X64;
    case SPN_ARCH_ARM64: return SP_MSVC_ARCH_ARM64;
    case SPN_ARCH_WASM32:
    case SPN_ARCH_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SP_MSVC_ARCH_X64);
}

static bool parse_version(sp_str_t str, version_t* version) {
  *version = sp_zero_s(version_t);
  bool digits = false;
  sp_for(it, str.len) {
    c8 c = str.data[it];
    if (c >= '0' && c <= '9') {
      version->parts[version->count] = version->parts[version->count] * 10 + (u32)(c - '0');
      digits = true;
    }
    else if (c == '.' && digits && version->count + 1 < version_max_parts) {
      version->count++;
      digits = false;
    }
    else {
      return false;
    }
  }
  version->count++;
  return digits;
}

static version_t version_of(sp_str_t str) {
  version_t version = sp_zero;
  parse_version(str, &version);
  return version;
}

static bool newer(version_t a, version_t b) {
  sp_for(it, version_max_parts) {
    if (a.parts[it] != b.parts[it]) {
      return a.parts[it] > b.parts[it];
    }
  }
  return false;
}

static pin_t parse_sdk(sp_str_t str) {
  if (sp_str_empty(str)) {
    return (pin_t) { .kind = PIN_ANY };
  }
  pin_t pin = { .kind = PIN_PREFIX };
  if (!parse_version(str, &pin.prefix)) {
    return (pin_t) { .kind = PIN_INVALID };
  }
  return pin;
}

static pin_t parse_toolset(sp_str_t str) {
  sp_carr_for(generations, it) {
    if (sp_str_equal_cstr(str, generations[it].name) || sp_str_equal_cstr(str, generations[it].year)) {
      return (pin_t) { .kind = PIN_GENERATION, .generation = &generations[it] };
    }
  }
  pin_t pin = parse_sdk(str);
  if (pin.kind == PIN_PREFIX && pin.prefix.parts[0] != toolset_major) {
    return (pin_t) { .kind = PIN_INVALID };
  }
  return pin;
}

static bool pinned(pin_t pin, version_t version) {
  switch (pin.kind) {
    case PIN_ANY: {
      return true;
    }
    case PIN_GENERATION: {
      return version.parts[0] == toolset_major && version.parts[1] >= pin.generation->first && version.parts[1] <= pin.generation->last;
    }
    case PIN_PREFIX: {
      sp_for(it, pin.prefix.count) {
        if (it >= version.count || version.parts[it] != pin.prefix.parts[it]) {
          return false;
        }
      }
      return true;
    }
    case PIN_INVALID: {
      return false;
    }
  }
  sp_unreachable_return(false);
}

static bool preferable(pin_t pin, const spn_msvc_toolset_t* candidate, const spn_msvc_toolset_t* best) {
  if (pin.kind == PIN_ANY) {
    return candidate->preferred && !best->preferred;
  }
  return newer(version_of(sp_msvc_version_str(&candidate->vs.version.tools)), version_of(sp_msvc_version_str(&best->vs.version.tools)));
}

bool spn_msvc_toolset_valid(sp_str_t toolset) {
  return parse_toolset(toolset).kind != PIN_INVALID;
}

bool spn_msvc_sdk_valid(sp_str_t sdk) {
  return parse_sdk(sdk).kind != PIN_INVALID;
}

spn_msvc_match_t spn_msvc_match(const spn_msvc_installs_t* installs, spn_arch_t arch, spn_msvc_request_t request, bool compiler, spn_msvc_pick_t* pick) {
  *pick = sp_zero_s(spn_msvc_pick_t);
  sp_msvc_arch_t target = spn_msvc_arch(arch);
  pin_t toolset = parse_toolset(request.toolset);
  pin_t sdk = parse_sdk(request.sdk);

  bool any_toolset = false;
  sp_da_for(installs->toolsets, it) {
    const spn_msvc_toolset_t* candidate = &installs->toolsets[it];
    if (candidate->vs.target != target || (compiler && !candidate->compiler)) {
      continue;
    }
    any_toolset = true;
    if (!pinned(toolset, version_of(sp_msvc_version_str(&candidate->vs.version.tools)))) {
      continue;
    }
    if (!pick->toolset || preferable(toolset, candidate, pick->toolset)) {
      pick->toolset = candidate;
    }
  }

  bool any_kit = false;
  sp_da_for(installs->kits, it) {
    const sp_msvc_sdk_t* candidate = &installs->kits[it];
    if (candidate->arch != target) {
      continue;
    }
    any_kit = true;
    version_t version = version_of(sp_msvc_version_str(&candidate->version));
    if (!pinned(sdk, version)) {
      continue;
    }
    if (!pick->kit || newer(version, version_of(sp_msvc_version_str(&pick->kit->version)))) {
      pick->kit = candidate;
    }
  }

  if (!any_toolset || !any_kit) {
    return SPN_MSVC_MATCH_ABSENT;
  }
  if (!pick->toolset) {
    return SPN_MSVC_MATCH_TOOLSET;
  }
  if (!pick->kit) {
    return SPN_MSVC_MATCH_SDK;
  }
  return SPN_MSVC_MATCH_OK;
}
