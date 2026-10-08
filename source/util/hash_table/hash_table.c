#include "hash_table/hash_table.h"

bool si_ht_on_compare_key(void* ka, void* kb, u64 size) {
  return sp_mem_is_equal(ka, kb, size);
}

sp_hash_t si_ht_on_hash_key(void* key, u64 size) {
  return sp_hash_bytes(key, size, SI_HT_HASH_SEED);
}

sp_hash_t si_ht_on_hash_str_key(void* key, u64 size) {
  sp_str_t* str = (sp_str_t*)key;
  return sp_hash_bytes(str->data, str->len, SI_HT_HASH_SEED);
}

bool si_ht_on_compare_str_key(void* ka, void* kb, u64 size) {
  sp_str_t* sa = (sp_str_t*)ka;
  sp_str_t* sb = (sp_str_t*)kb;
  return sp_str_equal(*sa, *sb);
}

sp_hash_t si_ht_on_hash_cstr_key(void* key, u64 size) {
  const c8** str = (const c8**)key;
  return sp_hash_cstr(*str);
}

bool si_ht_on_compare_cstr_key(void* ka, void* kb, u64 size) {
  const c8** sa = (const c8**)ka;
  const c8** sb = (const c8**)kb;
  return sp_cstr_equal(*sa, *sb);
}

u64 si_ht_get_key_index_fn(void** data, void* key, u64 capacity, si_ht_info_t info) {
  sp_hash_t hash = info.fn.hash(key, info.size.key);
  u64 hash_idx = hash % capacity;

  for (u64 c = 0; c < capacity; ++c) {
    u64 i = (hash_idx + c) % capacity;
    u64 offset = i * info.stride.entry;
    si_ht_entry_state state = *(si_ht_entry_state*)((c8*)(*data) + offset + info.stride.kv);

    if (state == SI_HT_ENTRY_INACTIVE) {
      break;
    }
    if (state == SI_HT_ENTRY_DELETED) {
      continue;
    }
    void* k = (c8*)(*data) + offset;
    if (info.fn.compare(k, key, info.size.key)) {
      return i;
    }
  }
  return SI_HT_INVALID_INDEX;
}

void si_ht_resize_impl(sp_mem_t mem, void** data, u64 old_cap, u64 new_cap, si_ht_info_t info) {
  void* old_data = *data;
  void* new_data = sp_alloc(mem, new_cap * info.stride.entry);

  for (u64 i = 0; i < old_cap; ++i) {
    u64 offset = i * info.stride.entry;
    si_ht_entry_state state = *(si_ht_entry_state*)((c8*)old_data + offset + info.stride.kv);
    if (state != SI_HT_ENTRY_ACTIVE) {
      continue;
    }

    void* old_key = (c8*)old_data + offset;
    sp_hash_t hash = info.fn.hash(old_key, info.size.key);
    u64 new_idx = hash % new_cap;

    while (*(si_ht_entry_state*)((c8*)new_data + new_idx * info.stride.entry + info.stride.kv) == SI_HT_ENTRY_ACTIVE) {
      new_idx = (new_idx + 1) % new_cap;
    }

    sp_mem_copy((c8*)new_data + new_idx * info.stride.entry, (c8*)old_data + offset, info.stride.kv);
    *(si_ht_entry_state*)((c8*)new_data + new_idx * info.stride.entry + info.stride.kv) = SI_HT_ENTRY_ACTIVE;
  }

  sp_free(mem, old_data, old_cap * info.stride.entry);
  *data = new_data;
}

void si_ht_insert_impl(sp_mem_t mem, void* ht, void* key, void* val, si_ht_info_t info) {
  u8* base = (u8*)ht;
  void** data = (void**)base;
  u64* size = (u64*)(base + info.header.size);
  u64* capacity = (u64*)(base + info.header.capacity);

  u64 cap = *capacity;
  if (*size * 4 >= cap * 3) {
    u64 new_cap = cap * 2;
    si_ht_resize_impl(mem, data, cap, new_cap, info);
    *capacity = new_cap;
    cap = new_cap;
  }

  sp_hash_t hash = info.fn.hash(key, info.size.key);
  u64 hash_idx = hash % cap;
  u64 first_free = SI_HT_INVALID_INDEX;

  for (u64 c = 0; c < cap; ++c) {
    u64 i = (hash_idx + c) % cap;
    u64 offset = i * info.stride.entry;
    si_ht_entry_state state = *(si_ht_entry_state*)((u8*)(*data) + offset + info.stride.kv);

    if (state == SI_HT_ENTRY_INACTIVE) {
      if (first_free == SI_HT_INVALID_INDEX) {
        first_free = i;
      }
      break;
    }
    if (state == SI_HT_ENTRY_DELETED) {
      if (first_free == SI_HT_INVALID_INDEX) {
        first_free = i;
      }
      continue;
    }
    void* k = (u8*)(*data) + offset;
    if (info.fn.compare(k, key, info.size.key)) {
      u8* entry = (u8*)(*data) + offset;
      sp_mem_copy(entry + info.stride.value, val, info.size.value);
      return;
    }
  }

  u64 idx = first_free != SI_HT_INVALID_INDEX ? first_free : hash_idx;
  u8* entry = (u8*)(*data) + idx * info.stride.entry;
  sp_mem_copy(entry, key, info.size.key);
  sp_mem_copy(entry + info.stride.value, val, info.size.value);
  *(si_ht_entry_state*)(entry + info.stride.kv) = SI_HT_ENTRY_ACTIVE;
  (*size)++;
}

si_ht_it_t si_ht_it_init_fn(void** data, u64 capacity, si_ht_info_t info) {
  si_ht_it_t it = 0;
  for (; it < capacity; ++it) {
    u64 offset = it * info.stride.entry;
    si_ht_entry_state state = *(si_ht_entry_state*)((u8*)*data + offset + info.stride.kv);
    if (state == SI_HT_ENTRY_ACTIVE) {
      break;
    }
  }
  return it;
}

void si_ht_it_advance_fn(void** data, u64 capacity, u64* it, si_ht_info_t info) {
  (*it)++;
  for (; *it < capacity; ++*it) {
    u64 offset = *it * info.stride.entry;
    si_ht_entry_state state = *(si_ht_entry_state*)((u8*)*data + offset + info.stride.kv);
    if (state == SI_HT_ENTRY_ACTIVE) {
      break;
    }
  }
}
