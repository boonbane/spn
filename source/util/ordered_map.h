#ifndef SI_OM_H
#define SI_OM_H

#include "sp.h"

#include "array/array.h"
#include "hash_table/hash_table.h"

#define si_om(K, V)        \
  struct {                 \
    si_da(V*) order;       \
    si_ht(K, V*) index;    \
  }

#define si_om_insert_ex(mem, om, key, val, hash_fn, cmp_fn)                                   \
  do {                                                                                        \
    if (!(om).index) {                                                                        \
      si_ht_init_ex((mem), (om).index, hash_fn, cmp_fn);                                      \
    }                                                                                         \
    if (si_ht_getp((om).index, (key)) != SP_NULLPTR) {                                        \
      break;                                                                                  \
    }                                                                                         \
    si_da_push((mem), (om).order, sp_alloc((mem), sizeof(*(om).order[0])));                   \
    **si_da_back((om).order) = (val);                                                         \
    si_ht_insert_ex((mem), (om).index, (om).index->tmp_key, *si_da_back((om).order), hash_fn, cmp_fn); \
  } while (0)

#define si_om_emplace_ex(mem, om, key, entry, hash_fn, cmp_fn)                                \
  do {                                                                                        \
    if (!(om).index) {                                                                        \
      si_ht_init_ex((mem), (om).index, hash_fn, cmp_fn);                                      \
    }                                                                                         \
    sp_assert(si_ht_getp((om).index, (key)) == SP_NULLPTR);                                   \
    si_da_push((mem), (om).order, sp_alloc((mem), sizeof(*(om).order[0])));                   \
    si_ht_insert_ex((mem), (om).index, (om).index->tmp_key, *si_da_back((om).order), hash_fn, cmp_fn); \
    (entry) = *si_da_back((om).order);                                                        \
  } while (0)

#define si_om_insert(mem, om, key, val) \
  si_om_insert_ex(mem, om, key, val, si_ht_on_hash_key, si_ht_on_compare_key)

#define si_om_emplace(mem, om, key, entry) \
  si_om_emplace_ex(mem, om, key, entry, si_ht_on_hash_key, si_ht_on_compare_key)

#define si_om_free(mem, om)                                                   \
  do {                                                                        \
    si_da_for((om).order, _om_it) {                                           \
      sp_free((mem), (om).order[_om_it], sizeof(*(om).order[0]));             \
    }                                                                         \
    si_da_free((mem), (om).order);                                            \
    si_ht_free((mem), (om).index);                                            \
  } while (0)

#define si_om_getp(om, key)  si_ht_getp((om).index, (key))
#define si_om_get(om, key)   (si_ht_getp((om).index, (key)) ? *si_ht_get_tmp_n((om).index) : SP_NULLPTR)
#define si_om_has(om, key)   (si_ht_getp((om).index, (key)) != SP_NULLPTR)
#define si_om_at(om, n)      ((om).order[(n)])
#define si_om_size(om)       si_da_size((om).order)
#define si_om_empty(om)      si_da_empty((om).order)
#define si_om_for(om, it)    for (u32 it = 0; it < si_om_size(om); it++)
#define si_om_back(om)       (*si_da_back((om).order))

////////////
// STR_OM //
////////////
#define si_str_om(T) si_om(sp_str_t, T)

#define si_str_om_insert(mem, om, key, val) \
  si_om_insert_ex(mem, om, key, val, si_ht_on_hash_str_key, si_ht_on_compare_str_key)

#define si_str_om_emplace(mem, om, key, entry) \
  si_om_emplace_ex(mem, om, key, entry, si_ht_on_hash_str_key, si_ht_on_compare_str_key)

#define si_str_om_free(mem, om)      si_om_free(mem, om)
#define si_str_om_get(om, key)       si_om_get(om, key)
#define si_str_om_getp(om, key)      si_om_getp(om, key)
#define si_str_om_has(om, key)       si_om_has(om, key)
#define si_str_om_at(om, n)          si_om_at(om, n)
#define si_str_om_size(om)           si_om_size(om)
#define si_str_om_empty(om)          si_om_empty(om)
#define si_str_om_for(om, it)        si_om_for(om, it)
#define si_str_om_back(om)           si_om_back(om)

#endif
