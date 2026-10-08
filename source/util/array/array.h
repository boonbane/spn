#ifndef SI_DA_H
#define SI_DA_H

#include "sp.h"

typedef struct SP_ALIGNED {
  u64 size;
  u64 capacity;
  sp_mem_t allocator;
} si_da_header_t;

#define si_da(T) T*

#define si_da_for(__ARR, __IT)  for (u64 __IT = 0; __IT < si_da_size((__ARR)); __IT++)
#define si_da_rfor(__ARR, __IT) for (u64 __IT = si_da_size(__ARR); __IT-- > 0; )

#define si_da_head(__ARR)\
  ((si_da_header_t*)((u8*)(__ARR) - sizeof(si_da_header_t)))

#define si_da_size(__ARR)\
  (__ARR ? si_da_head(__ARR)->size : 0)

#define si_da_stride(__ARR) \
  (sizeof(*(__ARR)))

#define si_da_capacity(__ARR)\
  ((__ARR) ? si_da_head(__ARR)->capacity : 0)

#define si_da_empty(__ARR)\
  (si_da_size(__ARR) == 0)

#define si_da_full(__ARR)\
  ((si_da_size(__ARR) == si_da_capacity(__ARR)))

#define si_da_clear(__ARR) \
  do {\
    if (__ARR) {\
      si_da_head(__ARR)->size = 0;\
    }\
  } while (0)

#define si_da_vp(__arr) \
  ((void**)&(__arr))

#define si_da_free(__mem, __arr)\
  do {\
    if (__arr) {\
      sp_assert(si_da_head(__arr)->allocator.on_alloc == (__mem).on_alloc && si_da_head(__arr)->allocator.user_data == (__mem).user_data);\
      sp_free((__mem), si_da_head(__arr), si_da_capacity(__arr) * si_da_stride(__arr) + sizeof(si_da_header_t));\
      (__arr) = SP_NULLPTR;\
    }\
  } while (0)

#define si_da_grow(__mem, __ARR, __N)\
  si_da_grow_ex((__mem), (__ARR), si_da_stride(__ARR), (__N))

#define si_da_push(__mem, __ARR, __VAL)\
  do {\
    *si_da_vp(__ARR) = si_da_grow((__mem), (__ARR), 1);\
    (__ARR)[si_da_head(__ARR)->size++] = (__VAL);\
  } while(0)

#define si_da_reserve(__mem, __arr, __n)\
  do {\
    if ((u64)(__n) > si_da_capacity(__arr)) {\
      *si_da_vp(__arr) = si_da_resize((__mem), (__arr), si_da_stride(__arr), (__n));\
    }\
  } while (0)

#define si_da_pop(__ARR)\
  do {\
    if (__ARR && !si_da_empty(__ARR)) {\
      si_da_head(__ARR)->size -= 1;\
    }\
  } while (0)

#define si_da_back(__ARR)\
  (__ARR + (si_da_size(__ARR) ? si_da_size(__ARR) - 1 : 0))

#define si_da_sort(arr, fn) sp_os_qsort(arr, si_da_size(arr), sizeof((arr)[0]), fn)
#define si_da_bounds_ok(arr, it) ((it) < si_da_size(arr))

void* si_da_resize(sp_mem_t mem, void* arr, u32 stride, u64 cap);

static inline void* si_da_grow_ex(sp_mem_t mem, void* arr, u32 stride, u64 n) {
  u64 required = si_da_size(arr) + n;
  if (arr && required <= si_da_capacity(arr)) {
    sp_assert(si_da_head(arr)->allocator.on_alloc == mem.on_alloc && si_da_head(arr)->allocator.user_data == mem.user_data);
    return arr;
  }
  return si_da_resize(mem, arr, stride, sp_max(si_da_capacity(arr) * 2, required));
}

#endif
