#include "core/types.h"
#include "unit/unit.h"

#include "sp.h"
#include "macro/macro.h"
#include "str/str.h"

#include "ctx/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "error/error.h"
#include "enum/enum.h"
#include "external/wasm/wasm.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "target/mutate.h"
#include "pkg/pkg.h"
#include "session/invocation.h"
#include "session/session.h"
#include "target/select.h"
#include "graph/build.h"
#include "profile/types.h"
#include "toolchain/linker.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

static spn_err_t ensure_target_unit(spn_session_t* s, spn_pkg_unit_t* pkg, spn_target_info_t* info, spn_target_unit_t** result) {
  spn_target_unit_id_t id = {
    .pkg = pkg->id,
    .target = { .name = spn_intern(info->name).id, .kind = info->kind },
  };
  spn_target_unit_t* target = spn_session_find_target_in_pkg(s, pkg, id.target);
  if (!target) {
    sp_om_insert(s->units.targets, id, SP_ZERO_STRUCT(spn_target_unit_t));
    target = sp_om_back(s->units.targets);
    target->id = id;
    target->pkg = pkg;
    target->info = info;
    sp_da_init(s->mem, target->objects);
    sp_da_init(s->mem, target->deps);

    switch (info->kind) {
      case SPN_TARGET_KIND_LIB: {
        sp_da_push(pkg->targets, target);
        sp_da_push(pkg->libs, target);

        if (spn_linkage_set_has(info->linkages, SPN_LIB_KIND_OBJECT) || info->no_link) {
          target->lib_kind = spn_linkage_set_default(info->linkages);
        }
        else {
          const spn_profile_info_t* profile = &pkg->build->profile;
          spn_kind_query_t query = {
            .config = spn_session_config_kind(s, pkg->info->name),
            .linkage = profile->linking.linkage,
          };

          if (spn_target_select_lib_kind(info, query, &target->lib_kind)) {
            spn_linkage_requester_t requester = SPN_LINKAGE_REQUESTER_TARGET;
            if (profile->request.linkage) {
              requester = SPN_LINKAGE_REQUESTER_PROFILE;
            }
            else if (profile->request.libc == SPN_RUNTIME_STATIC) {
              requester = SPN_LINKAGE_REQUESTER_LIBC;
            }
            sp_da(spn_linkage_t) supported = sp_da_new(s->mem, spn_linkage_t);
            if (info->linkages.shared) {
              sp_da_push(supported, SPN_LIB_KIND_SHARED);
            }
            if (info->linkages.static_lib) {
              sp_da_push(supported, SPN_LIB_KIND_STATIC);
            }
            if (info->linkages.source) {
              sp_da_push(supported, SPN_LIB_KIND_SOURCE);
            }
            if (info->linkages.object) {
              sp_da_push(supported, SPN_LIB_KIND_OBJECT);
            }
            return spn_err_emit(s->ctx, (spn_err_union_t) {
              .kind = SPN_ERR_TARGET_LINKAGE,
              .target = {
                .pkg = pkg->info->name,
                .name = info->name,
                .requested = spn_linkage_to_str(query.config.some ? query.config.value : query.linkage),
                .requester = query.config.some ? SPN_LINKAGE_REQUESTER_ROOT_MANIFEST : requester,
                .supported = supported,
              },
            });
          }
        }

        switch (target->lib_kind) {
          case SPN_LIB_KIND_STATIC: target->kind = SPN_CC_OUTPUT_STATIC_LIB; break;
          case SPN_LIB_KIND_SHARED: target->kind = SPN_CC_OUTPUT_SHARED_LIB; break;
          case SPN_LIB_KIND_SOURCE:
          case SPN_LIB_KIND_OBJECT: target->kind = SPN_CC_OUTPUT_OBJECT; break;
          case SPN_LIB_KIND_NONE: break;
        }
        break;
      }
      case SPN_TARGET_KIND_EXE:
      case SPN_TARGET_KIND_SCRIPT:
      case SPN_TARGET_KIND_TEST:
      case SPN_TARGET_KIND_EXAMPLE: {
        sp_da_push(pkg->targets, target);
        target->kind = SPN_CC_OUTPUT_EXE;
        break;
      }
      case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM: {
        sp_assert(pkg->build == s->units.metaprogram);
        pkg->scripts.configure = target;
        target->kind = SPN_CC_OUTPUT_REACTOR;
        break;
      }
      case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
        sp_assert(pkg->build == s->units.metaprogram);
        pkg->scripts.build = target;
        sp_da_push(pkg->targets, target);
        target->kind = SPN_CC_OUTPUT_REACTOR;
        break;
      }
    }

    if (target->lib_kind == SPN_LIB_KIND_OBJECT) {
      target->paths.object = pkg->paths.lib;
    }
    else {
      sp_str_t dir = sp_zero;
      switch (info->kind) {
        case SPN_TARGET_KIND_LIB:                   dir = sp_str_lit("lib"); break;
        case SPN_TARGET_KIND_EXE:                   dir = sp_str_lit("exe"); break;
        case SPN_TARGET_KIND_SCRIPT:                dir = sp_str_lit("script"); break;
        case SPN_TARGET_KIND_TEST:                  dir = sp_str_lit("test"); break;
        case SPN_TARGET_KIND_EXAMPLE:               dir = sp_str_lit("example"); break;
        case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM: dir = sp_str_lit("configure"); break;
        case SPN_TARGET_KIND_BUILD_METAPROGRAM:     dir = sp_str_lit("build"); break;
      }
      sp_str_buf_t buf = sp_zero;
      spn_path_t kind = spn_path_join(sp_str_buf_as_mem(&buf), pkg->paths.object, dir);
      target->paths.object = spn_path_join(s->mem, kind, info->name);
    }

    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    spn_triple_t triple = spn_profile_triple(&pkg->build->profile);
    switch (target->kind) {
      case SPN_CC_OUTPUT_EXE: {
        target->paths.output = spn_path_join(s->mem, pkg->paths.bin, spn_triple_exe_file_name(scratch.mem, triple, info->name));
        break;
      }
      case SPN_CC_OUTPUT_STATIC_LIB: {
        target->paths.output = spn_path_join(s->mem, pkg->paths.lib, spn_triple_lib_file_name(scratch.mem, triple, info->name, SP_OS_LIB_STATIC));
        break;
      }
      case SPN_CC_OUTPUT_SHARED_LIB: {
        target->paths.output = spn_path_join(s->mem, pkg->paths.lib, spn_triple_lib_file_name(scratch.mem, triple, info->name, SP_OS_LIB_SHARED));
        break;
      }
      case SPN_CC_OUTPUT_REACTOR: {
        target->paths.output = spn_path_join(s->mem, pkg->paths.work, sp_fmt(scratch.mem, "{}.wasm", sp_fmt_str(info->name)).value);
        break;
      }
      case SPN_CC_OUTPUT_OBJECT: {
        break;
      }
    }
    sp_mem_end_scratch(scratch);
  }
  if (result) *result = target;
  return SPN_OK;
}

static s32 compare_objects(const void* a, const void* b) {
  return sp_str_compare_alphabetical((*(spn_compile_unit_t* const*)a)->paths.file.sub, (*(spn_compile_unit_t* const*)b)->paths.file.sub);
}

static void add_object(spn_session_t* s, spn_target_unit_t* target, spn_path_t file) {
  spn_compile_unit_id_t id = {
    .target = target->id,
    .source = { .root = file.root, .sub = sp_intern(s->ctx->intern, file.sub).id },
  };
  if (sp_om_has(s->units.objects, id)) {
    return;
  }

  spn_path_t dir = target->paths.object;
  spn_tree_rel_t rel = spn_tree_rel(target->pkg->paths.roots, file);
  sp_str_t prefix = rel.tree == SPN_TREE_NONE ? spn_path_root_label(file.root) : spn_tree_to_str(rel.tree);
  sp_om_insert(s->units.objects, id, ((spn_compile_unit_t) {
    .id = id,
    .target = target,
    .lang = spn_lang_from_path(rel.sub),
    .paths = {
      .file = { .root = file.root, .sub = sp_intern_find(s->ctx->intern, id.source.sub) },
      .object = {
        .root = dir.root,
        .sub = sp_fmt(s->mem, "{}/{}/{}.o",
          sp_fmt_str(dir.sub),
          sp_fmt_str(prefix),
          sp_fmt_str(rel.sub)
        ).value
      },
    },
  }));
  sp_da_push(target->objects, sp_om_back(s->units.objects));
}

static spn_err_t create_target_objects(spn_session_t* s, spn_target_unit_t* target) {
  si_da_for(target->info->source, it) {
    spn_source_t source = target->info->source[it];
    switch (source.kind) {
      case SPN_SOURCE_FILE: {
        add_object(s, target, source.path);
        break;
      }
      case SPN_SOURCE_GLOB: {
        u64 first = sp_da_size(target->objects);
        sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
        spn_dag_glob_it_t glob = spn_dag_glob_it_new(scratch.mem, &s->ctx->roots, source.path);
        while (spn_dag_glob_it_next(&glob)) {
          if (glob.entry.kind != SP_FS_KIND_DIR) {
            add_object(s, target, spn_path_join(scratch.mem, glob.base, glob.entry.rel));
          }
        }
        spn_dag_glob_it_deinit(&glob);
        sp_mem_end_scratch(scratch);
        if (glob.err) {
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = SPN_ERR_TARGET_SOURCE_GLOB,
            .target_source = {
              .pkg = target->pkg->info->name,
              .name = target->info->name,
              .source = spn_path_str(&s->ctx->roots, s->mem, source.path),
            },
          });
        }
        sp_os_qsort(target->objects + first, sp_da_size(target->objects) - first, sizeof(*target->objects), compare_objects);
        break;
      }
    }
  }
  return SPN_OK;
}

static spn_os_version_t max_os_version(spn_os_version_t current, spn_os_version_t candidate) {
  if (current.major != candidate.major) {
    return current.major < candidate.major ? candidate : current;
  }
  return current.minor < candidate.minor ? candidate : current;
}

static bool is_any_object_cxx(sp_da(spn_compile_unit_t*) objects) {
  sp_da_for(objects, it) {
    if (objects[it]->lang == SPN_LANG_CXX) {
      return true;
    }
  }
  return false;
}

typedef sp_str_ht(u8) link_str_set_t;

static void push_unique(sp_mem_t mem, link_str_set_t* seen, si_da(sp_str_t)* result, si_da(sp_str_t) values) {
  si_da_for(values, it) {
    if (sp_str_ht_exists(*seen, values[it])) {
      continue;
    }
    sp_str_ht_insert(*seen, values[it], (u8)true);
    si_da_push(mem, *result, values[it]);
  }
}

static spn_err_t build_target_plan(spn_target_unit_t* target) {
  spn_pkg_unit_t* pkg = target->pkg;
  spn_session_t* s = pkg->session;
  spn_build_unit_t* build = pkg->build;
  spn_target_info_t* info = target->info;
  spn_profile_info_t* profile = &build->profile;
  spn_cc_t* toolchain = &build->toolchain->cc;
  sp_mem_t mem = s->mem;

  spn_target_plan_t* plan = SP_NULLPTR;
  sp_om_emplace(s->plans.targets, target->id, plan);

  si_da_for(info->configured.include, it) {
    si_da_push(mem, plan->include, info->configured.include[it]);
  }
  si_da_for(pkg->info->configured.include, it) {
    si_da_push(mem, plan->include, pkg->info->configured.include[it]);
  }
  sp_da_for(build->include, it) {
    si_da_push(mem, plan->include, build->include[it]);
  }
  si_da_for(pkg->info->include, it) {
    si_da_push(mem, plan->include, pkg->info->include[it]);
  }
  si_da_for(info->include, it) {
    si_da_push(mem, plan->include, info->include[it]);
  }
  if (info->kind == SPN_TARGET_KIND_EXAMPLE) {
    si_da_push(mem, plan->include, pkg->paths.include);
  }
  sp_da_for(pkg->deps, it) {
    if (!spn_dep_kind_applies(pkg->deps[it].kind, info->kind)) {
      continue;
    }
    si_da_push(mem, plan->include, pkg->deps[it].unit->paths.include);
  }
  if (!si_da_empty(info->embed)) {
    si_da_push(mem, plan->include, target->paths.object);
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(spn_closure_entry_t) closure = si_link_get_target_closure(scratch.mem, target);
  sp_assert(closure[0].pkg == pkg);

  plan->link = (spn_link_plan_t) {
    .libs = si_link_get_closure_libs(mem, closure),
    .cc = {
      .pkg = pkg->info->name,
      .name = info->name,
      .kind = target->kind,
      .lang = is_any_object_cxx(target->objects) ? SPN_LANG_CXX : SPN_LANG_C,
      .min_os = info->macos.min_os,
      .subsystem = info->windows.subsystem,
    },
  };
  spn_link_plan_t* link = &plan->link;
  si_da_for(link->libs, it) {
    spn_target_unit_t* lib = link->libs[it].lib;
    if (lib->lib_kind == SPN_LIB_KIND_STATIC && is_any_object_cxx(lib->objects)) {
      link->cc.lang = SPN_LANG_CXX;
      break;
    }
  }

  link_str_set_t frameworks;
  sp_str_ht_init(scratch.mem, frameworks);
  link_str_set_t system_libs;
  sp_str_ht_init(scratch.mem, system_libs);
  push_unique(mem, &frameworks, &link->cc.frameworks, info->macos.frameworks);
  push_unique(mem, &system_libs, &link->cc.system_libs, info->system_deps);
  sp_da_for(closure, it) {
    spn_closure_entry_t* entry = &closure[it];
    link->cc.min_os = max_os_version(link->cc.min_os, entry->pkg->info->macos.min_os);
    if (entry->links_code) {
      push_unique(mem, &frameworks, &link->cc.frameworks, entry->pkg->info->macos.frameworks);
    }
    push_unique(mem, &system_libs, &link->cc.system_libs, entry->pkg->info->system_deps);
    sp_da_for(entry->targets, lt) {
      spn_target_unit_t* lib = entry->targets[lt];
      link->cc.min_os = max_os_version(link->cc.min_os, lib->info->macos.min_os);
      if (lib->info->no_link || lib->lib_kind == SPN_LIB_KIND_SHARED) {
        continue;
      }
      push_unique(mem, &frameworks, &link->cc.frameworks, lib->info->macos.frameworks);
      push_unique(mem, &system_libs, &link->cc.system_libs, lib->info->system_deps);
    }
  }

  switch (target->kind) {
    case SPN_CC_OUTPUT_EXE: {
      link->cc.args = info->link_flags;
      link->cc.scripts = info->linker_script;
      break;
    }
    case SPN_CC_OUTPUT_REACTOR: {
      link->cc.args = info->link_flags;
      link->cc.scripts = info->linker_script;
      link->cc.exports = spn_target_exports_path(mem, target);
      break;
    }
    case SPN_CC_OUTPUT_SHARED_LIB: {
      link->cc.args = info->link_flags;
      link->cc.scripts = info->linker_script;
      link->cc.exports = spn_target_exports_path(mem, target);
      spn_triple_t triple = spn_profile_triple(profile);
      if (spn_ld_dialect(triple) == SPN_LD_DIALECT_LINK) {
        link->cc.implib = spn_path_join(mem, pkg->paths.lib, spn_triple_lib_file_name(scratch.mem, triple, info->name, SP_OS_LIB_STATIC));
      }
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }

  bool dynamic = target->kind == SPN_CC_OUTPUT_SHARED_LIB || target->kind == SPN_CC_OUTPUT_REACTOR;
  si_da_for(link->libs, it) {
    spn_link_lib_t* lib = &link->libs[it];
    if (lib->lib->info->no_link) {
      continue;
    }
    switch (lib->lib->lib_kind) {
      case SPN_LIB_KIND_SHARED: {
        si_da_push(mem, link->cc.lib_dirs, lib->lib->pkg->paths.lib);
        si_da_push(mem, link->cc.libs, lib->lib->info->name);
        break;
      }
      case SPN_LIB_KIND_STATIC: {
        if (!dynamic) {
          si_da_push(mem, link->cc.lib_dirs, lib->lib->pkg->paths.lib);
          si_da_push(mem, link->cc.libs, lib->lib->info->name);
        }
        else if (lib->private) {
          si_da_push(mem, link->cc.lib_dirs, lib->lib->pkg->paths.lib);
          si_da_push(mem, link->cc.private_libs, lib->lib->info->name);
        }
        else {
          si_da_push(mem, link->cc.whole_archives, lib->lib->paths.output);
        }
        break;
      }
      case SPN_LIB_KIND_SOURCE:
      case SPN_LIB_KIND_OBJECT:
      case SPN_LIB_KIND_NONE: {
        break;
      }
    }
  }

  spn_cc_compile_t compile = {
    .cxx = info->cxx,
    .pic = info->kind == SPN_TARGET_KIND_LIB && spn_triple_pic(spn_profile_triple(profile)),
    .include = plan->include,
  };
  if (profile->os == SPN_OS_MACOS) {
    compile.min_os = link->cc.min_os;
  }
  sp_da_for(build->define, it) {
    si_da_push(scratch.mem, compile.define, build->define[it]);
  }
  si_da_for(pkg->info->define, it) {
    si_da_push(scratch.mem, compile.define, pkg->info->define[it]);
  }
  si_da_for(info->define, it) {
    si_da_push(scratch.mem, compile.define, info->define[it]);
  }
  si_da_for(info->flags, it) {
    si_da_push(scratch.mem, compile.args, info->flags[it]);
  }
  sp_da_for(pkg->deps, it) {
    if (!spn_dep_kind_applies(pkg->deps[it].kind, info->kind)) {
      continue;
    }
    si_da_for(pkg->deps[it].unit->info->public_define, jt) {
      si_da_push(scratch.mem, compile.define, pkg->deps[it].unit->info->public_define[jt]);
    }
  }
  sp_da_for(target->objects, it) {
    spn_compile_unit_t* unit = target->objects[it];
    spn_invocation_t* invocation = SP_NULLPTR;
    sp_om_emplace(s->plans.objects, unit->id, invocation);

    compile.lang = unit->lang;
    spn_cc_render_compile(mem, toolchain, profile, &compile, invocation);
    invocation->cwd = pkg->paths.work;
  }
  sp_mem_end_scratch(scratch);

  switch (target->kind) {
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      return spn_cc_validate_link(toolchain, spn.host, profile, &link->cc);
    }
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_OBJECT: {
      return SPN_OK;
    }
  }

  sp_unreachable_return(SPN_ERROR);
}

static spn_pkg_unit_t* find_dep_unit(spn_session_t* s, spn_pkg_unit_t* pkg, sp_str_t qualified) {
  const spn_dep_kind_t kinds [] = { SPN_DEP_KIND_PACKAGE, SPN_DEP_KIND_TEST, SPN_DEP_KIND_BUILD };
  sp_carr_for(kinds, it) {
    spn_pkg_unit_t* unit = spn_session_find_dep(s, pkg, qualified, kinds[it]);
    if (unit) {
      return unit;
    }
  }
  return SP_NULLPTR;
}

static void collect_unit_targets(sp_da(spn_target_unit_t*)* targets, sp_da(spn_pkg_unit_t*) units) {
  sp_da_for(units, it) {
    spn_pkg_unit_t* pkg = units[it];
    sp_da_for(pkg->targets, jt) {
      sp_da_push(*targets, pkg->targets[jt]);
    }
  }
}

static spn_err_t ensure_sibling_targets(spn_session_t* s, sp_da(spn_target_unit_t*)* targets) {
  sp_for(it, sp_da_size(*targets)) {
    spn_target_unit_t* unit = (*targets)[it];
    si_da_for(unit->info->deps, jt) {
      sp_str_t qualified = spn_pkg_canonicalize_name(unit->info->deps[jt]);
      if (find_dep_unit(s, unit->pkg, qualified)) {
        continue;
      }
      if (spn_session_find_target_in_pkg(s, unit->pkg, ((spn_target_key_t) { .name = spn_intern(unit->info->deps[jt]).id, .kind = SPN_TARGET_KIND_LIB }))) {
        continue;
      }
      spn_target_info_t* info = spn_pkg_get_target(unit->pkg->info, unit->info->deps[jt], SPN_TARGET_KIND_LIB);
      if (!info) {
        continue;
      }
      spn_target_unit_t* target = SP_NULLPTR;
      spn_try(ensure_target_unit(s, unit->pkg, info, &target));
      sp_da_push(*targets, target);
    }
  }
  return SPN_OK;
}

static spn_err_t resolve_target_deps(spn_session_t* s, sp_da(spn_target_unit_t*) targets) {
  sp_da_for(targets, it) {
    spn_target_unit_t* unit = targets[it];
    si_da_for(unit->info->deps, jt) {
      sp_str_t qualified = spn_pkg_canonicalize_name(unit->info->deps[jt]);
      if (find_dep_unit(s, unit->pkg, qualified)) {
        continue;
      }

      spn_target_unit_t* target = spn_session_find_target_in_pkg(s, unit->pkg, ((spn_target_key_t) { .name = spn_intern(unit->info->deps[jt]).id, .kind = SPN_TARGET_KIND_LIB }));
      if (!target) {
        return spn_err_emit(s->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_TARGET_DEP,
          .target = { .name = unit->info->deps[jt] },
        });
      }
      sp_da_push(unit->deps, target);
    }
  }
  return SPN_OK;
}

static void init_wasm_scripts(spn_session_t* s) {
  sp_om_for(s->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(s->units.packages, it);
    if (!unit->metaprogram) {
      continue;
    }
    if (unit->metaprogram->scripts.configure) {
      spn_wasm_script_init(&unit->wasm.configure, unit->metaprogram->scripts.configure->paths.output);
    }
    if (unit->metaprogram->scripts.build) {
      spn_wasm_script_init(&unit->wasm.build, unit->metaprogram->scripts.build->paths.output);
    }
  }
}

static spn_err_t add_metaprogram_targets(spn_session_t* s) {
  spn_build_unit_t* world = s->units.metaprogram;

  sp_da_for(world->packages, it) {
    spn_pkg_unit_t* unit = world->packages[it];
    spn_loaded_pkg_t* loaded = sp_ht_getp(s->packages, unit->id.pkg);
    if (!si_da_empty(loaded->configure.source)) {
      spn_try(ensure_target_unit(s, unit, &loaded->configure, SP_NULLPTR));
    }
    if (!si_da_empty(loaded->build.source)) {
      spn_try(ensure_target_unit(s, unit, &loaded->build, SP_NULLPTR));
    }
  }

  sp_da_for(world->packages, it) {
    spn_pkg_unit_t* unit = world->packages[it];
    if (spn_pkg_unit_is_script_host(unit)) {
      continue;
    }
    si_om_for(unit->info->targets, jt) {
      spn_target_info_t* info = si_om_at(unit->info->targets, jt);
      if (info->kind != SPN_TARGET_KIND_LIB) {
        continue;
      }
      spn_try(ensure_target_unit(s, unit, info, SP_NULLPTR));
    }
  }

  sp_da(spn_target_unit_t*) targets = sp_da_new(s->mem, spn_target_unit_t*);
  collect_unit_targets(&targets, world->packages);
  spn_try(ensure_sibling_targets(s, &targets));
  spn_try(resolve_target_deps(s, targets));

  sp_da_for(targets, it) {
    if (targets[it]->lib_kind == SPN_LIB_KIND_SOURCE) {
      continue;
    }
    spn_try(create_target_objects(s, targets[it]));
  }
  sp_da_for(world->packages, it) {
    spn_target_unit_t* configure = world->packages[it]->scripts.configure;
    if (configure) {
      spn_try(create_target_objects(s, configure));
      sp_assert(!sp_da_empty(configure->objects));
    }
  }

  sp_da_for(targets, it) {
    spn_try(build_target_plan(targets[it]));
  }
  sp_da_for(world->packages, it) {
    spn_target_unit_t* configure = world->packages[it]->scripts.configure;
    if (configure) {
      spn_try(build_target_plan(configure));
    }
  }

  init_wasm_scripts(s);
  return SPN_OK;
}

static spn_err_t add_target_units(spn_session_t* s) {
  sp_da_for(s->plans.build, it) {
    spn_build_plan_t* plan = &s->plans.build[it];
    sp_da_for(plan->build->packages, jt) {
      spn_pkg_unit_t* pkg = plan->build->packages[jt];

      if (pkg == plan->root) {
        spn_target_selection_t* selection = &plan->selection;
        si_om_for(pkg->info->targets, kt) {
          spn_target_info_t* info = si_om_at(pkg->info->targets, kt);
          if (!(selection->kinds & spn_target_kind_bit(info->kind))) {
            continue;
          }
          bool selected = !selection->names.count;
          sp_for(lt, selection->names.count) {
            if (sp_str_equal(selection->names.items[lt], info->name)) {
              selected = true;
              break;
            }
          }
          if (!selected) {
            continue;
          }
          spn_target_unit_t* target = SP_NULLPTR;
          spn_try(ensure_target_unit(s, pkg, info, &target));
          sp_da_push(plan->roots, target->id);
        }

        sp_for(kt, selection->names.count) {
          sp_str_t name = selection->names.items[kt];
          bool matched = false;
          sp_da_for(plan->roots, lt) {
            if (sp_str_equal(spn_session_get_target_unit(s, plan->roots[lt])->info->name, name)) {
              matched = true;
              break;
            }
          }
          if (!matched) {
            return spn_err_emit(s->ctx, (spn_err_union_t) {
              .kind = SPN_ERR_TARGET_SELECTION,
              .target = { .name = name },
            });
          }
        }
      } else {
        si_om_for(pkg->info->targets, kt) {
          spn_target_info_t* info = si_om_at(pkg->info->targets, kt);
          if (info->kind != SPN_TARGET_KIND_LIB) {
            continue;
          }
          spn_try(ensure_target_unit(s, pkg, info, SP_NULLPTR));
        }
      }
    }
  }

  sp_da_for(s->plans.build, it) {
    spn_build_unit_t* world = s->plans.build[it].build;

    sp_om_for(s->units.targets, jt) {
      spn_target_unit_t* target = sp_om_at(s->units.targets, jt);

      // @spader
      // This is a hack. All we're really asking here is whether the unit
      // belongs to the metabuild or the build. The right fix is to stop
      // treating the metabuild as a special case, but I'm punting.
      if (target->pkg->build != world) {
        continue;
      }

      si_da_for(target->info->deps, kt) {
        sp_str_t name = target->info->deps[kt];

        // @review Fake defensive code or real?
        // Even if a real program state, is our code factored correctly? Like,
        // can we reorder the code so that any creation happens up front?
        if (find_dep_unit(s, target->pkg, spn_pkg_canonicalize_name(name))) {
          continue;
        }

        spn_target_info_t* info = spn_pkg_get_target(target->pkg->info, name, SPN_TARGET_KIND_LIB);

        // @review Fake defensive code or real?
        if (!info) {
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = SPN_ERR_TARGET_DEP,
            .target = { .name = name },
          });
        }
        spn_target_unit_t* dep = SP_NULLPTR;
        spn_try(ensure_target_unit(s, target->pkg, info, &dep));
        sp_da_push(target->deps, dep);
      }
      if (target->lib_kind == SPN_LIB_KIND_SOURCE) {
        continue;
      }
      spn_try(create_target_objects(s, target));
      if (is_any_object_cxx(target->objects) && spn_arg_empty(world->toolchain->cc.cxx.program)) {
        return spn_err_emit(s->ctx, (spn_err_union_t) { .kind = SPN_ERR_TOOLCHAIN_NO_CXX, .toolchain = { .name = world->toolchain->info->name } });
      }
    }

    sp_om_for(s->units.targets, jt) {
      spn_target_unit_t* target = sp_om_at(s->units.targets, jt);
      if (target->pkg->build != world) {
        continue;
      }
      spn_try(build_target_plan(target));
    }
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da_for(s->plans.build, it) {
    spn_build_plan_t* plan = &s->plans.build[it];
    sp_ht(spn_path_t, spn_target_unit_t*) claimed = SP_NULLPTR;
    sp_ht_init(scratch.mem, claimed);
    sp_ht_set_fns(claimed, spn_path_on_hash, spn_path_on_compare);
    sp_da_for(plan->roots, jt) {
      spn_target_unit_t* root = spn_session_get_target_unit(s, plan->roots[jt]);
      if (root->kind != SPN_CC_OUTPUT_EXE) {
        continue;
      }
      spn_stage_closure_t closure = {
        .exe = { .target = root, .path = spn_target_unit_staged_path(s->mem, root) },
      };
      sp_da_init(s->mem, closure.libs);

      spn_path_t dir = spn_path_parent(closure.exe.path);
      sp_da(spn_target_unit_t*) libs = si_link_get_target_runtime_libs(scratch.mem, root);
      sp_da_for(libs, lt) {
        spn_target_unit_t* lib = libs[lt];
        sp_str_t name = sp_fs_get_name(lib->paths.output.sub);
        spn_path_t path = spn_path_join(s->mem, dir, name);
        spn_target_unit_t** owner = sp_ht_getp(claimed, path);
        if (owner && *owner != lib) {
          sp_mem_end_scratch(scratch);
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = SPN_ERR_TARGET_COLLISION,
            .collision = {
              .exe = root->info->name,
              .pkg = (*owner)->pkg->info->name,
              .other = lib->pkg->info->name,
              .name = name,
            },
          });
        }
        sp_ht_insert(claimed, path, lib);
        sp_da_push(closure.libs, ((spn_stage_entry_t) { .target = lib, .path = path }));
      }
      sp_da_push(plan->staged, closure);
    }
  }
  sp_mem_end_scratch(scratch);
  return SPN_OK;
}

spn_err_t spn_units_add_targets(spn_session_t* s, spn_unit_scope_t scope) {
  switch (scope) {
    case SPN_UNIT_SCOPE_METAPROGRAM: return add_metaprogram_targets(s);
    case SPN_UNIT_SCOPE_TARGET:      return add_target_units(s);
  }
  sp_unreachable_return(SPN_ERROR);
}
