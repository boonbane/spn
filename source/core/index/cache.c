#include "index/cache.h"
#include "index/index.h"

void spn_index_cache_init(spn_index_cache_t* cache, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_da(spn_index_info_t)* indexes) {
  cache->mem = mem;
  cache->intern = intern;
  cache->roots = roots;
  cache->indexes = indexes;
  sp_ht_init(mem, cache->packages);
}

spn_err_t spn_index_cache_get_package(spn_index_cache_t* cache, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  spn_index_pkg_t** cached = sp_ht_getp(cache->packages, name.id);
  if (cached) {
    *pkg = *cached;
    return SPN_OK;
  }

  spn_index_pkg_t* package = SP_NULLPTR;
  sp_da_for(*cache->indexes, it) {
    spn_index_info_t* index = &(*cache->indexes)[it];
    spn_try(spn_index_get_package(index, cache->mem, cache->intern, cache->roots, name, &package, err));
    if (package) {
      break;
    }
  }

  sp_ht_insert(cache->packages, name.id, package);
  *pkg = package;
  return SPN_OK;
}

