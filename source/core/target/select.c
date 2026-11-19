#include "target/select.h"

#include "target/mutate.h"

spn_linkage_t spn_target_select_lib_kind(spn_linkage_set_t supported, spn_kind_query_t query) {
  if (query.config.some) {
    if (!spn_linkage_set_has(supported, query.config.value)) {
      return SPN_LIB_KIND_NONE;
    }
    if (query.config.value == SPN_LIB_KIND_STATIC || query.loader) {
      return query.config.value;
    }
  }

  if (supported.static_lib) {
    return SPN_LIB_KIND_STATIC;
  }
  if (supported.shared && query.linkage != SPN_LIB_KIND_STATIC) {
    return SPN_LIB_KIND_SHARED;
  }
  return SPN_LIB_KIND_NONE;
}
