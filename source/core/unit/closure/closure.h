#ifndef SI_CLOSURE_H
#define SI_CLOSURE_H

#include "sp.h"
#include "spn/core.h"
#include "core/types.h"
#include "array/array.h"
#include "hash_table/hash_table.h"

typedef enum {
  SI_CLOSURE_STOP_AT_SHARED,
  SI_CLOSURE_CHASE_SHARED,
} si_closure_shared_t;

typedef enum {
  SI_CLOSURE_YIELD_PKG,
  SI_CLOSURE_YIELD_LIB,
} si_closure_yield_t;

typedef struct {
  si_closure_yield_t yield;
  spn_pkg_unit_t* pkg;
  spn_target_unit_t* lib;
  bool private;
} si_closure_entry_t;

typedef struct {
  spn_pkg_unit_t* pkg;
  spn_target_unit_t* lib;
  u32 edge;
  u32 child;
  bool private;
  bool root;
} si_closure_frame_t;

typedef struct {
  sp_mem_t mem;
  si_closure_shared_t shared;
  si_closure_entry_t entry;
  si_da(si_closure_frame_t) stack;
  si_ht(spn_pkg_unit_t*, u8) pkgs;
  si_ht(spn_target_unit_t*, u8) libs;
} si_closure_it_t;

si_closure_it_t si_closure_it(sp_mem_t mem, si_closure_shared_t shared);
void            si_closure_it_push(si_closure_it_t* it, spn_target_unit_t* root);
bool            si_closure_it_next(si_closure_it_t* it);

#endif
