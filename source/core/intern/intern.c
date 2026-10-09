#include "intern/intern.h"

#define SP_INTERN_INDEX_MIN_CAPACITY 16

SP_PRIVATE u32 sp_intern_default_hash(sp_str_t str) {
  return (u32)sp_hash_bytes(str.data, str.len, 0);
}

SP_PRIVATE u32 sp_intern_index_find(sp_intern_index_t* index, sp_str_t str, u32 hash) {
  u32 mask = index->capacity - 1;
  u32 slot = hash & mask;

  while (index->slots[slot].data) {
    sp_intern_slot_t* entry = &index->slots[slot];
    if (entry->hash == hash && entry->len == str.len && sp_mem_is_equal(entry->data, str.data, str.len)) {
      break;
    }
    slot = (slot + 1) & mask;
  }

  return slot;
}

SP_PRIVATE void sp_intern_index_insert(sp_intern_index_t* index, sp_intern_slot_t entry) {
  u32 mask = index->capacity - 1;
  u32 slot = entry.hash & mask;

  while (index->slots[slot].data) {
    slot = (slot + 1) & mask;
  }

  index->slots[slot] = entry;
  index->count += 1;
}

SP_PRIVATE void sp_intern_index_grow(sp_intern_index_t* index) {
  u32 old_capacity = index->capacity;
  sp_intern_slot_t* old_slots = index->slots;

  index->capacity = old_capacity * 2;
  index->count = 0;
  index->slots = sp_alloc_n(index->mem, sp_intern_slot_t, index->capacity);

  sp_for(it, old_capacity) {
    sp_intern_slot_t entry = old_slots[it];
    if (entry.data) {
      sp_intern_index_insert(index, entry);
    }
  }

  sp_free(index->mem, old_slots, (u64)old_capacity * sizeof(sp_intern_slot_t));
}

SP_PRIVATE void sp_intern_index_put(sp_intern_index_t* index, u32 slot, sp_intern_slot_t entry) {
  if ((index->count + 1) * 4 > index->capacity * 3) {
    sp_intern_index_grow(index);
    sp_intern_index_insert(index, entry);
    return;
  }

  index->slots[slot] = entry;
  index->count += 1;
}

sp_intern_t* sp_intern_new(sp_mem_t mem) {
  sp_intern_t* intern = sp_alloc_type(mem, sp_intern_t);
  sp_intern_init_ex(intern, mem, sp_intern_default_hash);
  return intern;
}

void sp_intern_init(sp_intern_t* intern, sp_mem_t mem) {
  sp_intern_init_ex(intern, mem, sp_intern_default_hash);
}

void sp_intern_init_ex(sp_intern_t* intern, sp_mem_t mem, sp_intern_hash_fn_t hash) {
  if (!intern) return;

  intern->hash = hash;
  intern->next_id = SP_INTERN_INVALID_ID + 1;
  intern->data = sp_mem_arena_new_ex(mem, 4096, 1);

  intern->index = (sp_intern_index_t) {
    .mem = mem,
    .capacity = SP_INTERN_INDEX_MIN_CAPACITY,
    .slots = sp_alloc_n(mem, sp_intern_slot_t, SP_INTERN_INDEX_MIN_CAPACITY),
  };

  sp_mem_t data = sp_mem_arena_as_allocator(intern->data);
  sp_alloc(data, 1);
  const c8* empty = sp_str_to_cstr(data, sp_str_lit(""));
  sp_intern_index_insert(&intern->index, (sp_intern_slot_t) {
    .hash = hash(sp_str_lit("")),
    .len = 0,
    .id = SP_INTERN_INVALID_ID,
    .data = empty,
  });

  intern->by_id = sp_da_new(mem, sp_str_t);
  sp_da_push(intern->by_id, sp_str(empty, 0));
}

SP_PRIVATE sp_intern_str_t sp_intern_get_or_insert_2_locked(sp_intern_t* intern, sp_str_t str, u32 hash) {
  sp_intern_slot_t* slots = intern->index.slots;
  u32 slot = sp_intern_index_find(&intern->index, str, hash);

  if (slots[slot].data) {
    return (sp_intern_str_t) {
      .id = slots[slot].id,
      .str = sp_str(slots[slot].data, slots[slot].len)
    };
  }

  sp_mem_t arena = sp_mem_arena_as_allocator(intern->data);
  sp_intern_str_t interned = {
    .id = intern->next_id++,
    .str = (sp_str_t) {
      .data = sp_str_to_cstr(arena, str),
      .len = str.len,
    }
  };

  sp_intern_index_put(&intern->index, slot, (sp_intern_slot_t) {
    .hash = hash,
    .data = interned.str.data,
    .len = interned.str.len,
    .id = interned.id
  });
  sp_da_push(intern->by_id, interned.str);

  return interned;
}

sp_intern_str_t sp_intern(sp_intern_t* intern, sp_str_t str) {
  sp_assert(intern);
  u32 hash = intern->hash(str);
  sp_mutex_lock(&intern->mutex);
  sp_intern_str_t interned = sp_intern_get_or_insert_2_locked(intern, str, hash);
  sp_mutex_unlock(&intern->mutex);
  return interned;

}
sp_intern_str_t sp_intern_cstr(sp_intern_t* intern, const c8* cstr) {
  return sp_intern(intern, sp_cstr_as_str(cstr));
}

sp_str_t sp_intern_str_from_id(sp_intern_t* intern, sp_intern_id_t id) {
  if (!intern) return SP_INTERN_INVALID_STR;
  sp_mutex_lock(&intern->mutex);
  sp_str_t str = id < sp_da_size(intern->by_id) ? intern->by_id[id] : SP_INTERN_INVALID_STR;
  sp_mutex_unlock(&intern->mutex);
  return str;
}

u64 sp_intern_get_len(sp_intern_t* intern) {
  if (!intern) return 0;
  sp_mutex_lock(&intern->mutex);
  u64 count = intern->index.count;
  sp_mutex_unlock(&intern->mutex);
  return count;
}

u64 sp_intern_get_bytes_used(sp_intern_t* intern) {
  if (!intern) return 0;
  sp_mutex_lock(&intern->mutex);
  u64 bytes = sp_mem_arena_bytes_used(intern->data);
  sp_mutex_unlock(&intern->mutex);
  return bytes;
}

u64 sp_intern_get_bytes_allocated(sp_intern_t* intern) {
  if (!intern) return 0;
  sp_mutex_lock(&intern->mutex);
  u64 bytes = sp_mem_arena_capacity(intern->data);
  sp_mutex_unlock(&intern->mutex);
  return bytes;
}
