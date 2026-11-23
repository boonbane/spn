#include "toolchain/select.h"

#include "ctx/types.h"
#include "error/error.h"
#include "paths/paths.h"
#include "toolchain/catalog.h"
#include "toolchain/msvc.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

typedef enum {
  REACH_UNSERVED,
  REACH_UNLISTED,
  REACH_DROPPED,
  REACH_RUNTIME,
  REACH_SANITIZERS,
  REACH_OK,
} reach_kind_t;

typedef struct {
  reach_kind_t kind;
  spn_err_t err;
  spn_toolchain_row_t row;
  spn_sanitizer_set_t unsupported;
  spn_runtime_t runtime;
  spn_linking_t linking;
} reach_t;

static bool usable(const spn_toolchain_info_t* toolchain) {
  return toolchain->support.kind != SPN_TOOLCHAIN_SUPPORT_NONE;
}

static sp_da(spn_triple_t) triples(sp_mem_t mem, sp_da(spn_toolchain_row_t) rows) {
  sp_da(spn_triple_t) out = sp_da_new(mem, spn_triple_t);
  sp_da_for(rows, it) {
    sp_da_push(out, rows[it].triple);
  }
  return out;
}

static reach_kind_t absence(const spn_toolchain_catalog_t* catalog, const spn_toolchain_info_t* toolchain, spn_triple_t triple) {
  if (spn_toolchain_unserved(toolchain, triple)) {
    return REACH_DROPPED;
  }
  spn_sdk_t sdk = sp_zero;
  if (spn_toolchain_driver_retargets(toolchain->driver) && !si_sdk_served(catalog->mem, &catalog->sdks, catalog->host, triple, &sdk)) {
    return REACH_UNSERVED;
  }
  return REACH_UNLISTED;
}

static reach_t attempt(const spn_toolchain_catalog_t* catalog, const spn_toolchain_info_t* toolchain, spn_toolchain_query_t query, spn_toolchain_candidate_t candidate) {
  const spn_toolchain_row_t* row = spn_toolchain_row(toolchain, candidate.triple);
  if (!row) {
    return (reach_t) { .kind = absence(catalog, toolchain, candidate.triple), .row = { .triple = candidate.triple } };
  }

  spn_linkings_t linkings = candidate.linkings;
  u32 pick = 0;
  while (pick < linkings.count && !(row->runtimes & si_runtime_bit(linkings.items[pick].runtime))) {
    pick++;
  }
  if (pick == linkings.count) {
    return (reach_t) { .kind = REACH_RUNTIME, .row = *row, .runtime = linkings.items[0].runtime };
  }
  spn_linking_t linking = linkings.items[pick];
  spn_sanitizer_set_t missing = query.sanitizers & ~row->sanitizers;
  if (missing) {
    return (reach_t) { .kind = REACH_SANITIZERS, .err = SPN_ERR_SANITIZER_UNSUPPORTED, .row = *row, .unsupported = missing };
  }
  spn_sanitizer_set_t heavy = query.sanitizers & ~SPN_SANITIZER_UNDEFINED;
  if (heavy && !spn_ld_loader(row->triple, linking)) {
    return (reach_t) { .kind = REACH_SANITIZERS, .err = SPN_ERR_SANITIZER_STATIC, .row = *row, .unsupported = heavy };
  }
  return (reach_t) { .kind = REACH_OK, .row = *row, .linking = linking };
}

static bool reached(reach_t reach) {
  return reach.kind == REACH_OK;
}

static bool closer(reach_t a, reach_t b) {
  return a.kind > b.kind;
}

static reach_t reach_best(const spn_toolchain_catalog_t* catalog, const spn_toolchain_info_t* toolchain, spn_toolchain_query_t query, spn_toolchain_candidates_t candidates) {
  reach_t best = attempt(catalog, toolchain, query, candidates.items[0]);
  sp_for_range(it, 1, candidates.count) {
    if (reached(best)) {
      break;
    }
    reach_t reach = attempt(catalog, toolchain, query, candidates.items[it]);
    if (closer(reach, best)) {
      best = reach;
    }
  }
  return best;
}

static spn_err_t sdk_refusal(spn_triple_t target) {
  switch (spn_sdk_need(target)) {
    case SPN_SDK_NEED_SYSROOT: return SPN_ERR_TOOLCHAIN_SYSROOT;
    case SPN_SDK_NEED_MACOS: return SPN_ERR_TOOLCHAIN_SDK_MACOS;
    case SPN_SDK_NEED_MSVC: return SPN_ERR_TOOLCHAIN_SDK_MSVC;
    case SPN_SDK_NEED_NONE: sp_unreachable_case();
  }
  sp_unreachable_return(SPN_ERR_TOOLCHAIN_TARGET);
}

static bool satisfies(const spn_toolchain_catalog_t* catalog, const spn_toolchain_info_t* toolchain, spn_toolchain_query_t query, reach_t* reach) {
  if (!usable(toolchain)) {
    return false;
  }
  *reach = reach_best(catalog, toolchain, query, query.candidates);
  return reached(*reach);
}

static sp_da(sp_str_t) satisfying(spn_toolchain_catalog_t* catalog, spn_toolchain_query_t query) {
  sp_da(sp_str_t) names = sp_da_new(spn.events->mem, sp_str_t);
  si_om_for(catalog->entries, it) {
    spn_toolchain_info_t* entry = si_om_at(catalog->entries, it);
    reach_t reach = sp_zero;
    if (satisfies(catalog, entry, query, &reach)) {
      sp_da_push(names, entry->name.str);
    }
  }
  return names;
}

sp_da(sp_str_t) spn_toolchain_host_candidates(sp_mem_t mem, spn_toolchain_catalog_t* catalog) {
  sp_da(sp_str_t) names = sp_da_new(mem, sp_str_t);
  si_om_for(catalog->entries, it) {
    spn_toolchain_info_t* entry = si_om_at(catalog->entries, it);
    const spn_toolchain_row_t* row = spn_toolchain_row(entry, catalog->host);
    if (usable(entry) && row && row->sdk.kind == SPN_SDK_HOST) {
      sp_da_push(names, entry->name.str);
    }
  }
  return names;
}

sp_da(sp_str_t) spn_toolchain_exceptions_candidates(sp_mem_t mem, spn_toolchain_catalog_t* catalog, spn_triple_t target) {
  sp_da(sp_str_t) names = sp_da_new(mem, sp_str_t);
  si_om_for(catalog->entries, it) {
    spn_toolchain_info_t* entry = si_om_at(catalog->entries, it);
    const spn_toolchain_row_t* row = spn_toolchain_row(entry, target);
    if (usable(entry) && row && row->exceptions.declared) {
      sp_da_push(names, entry->name.str);
    }
  }
  return names;
}

static spn_err_t emit(spn_err_t kind, spn_toolchain_catalog_t* catalog, spn_toolchain_query_t query, spn_err_toolchain_t err) {
  err.name = query.toolchain.name.str;
  err.query = query.target;
  err.host = catalog->host;
  err.sanitizers = query.sanitizers;
  return spn_err_emit(&spn, (spn_err_union_t) { .kind = kind, .toolchain = err });
}

static void stage(sp_mem_t mem, si_da(spn_path_t)* staged, const spn_path_t* files, u64 count) {
  sp_for(it, count) {
    bool listed = false;
    si_da_for_value(*staged, file) {
      listed = listed || spn_path_equal(file, files[it]);
    }
    if (!listed) {
      si_da_push(mem, *staged, files[it]);
    }
  }
}

spn_err_t si_toolchain_select(spn_toolchain_catalog_t* catalog, spn_toolchain_query_t query, spn_toolchain_selection_t* selection) {
  sp_assert(query.candidates.count);
  *selection = sp_zero_s(spn_toolchain_selection_t);
  switch (query.toolchain.kind) {
    case SPN_TOOLCHAIN_REF_AUTO: {
      reach_t closest = sp_zero;
      si_om_for(catalog->entries, it) {
        spn_toolchain_info_t* entry = si_om_at(catalog->entries, it);
        reach_t reach = sp_zero;
        if (satisfies(catalog, entry, query, &reach)) {
          *selection = (spn_toolchain_selection_t) { .toolchain = entry, .row = reach.row, .linking = reach.linking };
          break;
        }
        if (closer(reach, closest)) {
          closest = reach;
        }
      }
      if (!selection->toolchain) {
        return emit(SPN_ERR_TOOLCHAIN_NONE, catalog, query, (spn_err_toolchain_t) { .target = query.target, .runtime = closest.runtime });
      }
      break;
    }
    case SPN_TOOLCHAIN_REF_NAMED: {
      spn_toolchain_info_t* toolchain = spn_toolchain_catalog_get(catalog, query.toolchain.id);
      if (!toolchain) {
        return emit(SPN_ERR_TOOLCHAIN_UNKNOWN, catalog, query, (spn_err_toolchain_t) { .target = query.target, .candidates = satisfying(catalog, query) });
      }
      if (!usable(toolchain)) {
        return emit(toolchain->support.err, catalog, query, (spn_err_toolchain_t) { .target = query.target, .candidates = satisfying(catalog, query) });
      }

      reach_t reach = reach_best(catalog, toolchain, query, query.candidates);
      switch (reach.kind) {
        case REACH_RUNTIME: {
          static const spn_runtime_t runtimes [] = { SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED };
          sp_da(spn_runtime_t) supported = sp_da_new(spn.events->mem, spn_runtime_t);
          sp_carr_for(runtimes, it) {
            if (reach.row.runtimes & si_runtime_bit(runtimes[it])) {
              sp_da_push(supported, runtimes[it]);
            }
          }
          return spn_err_emit(&spn, (spn_err_union_t) {
            .kind = SPN_ERR_RUNTIME_UNSUPPORTED,
            .runtime = {
              .toolchain = toolchain->name.str,
              .target = reach.row.triple,
              .requested = reach.runtime,
              .supported = supported,
            },
          });
        }
        case REACH_SANITIZERS: {
          return spn_err_emit(&spn, (spn_err_union_t) {
            .kind = reach.err,
            .sanitizer = {
              .toolchain = toolchain->name.str,
              .target = reach.row.triple,
              .unsupported = reach.unsupported,
              .supported = reach.row.sanitizers,
            },
          });
        }
        case REACH_DROPPED:
        case REACH_UNSERVED: {
          return emit(sdk_refusal(reach.row.triple), catalog, query, (spn_err_toolchain_t) { .target = reach.row.triple, .candidates = satisfying(catalog, query), .targets = triples(spn.events->mem, toolchain->rows) });
        }
        case REACH_UNLISTED: {
          return emit(SPN_ERR_TOOLCHAIN_TARGET, catalog, query, (spn_err_toolchain_t) { .target = query.target, .candidates = satisfying(catalog, query), .targets = triples(spn.events->mem, toolchain->rows) });
        }
        case REACH_OK: {
          break;
        }
      }
      *selection = (spn_toolchain_selection_t) { .toolchain = toolchain, .row = reach.row, .linking = reach.linking };
      break;
    }
    case SPN_TOOLCHAIN_REF_NONE: {
      sp_unreachable_case();
    }
  }

  const spn_msvc_redist_t* redist = SP_NULLPTR;
  spn_path_t asan = sp_zero;
  spn_sdk_t* sdk = &selection->row.sdk;
  if (sdk->kind == SPN_SDK_MSVC && !spn_path_empty(sdk->msvc.install.vs)) {
    sp_mem_t mem = spn.events->mem;
    const spn_msvc_installs_t* installs = &catalog->sdks.msvc;
    spn_triple_t target = selection->row.triple;
    bool compiler = selection->toolchain->driver == SPN_CC_DRIVER_MSVC;
    spn_msvc_pick_t pick = sp_zero;
    switch (si_msvc_match(installs, target.arch, query.msvc, compiler, &pick)) {
      case SPN_MSVC_MATCH_OK: {
        sdk->msvc = si_sdk_from_msvc(catalog->mem, pick.kit, &pick.toolset->vs, target.arch);
        redist = &pick.toolset->redist;
        if (compiler) {
          asan = sdk->msvc.runtime.asan;
        }
        break;
      }
      case SPN_MSVC_MATCH_ABSENT: {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_TOOLCHAIN_SDK_MSVC,
          .toolchain = {
            .name = selection->toolchain->name.str,
            .target = target,
            .query = query.target,
            .host = catalog->host,
          },
        });
      }
      case SPN_MSVC_MATCH_TOOLSET: {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_MSVC_TOOLSET,
          .msvc = {
            .target = target,
            .request = sp_str_copy(mem, query.msvc.toolset),
            .installed = si_msvc_installed_toolsets(mem, installs, target.arch, compiler),
          },
        });
      }
      case SPN_MSVC_MATCH_SDK: {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_MSVC_SDK,
          .msvc = {
            .target = target,
            .request = sp_str_copy(mem, query.msvc.sdk),
            .installed = si_msvc_installed_sdks(mem, installs, target.arch),
          },
        });
      }
    }
  }

  const spn_toolchain_files_t* files = &selection->row.files;
  spn_toolchain_staged_t* staged = &selection->staged;
  bool shared = selection->linking.runtime == SPN_RUNTIME_SHARED;
  bool sanitized = false;
  if (shared) {
    stage(catalog->mem, &staged->c, files->c, si_da_size(files->c));
  }
  sp_for(st, si_sanitizer_count) {
    if ((query.sanitizers & si_sanitizer_at(st)) && !si_da_empty(files->sanitizers[st])) {
      stage(catalog->mem, &staged->c, files->sanitizers[st], si_da_size(files->sanitizers[st]));
      sanitized = true;
    }
  }
  if ((query.sanitizers & SPN_SANITIZER_ADDRESS) && !spn_path_empty(asan)) {
    stage(catalog->mem, &staged->c, &asan, 1);
    sanitized = true;
  }
  if (redist && (shared || sanitized)) {
    if (si_da_empty(redist->files)) {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_TOOLCHAIN_FILE_MISSING,
        .toolchain_file = {
          .toolchain = selection->toolchain->name.str,
          .target = selection->row.triple,
          .path = redist->dir,
        },
      });
    }
    stage(catalog->mem, &staged->c, redist->files, si_da_size(redist->files));
  }
  stage(catalog->mem, &staged->cxx, staged->c, si_da_size(staged->c));
  if (shared) {
    stage(catalog->mem, &staged->cxx, files->cxx, si_da_size(files->cxx));
  }
  return SPN_OK;
}
