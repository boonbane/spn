#include "unit/unit.h"

#include "unit/closure/closure.h"

spn_closure_t spn_closure_collect(sp_mem_t mem, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots) {
  return (spn_closure_t) {
    .root = root,
    .roots = roots,
  };
}
