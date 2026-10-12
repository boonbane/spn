#include "mem/mem.h"

static void* on_alloc(void* user_data, sp_mem_alloc_mode_t mode, u64 size, void* old, u64 old_size) {
  si_mem_locked_arena_t* locked = (si_mem_locked_arena_t*)user_data;
  sp_mutex_lock(&locked->mutex);
  void* result = sp_mem_arena_on_alloc(locked->arena, mode, size, old, old_size);
  sp_mutex_unlock(&locked->mutex);
  return result;
}

void si_mem_locked_arena_init(si_mem_locked_arena_t* locked, sp_mem_t mem) {
  locked->arena = sp_mem_arena_new(mem);
  sp_mutex_init(&locked->mutex);
}

sp_mem_t si_mem_locked_arena_as_allocator(si_mem_locked_arena_t* locked) {
  return (sp_mem_t) {
    .on_alloc = on_alloc,
    .user_data = locked,
  };
}
