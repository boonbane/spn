#ifndef SPN_SESSION_REGISTRY_TYPES_H
#define SPN_SESSION_REGISTRY_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "pkg/types.h"

typedef struct {
  spn_pkg_source_t source;
  const spn_manifest_t* manifest;
  spn_path_t path;
} spn_registry_pkg_t;

typedef sp_ht(spn_pkg_id_t, spn_registry_pkg_t) spn_pkg_registry_t;

#endif
