#include "unit/unit.h"

#include "ctx/types.h"
#include "options/options.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "when/when.h"

spn_pkg_unit_t* spn_pkg_unit_ensure(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved) {
  spn_pkg_unit_id_t uid = { .pkg = resolved->id, .build = build->id };
  if (sp_om_has(u->packages, uid)) {
    return sp_om_get(u->packages, uid);
  }

  sp_mem_t mem = u->mem;
  spn_when_env_t env = sp_zero;
  spn_profile_env(mem, &build->profile, &env);
  spn_options_env_set(&env, &resolved->options.values);

  sp_om_insert(u->packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(u->packages);
  unit->id = uid;
  unit->build = build;
  unit->units = u;
  unit->resolved = resolved;
  unit->pkg = spn_pkg_eval(mem, resolved->manifest, &u->ctx->roots, resolved->roots, &env);
  if (resolved->origin.source.kind == SPN_PKG_ROOT_GIT) {
    unit->pin = (spn_source_pin_t) {
      .kind = SPN_PKG_ROOT_GIT,
      .rev = resolved->origin.source.git.rev,
      .dir = resolved->origin.source.git.dir,
      .patches = resolved->origin.source.git.patches.hash,
    };
  }
  sp_da_init(mem, unit->user_nodes);
  unit->fingerprint = spn_unit_fingerprint(u, build, resolved);

  unit->paths.roots = resolved->roots;
  switch (resolved->source) {
    case SPN_PKG_SOURCE_ROOT:
    case SPN_PKG_SOURCE_FILE: {
      spn_path_t work = spn_path_join(mem, build->paths.root, sp_str_lit(".spn"));
      spn_path_t store = spn_path_join(mem, build->paths.root, sp_str_lit("store"));
      unit->paths.work = spn_path_join(mem, work, resolved->manifest->name.str);
      unit->paths.store = spn_path_join(mem, store, resolved->manifest->name.str);
      break;
    }
    case SPN_PKG_SOURCE_INDEX: {
      sp_str_t fingerprint = spn_unit_fingerprint_str(mem, unit->fingerprint);
      spn_pkg_name_t name = spn_pkg_name_from_qualified(resolved->manifest->qualified.str);
      spn_path_t work = spn_path_s(mem, SPN_DIR_ID_BUILD, name.namespace);
      spn_path_t store = spn_path_s(mem, SPN_DIR_ID_STORE, name.namespace);
      unit->paths.work = spn_path_join(mem, spn_path_join(mem, work, name.name), fingerprint);
      unit->paths.store = spn_path_join(mem, spn_path_join(mem, store, name.name), fingerprint);
      break;
    }
  }
  unit->paths.work = spn_path_anchor(mem, &u->ctx->roots, unit->paths.work);
  unit->paths.store = spn_path_anchor(mem, &u->ctx->roots, unit->paths.store);
  unit->paths.include = spn_path_join(mem, unit->paths.store, SP_LIT("include"));
  unit->paths.share = spn_path_join(mem, unit->paths.store, SP_LIT("share"));
  unit->paths.bin = spn_path_join(mem, unit->paths.store, SP_LIT("bin"));
  unit->paths.lib = spn_path_join(mem, unit->paths.store, SP_LIT("lib"));
  unit->paths.vendor = spn_path_join(mem, unit->paths.store, SP_LIT("vendor"));
  return unit;
}
