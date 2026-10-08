#ifndef SI_HT_H
#define SI_HT_H

#include "sp.h"

#define SI_HT_HASH_SEED     0x31415296
#define SI_HT_INVALID_INDEX UINT32_MAX

typedef u64 si_ht_it_t;
SP_TYPEDEF_FN(sp_hash_t, si_ht_hash_key_fn_t, void*, u64);
SP_TYPEDEF_FN(bool, si_ht_compare_key_fn_t, void*, void*, u64);

typedef enum si_ht_entry_state {
  SI_HT_ENTRY_INACTIVE = 0,
  SI_HT_ENTRY_ACTIVE,
  SI_HT_ENTRY_DELETED,
} si_ht_entry_state;

typedef struct {
  struct {
    si_ht_hash_key_fn_t hash;
    si_ht_compare_key_fn_t compare;
  } fn;
  struct {
    u64 key;
    u64 value;
  } size;
  struct {
    u64 entry;
    u64 kv;
    u64 value;
  } stride;
  struct {
    u64 size;
    u64 capacity;
  } header;
  sp_mem_t allocator;
  u64 tmp_idx;
} si_ht_info_t;

#define si_ht_entry_s(__K, __V)            \
  struct {                                 \
    __K key;                               \
    __V val;                               \
    si_ht_entry_state state;               \
  }

#define si_ht_s(__K, __V)                  \
  {                                        \
    si_ht_entry_s(__K, __V)* data;         \
    __K tmp_key;                           \
    __V tmp_val;                           \
    u64 size;                              \
    u64 capacity;                          \
    si_ht_info_t info;                     \
  }

#define si_ht_ex(__K, __V, __tag)              \
  struct __tag si_ht_s(__K, __V)*

#define si_ht(__K, __V)                       \
  struct si_ht_s(__K, __V)*

#define si_ht_size(ht) \
  ((ht) ? (ht)->size : 0)

#define si_ht_capacity(ht) \
  ((ht) ? (ht)->capacity : 0)

#define si_ht_empty(ht) \
  ((ht) ? (ht)->size == 0 : true)

#define si_ht_clear(ht)                                    \
  do {                                                     \
    if ((ht)) {                                            \
      sp_for(i, (ht)->capacity) {                          \
        (ht)->data[i].state = SI_HT_ENTRY_INACTIVE;        \
      }                                                    \
      (ht)->size = 0;                                      \
    }                                                      \
  } while (0)

#define si_ht_free(mem, ht)                                                       \
  do {                                                                            \
    if ((ht)) {                                                                   \
      sp_assert((ht)->info.allocator.user_data == (mem).user_data);               \
      sp_free((mem), (ht)->data, (ht)->capacity * sizeof((ht)->data[0]));         \
      (ht)->data = SP_NULLPTR;                                                    \
      sp_free((mem), (ht), sizeof(*(ht)));                                        \
      (ht) = SP_NULLPTR;                                                          \
    }                                                                             \
  } while (0)

#define si_ht_data_u8_n(ht, n) ((u8*)(&((ht)->data[n])))
#define si_ht_data_u8(ht) si_ht_data_u8_n(ht, 0)
#define si_ht_data_offset_u8(ht, field) (((u8*)(&((ht)->data[0].field))) - si_ht_data_u8(ht))
#define si_ht_field_as_u8(ht, field) ((u8*)&((ht)->field))
#define si_ht_as_u8(ht) ((u8*)(ht))
#define si_ht_field_offset_u8(ht, field) (si_ht_field_as_u8(ht, field) - si_ht_as_u8(ht))

#define si_ht_init_ex(mem, ht, hash_fn, cmp_fn)                                       \
  do {                                                                                \
    (ht)                       = sp_alloc((mem), sizeof(*(ht)));                      \
    (ht)->data                 = sp_alloc((mem), 2 * sizeof((ht)->data[0]));          \
    (ht)->info.allocator       = (mem);                                               \
    (ht)->size                 = 0;                                                   \
    (ht)->capacity             = 2;                                                   \
    (ht)->info.size.key        = sizeof((ht)->data[0].key);                           \
    (ht)->info.size.value      = sizeof((ht)->data[0].val);                           \
    (ht)->info.stride.entry    = si_ht_data_u8_n(ht, 1) - si_ht_data_u8_n(ht, 0);     \
    (ht)->info.stride.value    = si_ht_data_offset_u8(ht, val);                       \
    (ht)->info.stride.kv       = si_ht_data_offset_u8(ht, state);                     \
    (ht)->info.header.size     = si_ht_field_offset_u8(ht, size);                     \
    (ht)->info.header.capacity = si_ht_field_offset_u8(ht, capacity);                 \
    (ht)->info.fn.hash         = (hash_fn);                                           \
    (ht)->info.fn.compare      = (cmp_fn);                                            \
  } while (0)

#define si_ht_init(mem, ht) \
  si_ht_init_ex(mem, ht, si_ht_on_hash_key, si_ht_on_compare_key)

#define si_ht_insert_ex(mem, ht, k, v, hash_fn, cmp_fn)                       \
  do {                                                                        \
    if (!(ht)) {                                                              \
      si_ht_init_ex((mem), ht, hash_fn, cmp_fn);                              \
    }                                                                         \
    sp_assert((ht)->info.allocator.user_data == (mem).user_data);             \
    (ht)->tmp_key = (k);                                                      \
    (ht)->tmp_val = (v);                                                      \
    si_ht_insert_impl((mem), ht, &(ht)->tmp_key, &(ht)->tmp_val, (ht)->info); \
  } while (0)

#define si_ht_insert(mem, ht, k, v) \
  si_ht_insert_ex(mem, ht, k, v, si_ht_on_hash_key, si_ht_on_compare_key)

#define si_ht_get_key_n(ht, n) \
  (&(ht)->data[(n)].key)

#define si_ht_get_n(ht, n) \
  (&(ht)->data[(n)].val)

#define si_ht_get_tmp_n(ht) \
  si_ht_get_n(ht, (ht)->info.tmp_idx)

#define si_ht_getp(ht, key) \
  (!(ht) ? SP_NULLPTR : ( \
    (ht)->tmp_key = (key), \
    (ht)->info.tmp_idx = si_ht_tmp_key_index(ht), \
    (ht)->info.tmp_idx == SI_HT_INVALID_INDEX ? \
      SP_NULLPTR : \
      si_ht_get_tmp_n(ht) \
    ) \
  )

#define si_ht_get_ex(ht, key, idx) \
  (!(ht) ? SP_NULLPTR : (   \
    (idx) = si_ht_key_index(ht, key), \
    (idx) == SI_HT_INVALID_INDEX ?  \
      SP_NULLPTR :  \
      si_ht_get_n(ht, idx) \
    ) \
  )

#define si_ht_erase(ht, k)                               \
  do {                                                   \
    if ((ht)) {                                          \
      (ht)->tmp_key = (k);                               \
      u64 _ht_idx = si_ht_tmp_key_index(ht);             \
      if (_ht_idx != SI_HT_INVALID_INDEX) {              \
        (ht)->data[_ht_idx].state = SI_HT_ENTRY_DELETED; \
        (ht)->size--;                                    \
      }                                                  \
    }                                                    \
  } while (0)

#define si_ht_it_valid(ht, it) \
  ((ht) && (it) < si_ht_capacity(ht) && (ht)->data[(it)].state == SI_HT_ENTRY_ACTIVE)

#define si_ht_it_advance(ht, it) \
  ((ht) ? si_ht_it_advance_fn((void**)&(ht)->data, (ht)->capacity, &(it), (ht)->info) : (void)0)

#define si_ht_it_getp(ht, it) \
  (!(ht) ? SP_NULLPTR : si_ht_get_n(ht, it))

#define si_ht_it_getkp(ht, it) \
  (!(ht) ? SP_NULLPTR : si_ht_get_key_n(ht, it))

#define si_ht_it_init(ht) \
  (!(ht) ? 0 : si_ht_it_init_fn((void**)&(ht)->data, (ht)->capacity, (ht)->info))

#define si_ht_for(ht, it) \
  for (si_ht_it_t it = si_ht_it_init(ht); si_ht_it_valid(ht, it); si_ht_it_advance(ht, it))

#define si_ht_key_t(ht)   __typeof__((ht)->data[0].key)*
#define si_ht_value_t(ht) __typeof__((ht)->data[0].val)*

#define si_ht_for_kv(ht, it)                                                       \
  for (                                                                            \
    struct { si_ht_it_t idx; si_ht_key_t(ht) key; si_ht_value_t(ht) val; } it = {  \
      si_ht_it_init(ht),                                                           \
      si_ht_it_getkp(ht, si_ht_it_init(ht)),                                       \
      si_ht_it_getp(ht, si_ht_it_init(ht))                                         \
    };                                                                             \
    si_ht_it_valid(ht, it.idx);                                                    \
    (si_ht_it_advance(ht, it.idx),                                                 \
     it.key = si_ht_it_getkp(ht, it.idx),                                          \
     it.val = si_ht_it_getp(ht, it.idx))                                           \
  )

#define si_ht_front(ht) \
  (!(ht) || !si_ht_it_valid(ht, si_ht_it_init(ht)) ? SP_NULLPTR : si_ht_it_getp(ht, si_ht_it_init(ht)))

#define si_ht_tmp_key_index(ht) \
  si_ht_get_key_index_fn((void**)&(ht)->data, (void*)&(ht)->tmp_key, (ht)->capacity, (ht)->info)

#define si_ht_key_index(ht, key) \
  si_ht_get_key_index_fn((void**)&(ht)->data, (void*)&(key), (ht)->capacity, (ht)->info)

////////////
// STR_HT //
////////////
#define si_str_ht(t) si_ht(sp_str_t, t)

#define si_str_ht_init(mem, ht) \
  si_ht_init_ex(mem, ht, si_ht_on_hash_str_key, si_ht_on_compare_str_key)

#define si_str_ht_insert(mem, ht, key, value) \
  si_ht_insert_ex(mem, ht, key, value, si_ht_on_hash_str_key, si_ht_on_compare_str_key)

#define si_str_ht_get(ht, key)       si_ht_getp(ht, key)
#define si_str_ht_get_ex(ht, key, n) si_ht_get_ex(ht, key, n)
#define si_str_ht_erase(ht, key)     si_ht_erase(ht, key)
#define si_str_ht_size(ht)           si_ht_size(ht)
#define si_str_ht_capacity(ht)       si_ht_capacity(ht)
#define si_str_ht_empty(ht)          si_ht_empty(ht)
#define si_str_ht_clear(ht)          si_ht_clear(ht)
#define si_str_ht_free(mem, ht)      si_ht_free(mem, ht)
#define si_str_ht_front(ht)          si_ht_front(ht)
#define si_str_ht_for(ht, it)        si_ht_for(ht, it)
#define si_str_ht_for_kv(ht, it)     si_ht_for_kv(ht, it)
#define si_str_ht_it_init(ht)        si_ht_it_init(ht)
#define si_str_ht_it_valid(ht, it)   si_ht_it_valid(ht, it)
#define si_str_ht_it_advance(ht, it) si_ht_it_advance(ht, it)
#define si_str_ht_it_getp(ht, it)    si_ht_it_getp(ht, it)
#define si_str_ht_it_getkp(ht, it)   si_ht_it_getkp(ht, it)

/////////////
// CSTR_HT //
/////////////
#define si_cstr_ht(t) si_ht(const c8*, t)

#define si_cstr_ht_init(mem, ht) \
  si_ht_init_ex(mem, ht, si_ht_on_hash_cstr_key, si_ht_on_compare_cstr_key)

#define si_cstr_ht_insert(mem, ht, key, value) \
  si_ht_insert_ex(mem, ht, key, value, si_ht_on_hash_cstr_key, si_ht_on_compare_cstr_key)

#define si_cstr_ht_get(ht, key)       si_ht_getp(ht, key)
#define si_cstr_ht_erase(ht, key)     si_ht_erase(ht, key)
#define si_cstr_ht_size(ht)           si_ht_size(ht)
#define si_cstr_ht_capacity(ht)       si_ht_capacity(ht)
#define si_cstr_ht_empty(ht)          si_ht_empty(ht)
#define si_cstr_ht_clear(ht)          si_ht_clear(ht)
#define si_cstr_ht_free(mem, ht)      si_ht_free(mem, ht)
#define si_cstr_ht_front(ht)          si_ht_front(ht)
#define si_cstr_ht_for(ht, it)        si_ht_for(ht, it)
#define si_cstr_ht_for_kv(ht, it)     si_ht_for_kv(ht, it)
#define si_cstr_ht_it_init(ht)        si_ht_it_init(ht)
#define si_cstr_ht_it_valid(ht, it)   si_ht_it_valid(ht, it)
#define si_cstr_ht_it_advance(ht, it) si_ht_it_advance(ht, it)
#define si_cstr_ht_it_getp(ht, it)    si_ht_it_getp(ht, it)
#define si_cstr_ht_it_getkp(ht, it)   si_ht_it_getkp(ht, it)

u64        si_ht_get_key_index_fn(void** data, void* key, u64 capacity, si_ht_info_t info);
void       si_ht_resize_impl(sp_mem_t mem, void** data, u64 old_cap, u64 new_cap, si_ht_info_t info);
void       si_ht_insert_impl(sp_mem_t mem, void* ht, void* key, void* val, si_ht_info_t info);
si_ht_it_t si_ht_it_init_fn(void** data, u64 capacity, si_ht_info_t info);
void       si_ht_it_advance_fn(void** data, u64 capacity, u64* it, si_ht_info_t info);
sp_hash_t  si_ht_on_hash_key(void* key, u64 size);
bool       si_ht_on_compare_key(void* ka, void* kb, u64 size);
sp_hash_t  si_ht_on_hash_str_key(void* key, u64 size);
bool       si_ht_on_compare_str_key(void* ka, void* kb, u64 size);
sp_hash_t  si_ht_on_hash_cstr_key(void* key, u64 size);
bool       si_ht_on_compare_cstr_key(void* ka, void* kb, u64 size);

#endif
