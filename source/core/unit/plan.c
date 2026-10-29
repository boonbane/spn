#include "core/types.h"
#include "unit/types.h"
#include "unit/unit.h"

#include "sp.h"
#include "macro/macro.h"
#include "str/str.h"

#include "ctx/types.h"

#include "compiler/driver.h"
#include "dag/glob.h"
#include "error/error.h"
#include "enum/enum.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "session/invocation.h"
#include "session/session.h"
#include "graph/build.h"
#include "profile/types.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

static bool publishes(spn_pkg_unit_t* unit) {
  if (!si_da_empty(unit->pkg.publish)) {
    return true;
  }
  si_om_for_each(unit->pkg.targets, target) {
    if (!si_da_empty(target->publish)) {
      return true;
    }
  }
  return false;
}

static s32 compare_objects(const void* a, const void* b) {
  return sp_str_compare_alphabetical((*(spn_compile_unit_t* const*)a)->file.sub, (*(spn_compile_unit_t* const*)b)->file.sub);
}

static void add_object(spn_session_t* s, spn_target_unit_t* target, spn_path_t file) {
  spn_compile_unit_id_t id = {
    .target = target->id,
    .source = { .root = file.root, .sub = sp_intern(s->ctx->intern, file.sub).id },
  };
  if (sp_om_has(s->units.objects, id)) {
    return;
  }

  spn_tree_rel_t rel = spn_tree_rel(target->pkg->paths.roots, file);
  sp_str_t prefix = rel.tree == SPN_TREE_NONE ? spn_path_root_label(file.root) : spn_tree_to_str(rel.tree);
  sp_str_t name = sp_fmt(s->mem, "{}/{}.o", sp_fmt_str(prefix), sp_fmt_str(rel.sub)).value;
  sp_om_insert(s->units.objects, id, ((spn_compile_unit_t) {
    .id = id,
    .target = target,
    .lang = spn_lang_from_path(rel.sub),
    .name = name,
    .file = { .root = file.root, .sub = sp_intern_find(s->ctx->intern, id.source.sub) },
  }));
  sp_da_push(target->objects, sp_om_back(s->units.objects));
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

static bool contains_str(si_da(sp_str_t) strs, sp_str_t str) {
  si_da_for(strs, it) {
    if (sp_str_equal(strs[it], str)) {
      return true;
    }
  }
  return false;
}

static void push_unique(sp_mem_t mem, si_da(sp_str_t)* result, si_da(sp_str_t) values) {
  si_da_for(values, it) {
    if (!contains_str(*result, values[it])) {
      si_da_push(mem, *result, values[it]);
    }
  }
}

static spn_err_t lower_target(spn_target_unit_t* unit) {
  spn_pkg_unit_t* pkg = unit->pkg;
  spn_session_t* s = pkg->session;
  spn_build_unit_t* build = pkg->build;
  si_target_t* eval = unit->target;
  const si_target_info_t* decl = eval->info;
  spn_profile_info_t* profile = &build->profile;
  spn_cc_t* toolchain = &build->toolchain->cc;
  sp_mem_t mem = s->mem;

  switch (decl->kind) {
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_SCRIPT: {
      unit->install = pkg->paths.bin;
      break;
    }
    case SPN_TARGET_KIND_LIB: {
      unit->install = pkg->paths.lib;
      break;
    }
    case SPN_TARGET_KIND_TEST:
    case SPN_TARGET_KIND_EXAMPLE:
    case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM:
    case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
      break;
    }
  }

  sp_da_for_ptr(pkg->user_nodes, node) {
    si_da_for_ptr(node->outputs, output) {
      si_da_push(mem, unit->orders, ((spn_target_order_t) { .path = output->path, .kind = output->kind }));
    }
  }
  si_da_for_ptr(pkg->deps, edge) {
    if (spn_dep_kind_applies(edge->kind, decl->kind) && publishes(edge->unit)) {
      si_da_push(mem, unit->orders, ((spn_target_order_t) { .path = edge->unit->paths.include, .kind = SPN_DAG_ARTIFACT_KIND_TREE }));
    }
  }
  if (decl->kind == SPN_TARGET_KIND_EXAMPLE && publishes(pkg)) {
    si_da_push(mem, unit->orders, ((spn_target_order_t) { .path = pkg->paths.include, .kind = SPN_DAG_ARTIFACT_KIND_TREE }));
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  si_da(spn_closure_entry_t) closure = si_link_get_target_closure(scratch.mem, unit);
  sp_assert(closure[0].pkg == pkg);
  si_da(spn_link_lib_t) libs = si_link_get_closure_libs(scratch.mem, closure);
  bool dynamic = unit->kind == SPN_CC_OUTPUT_SHARED_LIB || unit->kind == SPN_CC_OUTPUT_REACTOR;

  spn_link_plan_t* link = &unit->link;
  *link = (spn_link_plan_t) {
    .cc = {
      .kind = unit->kind,
      .lang = is_any_object_cxx(unit->objects) ? SPN_LANG_CXX : SPN_LANG_C,
      .min_os = decl->macos.min_os,
      .subsystem = decl->windows.subsystem,
    },
  };
  si_da_for(libs, it) {
    spn_target_unit_t* lib = libs[it].lib;
    spn_link_role_t role = SPN_LINK_ROLE_ARCHIVE;
    switch (lib->lib_kind) {
      case SPN_LIB_KIND_SHARED: {
        role = SPN_LINK_ROLE_SHARED;
        break;
      }
      case SPN_LIB_KIND_STATIC: {
        role = dynamic && !libs[it].private ? SPN_LINK_ROLE_WHOLE_ARCHIVE : SPN_LINK_ROLE_ARCHIVE;
        if (is_any_object_cxx(lib->objects)) {
          link->cc.lang = SPN_LANG_CXX;
        }
        break;
      }
      case SPN_LIB_KIND_SOURCE:
      case SPN_LIB_KIND_OBJECT:
      case SPN_LIB_KIND_NONE: {
        sp_unreachable_case();
      }
    }
    si_da_push(mem, link->deps, ((spn_link_dep_t) { .lib = lib, .role = role }));
  }

  push_unique(mem, &link->cc.frameworks, eval->macos.frameworks);
  push_unique(mem, &link->cc.system_libs, eval->system_deps);
  si_da_for(closure, it) {
    spn_closure_entry_t* entry = &closure[it];
    link->cc.min_os = max_os_version(link->cc.min_os, entry->pkg->pkg.manifest->macos.min_os);
    if (entry->links_code) {
      push_unique(mem, &link->cc.frameworks, entry->pkg->pkg.macos.frameworks);
    }
    push_unique(mem, &link->cc.system_libs, entry->pkg->pkg.system_deps);
    si_da_for(entry->targets, lt) {
      spn_target_unit_t* lib = entry->targets[lt];
      link->cc.min_os = max_os_version(link->cc.min_os, lib->target->info->macos.min_os);
      if (lib->target->info->no_link || lib->lib_kind == SPN_LIB_KIND_SHARED) {
        continue;
      }
      push_unique(mem, &link->cc.frameworks, lib->target->macos.frameworks);
      push_unique(mem, &link->cc.system_libs, lib->target->system_deps);
    }
  }

  switch (unit->kind) {
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_REACTOR:
    case SPN_CC_OUTPUT_SHARED_LIB: {
      link->cc.args = eval->link_flags;
      link->cc.scripts = eval->linker_script;
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }

  spn_cc_compile_t compile = {
    .cxx = decl->cxx,
    .pic = decl->kind == SPN_TARGET_KIND_LIB && spn_triple_pic(spn_profile_triple(profile)),
  };
  if (profile->os == SPN_OS_MACOS) {
    compile.min_os = link->cc.min_os;
  }
  si_da_for(eval->configured.include, it) {
    si_da_push(scratch.mem, compile.include, eval->configured.include[it]);
  }
  si_da_for(pkg->pkg.configured.include, it) {
    si_da_push(scratch.mem, compile.include, pkg->pkg.configured.include[it]);
  }
  sp_da_for(build->include, it) {
    si_da_push(scratch.mem, compile.include, build->include[it]);
  }
  si_da_for(pkg->pkg.include, it) {
    si_da_push(scratch.mem, compile.include, pkg->pkg.include[it]);
  }
  si_da_for(eval->include, it) {
    si_da_push(scratch.mem, compile.include, eval->include[it]);
  }
  if (decl->kind == SPN_TARGET_KIND_EXAMPLE) {
    si_da_push(scratch.mem, compile.include, pkg->paths.include);
  }
  si_da_for_ptr(pkg->deps, dep) {
    if (spn_dep_kind_applies(dep->kind, decl->kind)) {
      si_da_push(scratch.mem, compile.include, dep->unit->paths.include);
    }
  }
  sp_da_for(build->define, it) {
    si_da_push(scratch.mem, compile.define, build->define[it]);
  }
  si_da_for(pkg->pkg.define, it) {
    si_da_push(scratch.mem, compile.define, pkg->pkg.define[it]);
  }
  si_da_for(eval->define, it) {
    si_da_push(scratch.mem, compile.define, eval->define[it]);
  }
  si_da_for(eval->flags, it) {
    si_da_push(scratch.mem, compile.args, eval->flags[it]);
  }
  si_da_for_ptr(pkg->deps, dep) {
    if (!spn_dep_kind_applies(dep->kind, decl->kind)) {
      continue;
    }
    si_da_for_value(dep->unit->pkg.public_define, define) {
      si_da_push(scratch.mem, compile.define, define);
    }
  }
  sp_da_for_value(unit->objects, object) {
    object->plan = si_cc_render_compile(mem, toolchain, profile, object->lang, &compile);
  }
  sp_mem_end_scratch(scratch);

  switch (unit->kind) {
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      spn_cc_link_refusal_t refusal = spn_cc_validate_link(toolchain, spn.host, profile, &link->cc);
      switch (refusal.err) {
        case SPN_OK: {
          break;
        }
        case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = refusal.err,
            .compiler = {
              .toolchain = toolchain->name.str,
              .target = spn_profile_triple(profile),
              .feature = refusal.feature,
              .pkg = pkg->pkg.manifest->name.str,
              .name = decl->name.str,
              .frameworks = link->cc.frameworks,
            },
          });
        }
        case SPN_ERR_TOOLCHAIN_MSVC_LINKER_HOST: {
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = refusal.err,
            .toolchain = {
              .name = toolchain->name.str,
              .target = spn_profile_triple(profile),
              .host = spn.host,
            },
          });
        }
        default: {
          sp_unreachable_return(SPN_ERROR);
        }
      }
      link->plan = si_cc_render_link(mem, toolchain, profile, &link->cc);
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }

  switch (unit->kind) {
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      unit->archive = si_cc_render_archive(mem, toolchain, profile);
      break;
    }
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }
  return SPN_OK;
}

static spn_err_t plan_new(spn_session_t* s, sp_mem_t scratch, spn_closure_t closure, spn_build_plan_t* plan) {
  *plan = (spn_build_plan_t) { .closure = closure };

  si_da_for_value(closure.pkgs, pkg) {
    if (publishes(pkg)) {
      si_da_push(s->mem, plan->publishes, pkg);
    }
    sp_da_for_ptr(pkg->user_nodes, node) {
      si_da_push(s->mem, plan->nodes, node);
      if (sp_str_empty(node->fn)) {
        continue;
      }
      spn_pkg_unit_t* metaprogram = si_get_pkg_unit(s, s->units.metaprogram, pkg->id.pkg);
      sp_assert(metaprogram);
      si_target_t* script = spn_pkg_get_script(&metaprogram->pkg, SPN_TARGET_KIND_BUILD_METAPROGRAM);
      if (!script) {
        return spn_err_emit(s->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_NODE_NO_BUILD_SCRIPT,
          .node = {
            .pkg = pkg->pkg.manifest->name.str,
            .tag = node->tag,
            .fn = node->fn,
          },
        });
      }
      node->script = spn_session_find_target_in_pkg(s, metaprogram, script->info->id);
      sp_assert(node->script);
    }
  }

  si_da_for_value(closure.targets, target) {
    if (target->target->info->kind == SPN_TARGET_KIND_CONFIGURE_METAPROGRAM) {
      si_da_for_value(target->target->source, source) {
        bool found = false;
        switch (source.kind) {
          case SPN_SOURCE_FILE: {
            sp_path_t at = spn_path_at(&s->ctx->roots, source.path);
            sp_sys_file_meta_t meta = sp_zero;
            found = !sp_sys_get_path_metadata_s(at.dir, at.sub, &meta) && meta.kind == SP_FS_KIND_FILE;
            break;
          }
          case SPN_SOURCE_GLOB: {
            spn_dag_glob_it_t glob = spn_dag_glob_it_new(scratch, &s->ctx->roots, source.path);
            while (!found && spn_dag_glob_it_next(&glob)) {
              found = glob.entry.kind != SP_FS_KIND_DIR;
            }
            spn_dag_glob_it_deinit(&glob);
            break;
          }
        }
        if (!found) {
          return spn_err_emit(s->ctx, (spn_err_union_t) {
            .kind = source.kind == SPN_SOURCE_FILE ? SPN_ERR_CONFIGURE_SOURCE_MISSING : SPN_ERR_CONFIGURE_SOURCE_GLOB,
            .configure_source = {
              .name = target->pkg->pkg.manifest->qualified.str,
              .source = spn_path_str(&s->ctx->roots, s->ctx->events->mem, source.path),
            },
          });
        }
      }
    }

    if (target->lib_kind == SPN_LIB_KIND_SOURCE) {
      continue;
    }
    si_da_for_value(target->target->source, source) {
      switch (source.kind) {
        case SPN_SOURCE_FILE: {
          add_object(s, target, source.path);
          break;
        }
        case SPN_SOURCE_GLOB: {
          u64 first = sp_da_size(target->objects);
          spn_dag_glob_it_t glob = spn_dag_glob_it_new(scratch, &s->ctx->roots, source.path);
          while (spn_dag_glob_it_next(&glob)) {
            if (glob.entry.kind != SP_FS_KIND_DIR) {
              add_object(s, target, spn_path_join(scratch, glob.base, glob.entry.rel));
            }
          }
          spn_dag_glob_it_deinit(&glob);
          if (glob.err) {
            return spn_err_emit(s->ctx, (spn_err_union_t) {
              .kind = SPN_ERR_TARGET_SOURCE_GLOB,
              .target_source = {
                .pkg = target->pkg->pkg.manifest->name.str,
                .name = target->target->info->name.str,
                .source = spn_path_str(&s->ctx->roots, s->ctx->events->mem, source.path),
              },
            });
          }
          sp_os_qsort(target->objects + first, sp_da_size(target->objects) - first, sizeof(*target->objects), compare_objects);
          break;
        }
      }
    }
    if (is_any_object_cxx(target->objects) && spn_arg_empty(target->pkg->build->toolchain->cc.cxx.program)) {
      return spn_err_emit(s->ctx, (spn_err_union_t) {
        .kind = SPN_ERR_TOOLCHAIN_NO_CXX,
        .toolchain = { .name = target->pkg->build->toolchain->info->name.str },
      });
    }
  }

  si_da_for_value(closure.targets, target) {
    spn_try(lower_target(target));
  }

  si_ht(spn_path_t, spn_target_unit_t*) claimed = sp_zero;
  si_ht_init_ex(scratch, claimed, spn_path_on_hash, spn_path_on_compare);
  si_da_for_value(closure.roots, root) {
    if (root->kind != SPN_CC_OUTPUT_EXE) {
      continue;
    }
    spn_stage_closure_t closure = {
      .exe = {
        .target = root,
        .path = spn_target_unit_staged_path(s->mem, root),
      },
    };
    spn_path_t dir = spn_path_parent(closure.exe.path);
    si_da(spn_target_unit_t*) libs = si_link_get_target_runtime_libs(scratch, root);
    si_da_for_value(libs, lib) {
      spn_path_t path = spn_path_join(s->mem, dir, lib->file_name);
      spn_target_unit_t** owner = si_ht_getp(claimed, path);
      if (owner && *owner != lib) {
        return spn_err_emit(s->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_TARGET_COLLISION,
          .collision = {
            .exe = root->target->info->name.str,
            .pkg = (*owner)->pkg->pkg.manifest->name.str,
            .other = lib->pkg->pkg.manifest->name.str,
            .name = lib->file_name,
          },
        });
      }
      si_ht_insert(scratch, claimed, path, lib);
      si_da_push(s->mem, closure.libs, ((spn_stage_entry_t) { .target = lib, .path = path }));
    }
    si_da_push(s->mem, plan->staged, closure);
  }
  return SPN_OK;
}

spn_err_t spn_plan_new(spn_session_t* s, spn_closure_t closure, spn_build_plan_t* plan) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = plan_new(s, scratch.mem, closure, plan);
  sp_mem_end_scratch(scratch);
  return err;
}
