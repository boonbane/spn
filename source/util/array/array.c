#include "array/array.h"

static void* init(sp_mem_t mem, u32 stride, u64 cap) {
  si_da_header_t* header = (si_da_header_t*)sp_alloc_uninitialized(mem, cap * stride + sizeof(si_da_header_t));
  *header = (si_da_header_t) {
    .capacity = cap,
    .allocator = mem,
  };
  return header + 1;
}

void* si_da_resize(sp_mem_t mem, void* arr, u32 stride, u64 cap) {
  cap = sp_max(cap, 4);
  if (!arr) {
    return init(mem, stride, cap);
  }

  si_da_header_t* header = si_da_head(arr);
  sp_assert(header->allocator.on_alloc == mem.on_alloc && header->allocator.user_data == mem.user_data);

  u64 old_size = header->capacity * stride + sizeof(si_da_header_t);
  header = sp_cast(si_da_header_t*, sp_realloc_uninitialized(mem, header, old_size, cap * stride + sizeof(si_da_header_t)));
  header->capacity = cap;
  return header + 1;
}

void* si_da_copy_ex(sp_mem_t mem, const void* arr, u32 stride) {
  u64 size = si_da_size(arr);
  if (!size) {
    return SP_NULLPTR;
  }

  void* copy = init(mem, stride, size);
  sp_mem_copy(copy, arr, size * stride);
  si_da_head(copy)->size = size;
  return copy;
}
