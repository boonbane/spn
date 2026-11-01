#include "toolchain/catalog.h"

#include "paths/paths.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

static void push_row(spn_toolchain_info_t* info, spn_toolchain_row_t row) {
  if (!spn_toolchain_row(info, row.triple)) {
    sp_da_push(info->rows, row);
  }
}

static spn_path_t sdk_root(spn_toolchain_catalog_t* catalog, spn_toolchain_support_t support, spn_arg_t sdk) {
  switch (support.kind) {
    case SPN_TOOLCHAIN_SUPPORT_ARTIFACT: return spn_path_empty(sdk.path) ? spn_path_join(catalog->mem, spn_toolchain_artifact_root(catalog->mem, support.artifact), sdk.prefix) : sdk.path;
    case SPN_TOOLCHAIN_SUPPORT_DETECTED:
    case SPN_TOOLCHAIN_SUPPORT_LOCAL:
    case SPN_TOOLCHAIN_SUPPORT_NONE: return sdk.path;
  }
  sp_unreachable_return(sdk.path);
}

static void bind_target(spn_toolchain_catalog_t* catalog, spn_toolchain_info_t* info, spn_toolchain_support_t support, spn_toolchain_target_t target) {
  spn_toolchain_row_t row = { .triple = target.triple, .sanitizers = target.sanitizers };
  if (!spn_arg_empty(target.sdk)) {
    row.sdk = spn_sdk_at(catalog->mem, target.triple, sdk_root(catalog, support, target.sdk));
    sp_da_push(info->rows, row);
    return;
  }
  if (spn_sdk_default(&catalog->sdks, target.triple, &row.sdk)) {
    sp_da_push(info->rows, row);
    return;
  }
  sp_da_push(info->unserved, target.triple);
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

static spn_sanitizer_set_t stock_sanitizers(spn_cc_driver_t driver, spn_triple_t host) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return gcc_stock_sanitizers(host);
    case SPN_CC_DRIVER_CLANG: return clang_stock_sanitizers(host);
    case SPN_CC_DRIVER_ZIG:
    case SPN_CC_DRIVER_MSVC: return 0;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(0);
}

static spn_toolchain_target_t stock(spn_cc_driver_t driver, spn_triple_t triple) {
  return (spn_toolchain_target_t) {
    .triple = triple,
    .sanitizers = stock_sanitizers(driver, triple),
  };
}

static void push_stock(spn_toolchain_catalog_t* catalog, spn_toolchain_info_t* info, const spn_toolchain_decl_t* decl) {
  spn_triple_t host = catalog->host;
  host.abi = host.abi ? host.abi : spn_default_abi(decl->driver, host.os);
  if (!host.abi || !spn_toolchain_driver_composes(decl->driver, host) || spn_toolchain_row(info, host) || spn_toolchain_unserved(info, host)) {
    return;
  }
  bind_target(catalog, info, sp_zero_struct(spn_toolchain_support_t), stock(decl->driver, host));
}

static void push_hosted(spn_toolchain_info_t* info, spn_toolchain_catalog_t* catalog, spn_cc_driver_t driver) {
  static const spn_os_t hosted [] = { SPN_OS_LINUX, SPN_OS_MACOS, SPN_OS_WINDOWS };
  sp_carr_for(hosted, os) {
    const spn_arch_t* arches = SP_NULLPTR;
    const spn_abi_t* abis = SP_NULLPTR;
    u32 num_arches = spn_os_archs(hosted[os], &arches);
    u32 num_abis = spn_os_completions(hosted[os], &abis);
    bool own = hosted[os] == catalog->host.os;
    sp_for(arch, num_arches) {
      sp_for(abi, num_abis) {
        spn_toolchain_row_t row = { .triple = { arches[arch], hosted[os], abis[abi] } };
        if (own) {
          row.sanitizers = stock_sanitizers(driver, row.triple);
        }
        if (spn_sdk_served(&catalog->sdks, catalog->host, row.triple, &row.sdk)) {
          push_row(info, row);
        }
      }
    }
  }
}

static void push_bare(spn_toolchain_info_t* info, spn_cc_driver_t driver, spn_triple_t host) {
  if (!(spn_toolchain_driver_caps(driver) & SPN_CC_CAP_BARE) || spn_os_to_native_object_format(host.os) != SPN_OBJ_ELF) {
    return;
  }
  push_row(info, (spn_toolchain_row_t) { .triple = { host.arch, SPN_OS_FREESTANDING, SPN_ABI_BARE } });
  push_row(info, (spn_toolchain_row_t) { .triple = { host.arch, SPN_OS_LINUX, SPN_ABI_BARE } });
}

static void bind_rows(spn_toolchain_catalog_t* catalog, const spn_toolchain_decl_t* decl, spn_toolchain_support_t support, spn_toolchain_info_t* info) {
  info->rows = sp_da_new(catalog->mem, spn_toolchain_row_t);
  info->unserved = sp_da_new(catalog->mem, spn_triple_t);
  sp_da_for(decl->targets, it) {
    bind_target(catalog, info, support, decl->targets[it]);
  }
  if (!decl->host_row) {
    return;
  }

  push_stock(catalog, info, decl);
  push_bare(info, decl->driver, catalog->host);
  if (spn_toolchain_driver_retargets(decl->driver)) {
    push_hosted(info, catalog, decl->driver);
  }
}

static const spn_toolchain_host_t* find_host(sp_da(spn_toolchain_host_t) hosts, spn_triple_t host) {
  sp_da_for(hosts, it) {
    spn_triple_t entry = hosts[it].triple;
    if (entry.arch == host.arch && entry.os == host.os && (!entry.abi || entry.abi == host.abi)) {
      return &hosts[it];
    }
  }
  return SP_NULLPTR;
}

static spn_toolchain_support_t bind_support(spn_toolchain_catalog_t* catalog, const spn_toolchain_decl_t* decl) {
  switch (decl->source) {
    case SPN_TOOLCHAIN_SOURCE_LOCAL: {
      if (sp_da_empty(decl->hosts) || find_host(decl->hosts, catalog->host)) {
        return (spn_toolchain_support_t) {
          .kind = SPN_TOOLCHAIN_SUPPORT_LOCAL
        };
      }
      return (spn_toolchain_support_t) {
        .kind = SPN_TOOLCHAIN_SUPPORT_NONE,
        .err = SPN_ERR_TOOLCHAIN_UNAVAILABLE_FOR_HOST
      };
    }
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION: {
      const spn_toolchain_host_t* host = find_host(decl->hosts, catalog->host);
      if (host) {
        return (spn_toolchain_support_t) { .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT, .artifact = host->artifact };
      }
      return (spn_toolchain_support_t) { .kind = SPN_TOOLCHAIN_SUPPORT_NONE, .err = SPN_ERR_TOOLCHAIN_UNAVAILABLE_FOR_HOST };
    }
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      switch (decl->detect) {
        case SPN_TOOLCHAIN_DETECT_MSVC: {
          if (sp_da_empty(catalog->sdks.msvc)) {
            return (spn_toolchain_support_t) { .kind = SPN_TOOLCHAIN_SUPPORT_NONE, .err = SPN_ERR_TOOLCHAIN_NOT_INSTALLED };
          }
          return (spn_toolchain_support_t) { .kind = SPN_TOOLCHAIN_SUPPORT_DETECTED };
        }
        case SPN_TOOLCHAIN_DETECT_NONE: {
          sp_unreachable_case();
        }
      }
      sp_unreachable_return(sp_zero_struct(spn_toolchain_support_t));
    }
  }
  sp_unreachable_return(sp_zero_struct(spn_toolchain_support_t));
}

static spn_toolchain_info_t bind_toolchain(spn_toolchain_catalog_t* catalog, const spn_toolchain_decl_t* decl) {
  spn_toolchain_info_t info = {
    .id = decl->id,
    .name = decl->name,
    .version = decl->version,
    .driver = decl->driver,
    .compiler = decl->compiler,
    .cxx = decl->cxx,
    .archiver = decl->archiver,
    .lld = decl->lld,
    .link_args = decl->link_args,
    .support = bind_support(catalog, decl),
  };
  bind_rows(catalog, decl, info.support, &info);
  return info;
}

void spn_toolchain_catalog_init(spn_toolchain_catalog_t* catalog, spn_triple_t host, spn_sdk_host_t sdks, sp_mem_t mem) {
  catalog->mem = mem;
  catalog->host = host;
  catalog->sdks = sdks;
}

void spn_toolchain_catalog_add(spn_toolchain_catalog_t* catalog, spn_toolchain_decl_t decl) {
  spn_toolchain_info_t toolchain = bind_toolchain(catalog, &decl);
  spn_toolchain_info_t* existing = spn_toolchain_catalog_get(catalog, toolchain.id);
  if (existing) {
    *existing = toolchain;
    return;
  }
  si_om_insert(catalog->mem, catalog->entries, toolchain.id, toolchain);
}

spn_toolchain_info_t* spn_toolchain_catalog_get(spn_toolchain_catalog_t* catalog, spn_toolchain_id_t id) {
  spn_toolchain_info_t** entry = si_om_getp(catalog->entries, id);
  return entry ? *entry : SP_NULLPTR;
}

const spn_toolchain_row_t* spn_toolchain_row(const spn_toolchain_info_t* info, spn_triple_t triple) {
  sp_da_for(info->rows, it) {
    if (spn_triple_equal(info->rows[it].triple, triple)) {
      return &info->rows[it];
    }
  }
  return SP_NULLPTR;
}

bool spn_toolchain_unserved(const spn_toolchain_info_t* info, spn_triple_t triple) {
  sp_da_for(info->unserved, it) {
    if (spn_triple_equal(info->unserved[it], triple)) {
      return true;
    }
  }
  return false;
}
