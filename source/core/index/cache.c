#include "index/cache.h"
#include "index/index.h"

void si_index_cache_init(spn_index_cache_t* cache, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, si_da(si_index_place_t) indexes) {
  *cache = (spn_index_cache_t) {
    .mem = mem,
    .intern = intern,
    .roots = roots,
    .indexes = indexes,
  };
  sp_ht_init(mem, cache->packages);
}

spn_err_t si_index_cache_get_package(spn_index_cache_t* cache, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  if (si_da_empty(cache->indexes)) {
    *err = (spn_err_union_t) { .kind = SPN_ERR_INDEX_NONE, .subject = name.str };
    return err->kind;
  }

  spn_index_pkg_t** cached = sp_ht_getp(cache->packages, name.id);
  if (cached) {
    *pkg = *cached;
    return SPN_OK;
  }

  spn_index_pkg_t* package = SP_NULLPTR;
  si_da_for(cache->indexes, it) {
    cache->consulted = sp_max(cache->consulted, (u32)it + 1);
    if (sp_str_empty(cache->indexes[it].dir)) {
      break;
    }
    spn_try(si_index_get_package(&cache->indexes[it], cache->mem, cache->intern, cache->roots, name, &package, err));
    if (package) {
      break;
    }
  }

  sp_ht_insert(cache->packages, name.id, package);
  *pkg = package;
  return SPN_OK;
}

