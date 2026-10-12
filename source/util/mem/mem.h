#ifndef SI_MEM_H
#define SI_MEM_H

#include "sp.h"

typedef struct {
  sp_mem_arena_t* arena;
  sp_mutex_t mutex;
} si_mem_locked_arena_t;

void     si_mem_locked_arena_init(si_mem_locked_arena_t* locked, sp_mem_t mem);
sp_mem_t si_mem_locked_arena_as_allocator(si_mem_locked_arena_t* locked);

#endif
