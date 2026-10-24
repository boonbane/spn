#ifndef SPN_TEST_MANIFEST_H
#define SPN_TEST_MANIFEST_H

#include "intern/intern.h"
#include "pkg/types.h"
#include "target/types.h"

static inline si_pkg_info_t test_manifest_make(sp_str_t name) {
  si_pkg_info_t manifest = sp_zero;
  manifest.name = spn_intern(name);
  return manifest;
}

static inline si_target_info_t* test_manifest_target(sp_mem_t mem, si_pkg_info_t* manifest, sp_str_t name, spn_target_kind_t kind) {
  sp_intern_str_t interned = spn_intern(name);
  si_target_id_t id = { .name = interned.id, .kind = kind };
  si_om_insert(mem, manifest->targets, id, ((si_target_info_t) {
    .id = id,
    .name = interned,
    .kind = kind,
  }));
  return si_om_back(manifest->targets);
}

#endif
