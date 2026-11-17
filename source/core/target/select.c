#include "target/select.h"

#include "target/mutate.h"

spn_err_t spn_target_select_lib_kind(spn_linkage_set_t supported, spn_kind_query_t query, spn_linkage_t* kind) {

  if (query.config.some) {
    if (!spn_linkage_set_has(supported, query.config.value)) {
      return SPN_ERROR;
    }

    *kind = query.config.value;
    return SPN_OK;
  }

  // The profile's linkage is a constraint, not a preference: a static build promises no runtime
  // library dependencies, so shared libs are excluded outright. Within whatever is allowed,
  // static comes first.
  if (supported.static_lib) {
    *kind = SPN_LIB_KIND_STATIC;
    return SPN_OK;
  }
  if (supported.shared && query.linkage != SPN_LIB_KIND_STATIC) {
    *kind = SPN_LIB_KIND_SHARED;
    return SPN_OK;
  }
  return SPN_ERROR;
}
