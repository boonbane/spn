#include "core/types.h"
#include "ordered_map.h"
#include "unit/types.h"
#include "unit/unit.h"
#include "unit/closure/closure.h"

#include "sp.h"
#include "macro/macro.h"
#include "str/str.h"

#include "ctx/types.h"

#include "compiler/driver.h"
#include "compiler/rsp.h"
#include "dag/glob.h"
#include "error/error.h"
#include "enum/enum.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "profile/types.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

static s32 compare_objects(const void* a, const void* b) {
  return sp_str_compare_alphabetical((*(spn_compile_unit_t* const*)a)->file.sub, (*(spn_compile_unit_t* const*)b)->file.sub);
}

static void add_object(spn_units_t* u, spn_target_unit_t* target, spn_path_t file) {
  spn_compile_unit_id_t id = {
    .target = target->id,
    .source = { .root = file.root, .sub = sp_intern(u->ctx->intern, file.sub).id },
  };
  if (sp_om_has(u->objects, id)) {
    return;
  }

  spn_tree_rel_t rel = spn_tree_rel(target->pkg->paths.roots, file);
  sp_str_t prefix = rel.tree == SPN_TREE_NONE ? spn_path_root_label(file.root) : spn_tree_to_str(rel.tree);
  sp_str_t name = sp_fmt(u->mem, "{}/{}.o", sp_fmt_str(prefix), sp_fmt_str(rel.sub)).value;
  sp_om_insert(u->objects, id, ((spn_compile_unit_t) {
    .id = id,
    .target = target,
    .lang = spn_lang_from_path(rel.sub),
    .name = name,
    .file = { .root = file.root, .sub = sp_intern_find(u->ctx->intern, id.source.sub) },
  }));
  sp_da_push(target->objects, sp_om_back(u->objects));
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

static spn_err_t lower_link(spn_units_t* u, spn_target_unit_t* unit) {
  spn_pkg_unit_t* pkg = unit->pkg;
  spn_profile_info_t* profile = &pkg->build->profile;
  spn_cc_t* toolchain = &pkg->build->toolchain->cc;
  spn_cc_link_t* cc = &unit->link.cc;
  cc->args = unit->target->link_flags;
  cc->scripts = unit->target->linker_script;

  spn_cc_link_refusal_t refusal = spn_cc_validate_link(toolchain, spn.host, profile, cc);
  switch (refusal.err) {
    case SPN_OK: {
      break;
    }
    case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
      return spn_err_emit(u->ctx, (spn_err_union_t) {
        .kind = refusal.err,
        .compiler = {
          .toolchain = toolchain->name.str,
          .target = profile->target,
          .host = spn.host,
          .feature = refusal.feature,
          .pkg = pkg->pkg.manifest->name.str,
          .name = unit->target->info->name.str,
          .frameworks = cc->frameworks,
        },
      });
    }
    case SPN_ERR_TOOLCHAIN_MSVC_LINKER_HOST: {
      return spn_err_emit(u->ctx, (spn_err_union_t) {
        .kind = refusal.err,
        .toolchain = {
          .name = toolchain->name.str,
          .target = profile->target,
          .host = spn.host,
        },
      });
    }
    default: {
      sp_unreachable_return(SPN_ERROR);
    }
  }
  sp_opt_set(unit->emit.link, si_cc_render_link(u->mem, toolchain, profile, cc));
  return SPN_OK;
}

static spn_exports_plan_t render_exports(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, const spn_cc_link_plan_t* link, sp_str_t library) {
  return (spn_exports_plan_t) {
    .archive = si_cc_render_archive(mem, toolchain, profile),
    .format = link->exports,
    .rsp = spn_rsp_style(toolchain->driver),
    .library = library,
    .file_name = sp_fmt(mem, "{}.{}", sp_fmt_str(library), sp_fmt_str(spn_cc_exports_extension(link->exports))).value,
  };
}

static spn_err_t lower_target(spn_units_t* u, spn_target_unit_t* unit) {
  spn_pkg_unit_t* pkg = unit->pkg;
  spn_build_unit_t* build = pkg->build;
  si_target_t* target = unit->target;
  spn_profile_info_t* profile = &build->profile;
  spn_cc_t* toolchain = &build->toolchain->cc;
  sp_mem_t mem = u->mem;

  switch (target->info->kind) {
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

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  si_da(si_closure_entry_t) entries = sp_zero;
  si_closure_it_t it = si_closure_it(scratch.mem, SI_CLOSURE_STOP_AT_SHARED);
  si_closure_it_push(&it, unit);
  while (si_closure_it_next(&it)) {
    si_da_push(scratch.mem, entries, it.entry);
  }
  bool dynamic = unit->kind == SPN_CC_OUTPUT_SHARED_LIB || unit->kind == SPN_CC_OUTPUT_REACTOR;

  spn_link_plan_t* link = &unit->link;
  *link = (spn_link_plan_t) {
    .cc = {
      .kind = unit->kind,
      .lang = is_any_object_cxx(unit->objects) ? SPN_LANG_CXX : SPN_LANG_C,
      .min_os = target->info->macos.min_os,
      .subsystem = target->info->windows.subsystem,
    },
  };
  link->cc.min_os = max_os_version(link->cc.min_os, pkg->pkg.manifest->macos.min_os);
  push_unique(mem, &link->cc.frameworks, target->macos.frameworks);
  push_unique(mem, &link->cc.frameworks, pkg->pkg.macos.frameworks);
  push_unique(mem, &link->cc.system_libs, target->system_deps);
  push_unique(mem, &link->cc.system_libs, pkg->pkg.system_deps);
  si_da_rfor(entries, et) {
    si_closure_entry_t* entry = &entries[et];
    switch (entry->yield) {
      case SI_CLOSURE_YIELD_PKG: {
        spn_pkg_unit_t* dep = entry->pkg;
        link->cc.min_os = max_os_version(link->cc.min_os, dep->pkg.manifest->macos.min_os);
        bool links_code = si_da_empty(dep->libs);
        si_da_for_value(dep->libs, lib) {
          links_code = links_code || (!lib->target->info->no_link && lib->lib_kind != SPN_LIB_KIND_SHARED);
        }
        if (links_code) {
          push_unique(mem, &link->cc.frameworks, dep->pkg.macos.frameworks);
        }
        push_unique(mem, &link->cc.system_libs, dep->pkg.system_deps);
        break;
      }
      case SI_CLOSURE_YIELD_LIB: {
        spn_target_unit_t* lib = entry->lib;
        link->cc.min_os = max_os_version(link->cc.min_os, lib->target->info->macos.min_os);
        if (lib->target->info->no_link) {
          break;
        }
        switch (lib->lib_kind) {
          case SPN_LIB_KIND_SHARED: {
            if (!sp_da_empty(lib->objects)) {
              si_da_push(mem, link->deps, ((spn_link_dep_t) { .lib = lib, .role = SPN_LINK_ROLE_SHARED }));
            }
            break;
          }
          case SPN_LIB_KIND_STATIC: {
            if (!sp_da_empty(lib->objects)) {
              spn_link_role_t role = dynamic && !entry->private ? SPN_LINK_ROLE_WHOLE_ARCHIVE : SPN_LINK_ROLE_ARCHIVE;
              si_da_push(mem, link->deps, ((spn_link_dep_t) { .lib = lib, .role = role }));
            }
            if (is_any_object_cxx(lib->objects)) {
              link->cc.lang = SPN_LANG_CXX;
            }
            push_unique(mem, &link->cc.frameworks, lib->target->macos.frameworks);
            push_unique(mem, &link->cc.system_libs, lib->target->system_deps);
            break;
          }
          case SPN_LIB_KIND_SOURCE:
          case SPN_LIB_KIND_OBJECT:
          case SPN_LIB_KIND_NONE: {
            push_unique(mem, &link->cc.frameworks, lib->target->macos.frameworks);
            push_unique(mem, &link->cc.system_libs, lib->target->system_deps);
            break;
          }
        }
        break;
      }
    }
  }

  spn_cc_compile_t compile = {
    .cxx = target->info->cxx,
    .pic = target->info->kind == SPN_TARGET_KIND_LIB && spn_ld_dynamic(profile->target) && spn_os_to_native_object_format(profile->target.os) != SPN_OBJ_COFF,
  };
  if (profile->target.os == SPN_OS_MACOS) {
    compile.min_os = link->cc.min_os;
  }
  sp_da_for(build->include, it) {
    si_da_push(scratch.mem, compile.include, build->include[it]);
  }
  si_da_for(pkg->pkg.include, it) {
    si_da_push(scratch.mem, compile.include, pkg->pkg.include[it]);
  }
  si_da_for(target->include, it) {
    si_da_push(scratch.mem, compile.include, target->include[it]);
  }
  si_da_for_value(unit->includes, dep) {
    si_da_push(scratch.mem, compile.include, dep->paths.include);
  }
  sp_da_for(build->define, it) {
    si_da_push(scratch.mem, compile.define, build->define[it]);
  }
  si_da_for(pkg->pkg.define, it) {
    si_da_push(scratch.mem, compile.define, pkg->pkg.define[it]);
  }
  si_da_for(target->define, it) {
    si_da_push(scratch.mem, compile.define, target->define[it]);
  }
  si_da_for(target->flags, it) {
    si_da_push(scratch.mem, compile.args, target->flags[it]);
  }
  si_da_for_ptr(unit->edges, edge) {
    si_da_for_value(edge->pkg->pkg.public_define, define) {
      si_da_push(scratch.mem, compile.define, define);
    }
  }
  sp_da_for_value(unit->objects, object) {
    object->plan = si_cc_render_compile(mem, toolchain, profile, object->lang, &compile);
  }
  sp_mem_end_scratch(scratch);

  if (sp_da_empty(unit->objects)) {
    return SPN_OK;
  }

  spn_target_emit_t* emit = &unit->emit;
  spn_triple_t triple = profile->target;
  sp_str_t name = target->info->name.str;
  switch (unit->kind) {
    case SPN_CC_OUTPUT_EXE: {
      spn_try(lower_link(u, unit));
      switch (triple.os) {
        case SPN_OS_WINDOWS:      emit->output = sp_fmt(mem, "{}.exe", sp_fmt_str(name)).value; break;
        case SPN_OS_WASI:         emit->output = sp_fmt(mem, "{}.wasm", sp_fmt_str(name)).value; break;
        case SPN_OS_FREESTANDING: emit->output = sp_fmt(mem, "{}.elf", sp_fmt_str(name)).value; break;
        case SPN_OS_LINUX:
        case SPN_OS_MACOS:        emit->output = name; break;
        case SPN_OS_NONE:         sp_unreachable_case();
      }
      break;
    }
    case SPN_CC_OUTPUT_SHARED_LIB: {
      spn_try(lower_link(u, unit));
      switch (triple.os) {
        case SPN_OS_WINDOWS: emit->output = sp_fmt(mem, "{}.dll", sp_fmt_str(name)).value; break;
        case SPN_OS_MACOS:   emit->output = sp_fmt(mem, "lib{}.dylib", sp_fmt_str(name)).value; break;
        case SPN_OS_LINUX:   emit->output = sp_fmt(mem, "lib{}.so", sp_fmt_str(name)).value; break;
        case SPN_OS_WASI:
        case SPN_OS_FREESTANDING:
        case SPN_OS_NONE:    sp_unreachable_case();
      }
      if (spn_cc_link_implib(profile, unit->kind)) {
        emit->implib = sp_fmt(mem, "{}.lib", sp_fmt_str(name)).value;
      }
      sp_opt_set(emit->exports, render_exports(mem, toolchain, profile, &emit->link.value, name));
      break;
    }
    case SPN_CC_OUTPUT_REACTOR: {
      spn_try(lower_link(u, unit));
      emit->output = sp_fmt(mem, "{}.wasm", sp_fmt_str(name)).value;
      sp_opt_set(emit->exports, render_exports(mem, toolchain, profile, &emit->link.value, name));
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB: {
      emit->output = sp_fmt(mem, triple.abi == SPN_ABI_MSVC ? "{}.lib" : "lib{}.a", sp_fmt_str(name)).value;
      sp_opt_set(emit->archive, si_cc_render_archive(mem, toolchain, profile));
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      emit->objects = true;
      break;
    }
  }
  return SPN_OK;
}

static spn_path_t staged_path(sp_mem_t mem, spn_target_unit_t* unit) {
  if (sp_str_empty(unit->emit.output)) {
    return sp_zero_s(spn_path_t);
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t root = unit->pkg->build->paths.root;
  spn_path_t path = sp_zero;
  switch (unit->target->info->kind) {
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_SCRIPT: {
      path = spn_path_join(mem, root, unit->emit.output);
      break;
    }
    case SPN_TARGET_KIND_TEST: {
      path = spn_path_join(mem, spn_path_join(s.mem, root, SP_LIT("test")), unit->emit.output);
      break;
    }
    case SPN_TARGET_KIND_EXAMPLE: {
      path = spn_path_join(mem, spn_path_join(s.mem, root, SP_LIT("example")), unit->emit.output);
      break;
    }
    case SPN_TARGET_KIND_LIB:
    case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM:
    case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
      break;
    }
  }
  sp_mem_end_scratch(s);
  return path;
}

static spn_err_t plan_new(spn_units_t* u, sp_mem_t scratch, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots, spn_build_plan_t* plan) {
  *plan = sp_zero_s(spn_build_plan_t);

  sp_om_for(u->packages, pt) {
    spn_pkg_unit_t* pkg = sp_om_at(u->packages, pt);
    si_da_push(u->mem, plan->pkgs, pkg);
    spn_publish_plan_t publish = { .pkg = pkg };
    si_da_for_value(pkg->pkg.publish, entry) {
      si_da_push(u->mem, publish.entries, entry);
    }
    si_om_for_each(pkg->pkg.targets, target) {
      si_da_for_value(target->publish, entry) {
        si_da_push(u->mem, publish.entries, entry);
      }
    }
    if (!si_da_empty(publish.entries)) {
      si_da_push(u->mem, plan->publishes, publish);
    }
    sp_da_for_ptr(pkg->user_nodes, node) {
      si_da_push(u->mem, plan->nodes, node);
    }
  }

  sp_om_for(u->targets, tt) {
    spn_target_unit_t* target = sp_om_at(u->targets, tt);
    si_da_push(u->mem, plan->targets, target);
    if (target->target->info->kind == SPN_TARGET_KIND_CONFIGURE_METAPROGRAM) {
      si_da_for_value(target->target->source, source) {
        bool found = false;
        switch (source.kind) {
          case SPN_SOURCE_FILE: {
            sp_path_t at = spn_path_at(&u->ctx->roots, source.path);
            sp_sys_file_meta_t meta = sp_zero;
            found = !sp_sys_get_path_metadata_s(at.dir, at.sub, &meta) && meta.kind == SP_FS_KIND_FILE;
            break;
          }
          case SPN_SOURCE_GLOB: {
            spn_dag_glob_it_t glob = spn_dag_glob_it_new(scratch, &u->ctx->roots, source.path);
            while (!found && spn_dag_glob_it_next(&glob)) {
              found = glob.entry.kind != SP_FS_KIND_DIR;
            }
            spn_dag_glob_it_deinit(&glob);
            break;
          }
        }
        if (!found) {
          return spn_err_emit(u->ctx, (spn_err_union_t) {
            .kind = source.kind == SPN_SOURCE_FILE ? SPN_ERR_CONFIGURE_SOURCE_MISSING : SPN_ERR_CONFIGURE_SOURCE_GLOB,
            .configure_source = {
              .name = target->pkg->pkg.manifest->name.str,
              .source = spn_path_str(&u->ctx->roots, u->ctx->events->mem, source.path),
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
          add_object(u, target, source.path);
          break;
        }
        case SPN_SOURCE_GLOB: {
          u64 first = sp_da_size(target->objects);
          spn_dag_glob_it_t glob = spn_dag_glob_it_new(scratch, &u->ctx->roots, source.path);
          while (spn_dag_glob_it_next(&glob)) {
            if (glob.entry.kind != SP_FS_KIND_DIR) {
              add_object(u, target, spn_path_join(scratch, glob.base, glob.entry.rel));
            }
          }
          spn_dag_glob_it_deinit(&glob);
          if (glob.err) {
            return spn_err_emit(u->ctx, (spn_err_union_t) {
              .kind = SPN_ERR_TARGET_SOURCE_GLOB,
              .target_source = {
                .pkg = target->pkg->pkg.manifest->name.str,
                .name = target->target->info->name.str,
                .source = spn_path_str(&u->ctx->roots, u->ctx->events->mem, source.path),
              },
            });
          }
          sp_os_qsort(target->objects + first, sp_da_size(target->objects) - first, sizeof(*target->objects), compare_objects);
          break;
        }
      }
    }
    if (is_any_object_cxx(target->objects) && spn_arg_empty(target->pkg->build->toolchain->cc.cxx.program)) {
      return spn_err_emit(u->ctx, (spn_err_union_t) {
        .kind = SPN_ERR_TOOLCHAIN_NO_CXX,
        .toolchain = { .name = target->pkg->build->toolchain->info->name.str },
      });
    }
  }

  si_da_for_value(plan->targets, target) {
    spn_try(lower_target(u, target));
  }

  si_ht(spn_path_t, spn_target_unit_t*) claimed = sp_zero;
  si_ht_init_ex(scratch, claimed, spn_path_on_hash, spn_path_on_compare);
  si_da_for_value(roots, exe) {
    spn_path_t staged = staged_path(u->mem, exe);
    if (spn_path_empty(staged)) {
      continue;
    }
    spn_stage_closure_t closure = {
      .exe = {
        .target = exe,
        .path = staged,
      },
    };
    si_da_push(u->mem, plan->claims, ((spn_dag_claim_t) { .path = staged, .kind = SPN_DAG_ARTIFACT_KIND_FILE }));
    spn_path_t dir = spn_path_parent(closure.exe.path);
    si_closure_it_t it = si_closure_it(scratch, SI_CLOSURE_CHASE_SHARED);
    si_closure_it_push(&it, exe);
    while (si_closure_it_next(&it)) {
      if (it.entry.yield != SI_CLOSURE_YIELD_LIB) {
        continue;
      }
      spn_target_unit_t* lib = it.entry.lib;
      if (lib->lib_kind != SPN_LIB_KIND_SHARED || lib->target->info->no_link || sp_str_empty(lib->emit.output)) {
        continue;
      }
      spn_path_t path = spn_path_join(u->mem, dir, lib->emit.output);
      spn_target_unit_t** owner = si_ht_getp(claimed, path);
      if (owner && *owner != lib) {
        return spn_err_emit(u->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_TARGET_COLLISION,
          .collision = {
            .exe = exe->target->info->name.str,
            .pkg = (*owner)->pkg->pkg.manifest->name.str,
            .other = lib->pkg->pkg.manifest->name.str,
            .name = lib->emit.output,
          },
        });
      }
      if (!owner) {
        si_ht_insert(scratch, claimed, path, lib);
        si_da_push(u->mem, plan->claims, ((spn_dag_claim_t) { .path = path, .kind = SPN_DAG_ARTIFACT_KIND_FILE }));
      }
      si_da_push(u->mem, closure.libs, ((spn_stage_entry_t) { .target = lib, .path = path }));
    }
    si_da_push(u->mem, plan->closures, closure);
  }

  if (root) {
    si_da_for_value(root->pkg.manifest->stage, copy) {
      spn_user_output_t* from = SP_NULLPTR;
      sp_da_for(root->user_nodes, nt) {
        si_da_for_ptr(root->user_nodes[nt].outputs, output) {
          if (output->tree == copy.tree && sp_str_equal(output->sub, copy.sub)) {
            from = output;
          }
        }
      }
      if (!from) {
        return spn_err_emit(u->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_STAGE_UNPRODUCED,
          .dag = { .path = sp_fmt(u->ctx->events->mem, "{}/{}", sp_fmt_str(spn_tree_to_str(copy.tree)), sp_fmt_str(copy.sub)).value },
        });
      }
      spn_path_t to = spn_path_join(u->mem, spn_path_from_root(SPN_PATH_ROOT_PROJECT), copy.to);
      si_da_push(u->mem, plan->stages, ((spn_stage_plan_t) { .from = from, .to = to }));
      si_da_push(u->mem, plan->claims, ((spn_dag_claim_t) { .path = to, .kind = from->kind }));
    }
  }
  return SPN_OK;
}

spn_err_t spn_plan_new(spn_units_t* u, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots, spn_build_plan_t* plan) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = plan_new(u, scratch.mem, root, roots, plan);
  sp_mem_end_scratch(scratch);
  return err;
}
