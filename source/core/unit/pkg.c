#include "unit/unit.h"

#include "ctx/types.h"
#include "options/options.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "session/session.h"
#include "when/when.h"

spn_pkg_unit_t* spn_pkg_unit_ensure(spn_session_t* s, spn_build_unit_t* build, spn_pkg_id_t id) {
  spn_pkg_unit_id_t uid = { .pkg = id, .build = build->id };
  if (sp_om_has(s->units.packages, uid)) {
    return sp_om_get(s->units.packages, uid);
  }

  spn_loaded_pkg_t* loaded = sp_ht_getp(s->packages, id);
  sp_assert(loaded);
  spn_resolved_pkg_t* resolved = sp_ht_getp(s->resolve, id);
  sp_assert(resolved);

  sp_mem_t mem = s->mem;
  spn_when_env_t env = sp_zero;
  spn_profile_env(mem, &build->profile, &env);
  spn_resolved_options_t* options = sp_ht_getp(s->options, id);
  if (options) {
    spn_options_env_set(&env, options);
  }

  sp_om_insert(s->units.packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(s->units.packages);
  unit->id = uid;
  unit->build = build;
  unit->session = s;
  unit->source = loaded->source;
  unit->pkg = spn_pkg_eval(mem, loaded->manifest, &s->ctx->roots, loaded->roots, &env);
  if (resolved->origin.source.kind == SPN_PKG_ROOT_GIT) {
    unit->pin = (spn_source_pin_t) {
      .kind = SPN_PKG_ROOT_GIT,
      .rev = resolved->origin.source.git.rev,
      .dir = resolved->origin.source.git.dir,
      .patches = resolved->origin.source.git.patches.hash,
    };
  }
  sp_da_init(mem, unit->user_nodes);
  unit->fingerprint = spn_unit_fingerprint(s, build, id);

  unit->paths.roots = loaded->roots;
  switch (loaded->source) {
    case SPN_PKG_SOURCE_ROOT:
    case SPN_PKG_SOURCE_FILE: {
      spn_path_t work = spn_path_join(mem, build->paths.root, sp_str_lit(".spn"));
      spn_path_t store = spn_path_join(mem, build->paths.root, sp_str_lit("store"));
      unit->paths.work = spn_path_join(mem, work, loaded->manifest->name.str);
      unit->paths.store = spn_path_join(mem, store, loaded->manifest->name.str);
      break;
    }
    case SPN_PKG_SOURCE_INDEX: {
      sp_str_t fingerprint = spn_unit_fingerprint_str(mem, unit->fingerprint);
      spn_pkg_name_t name = spn_pkg_name_from_qualified(loaded->manifest->qualified.str);
      spn_path_t work = spn_path_s(mem, SPN_DIR_ID_BUILD, name.namespace);
      spn_path_t store = spn_path_s(mem, SPN_DIR_ID_STORE, name.namespace);
      unit->paths.work = spn_path_join(mem, spn_path_join(mem, work, name.name), fingerprint);
      unit->paths.store = spn_path_join(mem, spn_path_join(mem, store, name.name), fingerprint);
      break;
    }
  }
  unit->paths.work = spn_path_anchor(mem, &s->ctx->roots, unit->paths.work);
  unit->paths.store = spn_path_anchor(mem, &s->ctx->roots, unit->paths.store);
  unit->paths.include = spn_path_join(mem, unit->paths.store, SP_LIT("include"));
  unit->paths.share = spn_path_join(mem, unit->paths.store, SP_LIT("share"));
  unit->paths.bin = spn_path_join(mem, unit->paths.store, SP_LIT("bin"));
  unit->paths.lib = spn_path_join(mem, unit->paths.store, SP_LIT("lib"));
  unit->paths.vendor = spn_path_join(mem, unit->paths.store, SP_LIT("vendor"));
  return unit;
}
