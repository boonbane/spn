#include "closure.h"

#include "intern/intern.h"

sp_err_t expect_target_names(sp_test_t* t, si_da(spn_target_unit_t*) targets, const c8* const* expect) {
  sp_da(sp_str_t) names = sp_da_new(sp_test_arena(t), sp_str_t);
  si_da_for_value(targets, target) {
    sp_da_push(names, target->eval->info->name.str);
  }

  sp_must_strs_eq(t, names, sp_da_size(names), expect);
  return SP_OK;
}

spn_pkg_unit_t* find_pkg(spn_pkg_unit_t** pkgs, u32 count, const c8* name) {
  sp_intern_id_t needle = spn_intern_cstr(name).id;
  sp_for(it, count) {
    if (pkgs[it]->eval.manifest->name.id == needle) {
      return pkgs[it];
    }
  }
  return SP_NULLPTR;
}

spn_target_unit_t* find_lib(spn_pkg_unit_t** pkgs, u32 count, const c8* name) {
  sp_intern_id_t needle = spn_intern_cstr(name).id;
  sp_for(it, count) {
    si_da_for_value(pkgs[it]->libs, lib) {
      if (lib->eval->info->name.id == needle) {
        return lib;
      }
    }
  }
  return SP_NULLPTR;
}

static spn_target_eval_t* make_target(sp_mem_t mem, sp_intern_str_t name, spn_target_kind_t kind, bool no_link) {
  spn_target_info_t* decl = sp_alloc_type(mem, spn_target_info_t);
  decl->id = (spn_target_id_t) { .name = name.id, .kind = kind };
  decl->name = name;
  decl->kind = kind;
  decl->no_link = no_link;

  spn_target_eval_t* eval = sp_alloc_type(mem, spn_target_eval_t);
  eval->info = decl;
  return eval;
}

static spn_target_unit_t* add_lib(sp_mem_t mem, spn_pkg_unit_t* pkg, const c8* name, spn_linkage_t kind, bool no_link) {
  spn_target_unit_t* lib = sp_alloc_type(mem, spn_target_unit_t);
  lib->eval = make_target(mem, spn_intern_cstr(name), SPN_TARGET_KIND_LIB, no_link);
  lib->pkg = pkg;
  lib->lib_kind = kind;
  sp_da_init(mem, lib->deps);
  si_da_push(mem, pkg->libs, lib);
  return lib;
}

static spn_pkg_unit_t* build_pkg(sp_mem_t mem, spn_session_t* session, u32 id, closure_pkg_t spec) {
  spn_manifest_t* manifest = sp_alloc_type(mem, spn_manifest_t);
  manifest->name = spn_intern_cstr(spec.name);

  spn_pkg_unit_t* pkg = sp_alloc_type(mem, spn_pkg_unit_t);
  pkg->id.pkg.qualified = id;
  pkg->eval.manifest = manifest;
  pkg->session = session;

  if (spec.kind != SPN_LIB_KIND_NONE) {
    add_lib(mem, pkg, spec.name, spec.kind, false);
  }
  sp_carr_for(spec.libs, it) {
    if (!spec.libs[it].name) {
      break;
    }
    add_lib(mem, pkg, spec.libs[it].name, spec.libs[it].kind, spec.libs[it].no_link);
  }

  return pkg;
}

closure_graph_t build_graph(closure_graph_test_t* t) {
  closure_graph_t g = sp_zero;
  g.mem = sp_mem_os_new();
  spn_session_t* session = sp_alloc_type(g.mem, spn_session_t);
  session->mem = g.mem;

  sp_carr_for(t->pkgs, it) {
    if (!t->pkgs[it].name) {
      break;
    }
    g.pkgs[it] = build_pkg(g.mem, session, it + 1, t->pkgs[it]);
    g.count++;
  }

  sp_for(it, g.count) {
    sp_da(spn_pkg_dep_t) deps = sp_da_new(g.mem, spn_pkg_dep_t);
    sp_carr_for(t->pkgs[it].deps, d) {
      if (!t->pkgs[it].deps[d].name) {
        break;
      }
      sp_da_push(deps, ((spn_pkg_dep_t) {
        .unit = find_pkg(g.pkgs, g.count, t->pkgs[it].deps[d].name),
        .kind = t->pkgs[it].deps[d].kind,
        .private = t->pkgs[it].deps[d].private,
      }));
    }
    g.pkgs[it]->deps = deps;

    sp_carr_for(t->pkgs[it].libs, lt) {
      if (!t->pkgs[it].libs[lt].name) {
        break;
      }
      spn_target_unit_t* lib = find_lib(g.pkgs, g.count, t->pkgs[it].libs[lt].name);
      sp_carr_for(t->pkgs[it].libs[lt].deps, d) {
        if (!t->pkgs[it].libs[lt].deps[d]) {
          break;
        }
        sp_da_push(lib->deps, find_lib(g.pkgs, g.count, t->pkgs[it].libs[lt].deps[d]));
      }
    }
  }

  spn_pkg_unit_t* root = find_pkg(g.pkgs, g.count, t->root);
  spn_target_unit_t* exe = sp_alloc_type(g.mem, spn_target_unit_t);
  exe->pkg = root;
  exe->eval = make_target(g.mem, root->eval.manifest->name, t->target, false);
  exe->lib_kind = SPN_LIB_KIND_NONE;
  sp_da_init(g.mem, exe->deps);
  sp_carr_for(t->root_deps, it) {
    if (!t->root_deps[it]) {
      break;
    }
    sp_da_push(exe->deps, find_lib(g.pkgs, g.count, t->root_deps[it]));
  }

  g.root = exe;
  return g;
}
