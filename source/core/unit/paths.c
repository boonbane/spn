#include "unit/unit.h"

#include "ctx/types.h"
#include "paths/paths.h"

void spn_unit_paths_init(spn_pkg_unit_t* unit, spn_loaded_pkg_t* loaded) {
  spn_session_t* s = unit->session;
  sp_mem_t mem = s->mem;
  spn_build_unit_t* build = unit->build;

  unit->paths.roots = loaded->roots;

  switch (loaded->source) {
    case SPN_PKG_SOURCE_ROOT:
    case SPN_PKG_SOURCE_FILE: {
      spn_path_t work = spn_path_join(mem, build->paths.root, sp_str_lit(".spn"));
      spn_path_t store = spn_path_join(mem, build->paths.root, sp_str_lit("store"));
      unit->paths.work = spn_path_join(mem, work, loaded->info->name);
      unit->paths.store = spn_path_join(mem, store, loaded->info->name);
      break;
    }
    case SPN_PKG_SOURCE_INDEX: {
      sp_str_t fingerprint = spn_unit_fingerprint_str(mem, unit->fingerprint);
      spn_path_t work = spn_path_join(mem, spn_path_from_root(SPN_PATH_ROOT_BUILD), loaded->info->qualified);
      spn_path_t store = spn_path_join(mem, spn_path_from_root(SPN_PATH_ROOT_STORE), loaded->info->qualified);
      unit->paths.work = spn_path_join(mem, work, fingerprint);
      unit->paths.store = spn_path_join(mem, store, fingerprint);
      break;
    }
  }

  unit->paths.work = spn_path_anchor(mem, &spn.roots, unit->paths.work);
  unit->paths.store = spn_path_anchor(mem, &spn.roots, unit->paths.store);

  unit->paths.include = spn_path_join(mem, unit->paths.store, SP_LIT("include"));
  unit->paths.bin = spn_path_join(mem, unit->paths.store, SP_LIT("bin"));
  unit->paths.lib = spn_path_join(mem, unit->paths.store, SP_LIT("lib"));
  unit->paths.vendor = spn_path_join(mem, unit->paths.store, SP_LIT("vendor"));

  unit->paths.object = spn_path_join(mem, unit->paths.work, SP_LIT("object"));

  unit->paths.stamp = spn_path_join(mem, unit->paths.work, SP_LIT("stamp"));
}
