#include "unit/closure/closure.h"

#include "unit/types.h"

si_closure_it_t si_closure_it(sp_mem_t mem, si_closure_shared_t shared) {
  return (si_closure_it_t) {
    .mem = mem,
    .shared = shared,
  };
}

void si_closure_it_push(si_closure_it_t* it, spn_target_unit_t* root) {
}

bool si_closure_it_next(si_closure_it_t* it) {
  return false;
}
