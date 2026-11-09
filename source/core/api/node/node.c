#include "sp.h"
#include "macro/macro.h"
#include "spn.h"

#include "api/api.h"
#include "api/types.h"
#include "ctx/types.h"
#include "event/types.h"
#include "unit/types.h"

#include "event/event.h"
#include "external/wasm/wasi.h"
#include "external/wasm/wasm.h"
#include "paths/paths.h"
#include "unit/unit.h"
#include "intern/intern.h"

spn_node_t* spn_add_node(spn_config_t* config, const c8* tag) {
  spn_pkg_unit_t* unit = spn_api_unit(config);
  if (spn_api_name_rejected("spn_add_node", tag)) {
    return SP_NULLPTR;
  }

  sp_mem_t mem = spn.mem;
  u32 index = sp_da_size(unit->user_nodes);
  spn_user_node_t node = {
    .unit = unit,
    .tag = spn_intern_cstr(tag).str,
  };
  sp_da_push(unit->user_nodes, node);

  spn_node_t* out = sp_alloc_type(mem, spn_node_t);
  *out = (spn_node_t) {
    .ref = {
      .pkg = unit,
      .index = index,
    },
  };

  return out;
}

typedef enum {
  OUTPUT_OK,
  OUTPUT_ABNORMAL,
  OUTPUT_UNNAMED,
  OUTPUT_ROOT,
} output_problem_t;

static sp_str_t output_problem_str(output_problem_t problem) {
  switch (problem) {
    case OUTPUT_OK:       return sp_str_lit("");
    case OUTPUT_ABNORMAL: return sp_str_lit("must not contain '.', '..', or empty components");
    case OUTPUT_UNNAMED:  return sp_str_lit("must name a path under its root");
    case OUTPUT_ROOT:     return sp_str_lit("must be under /work, /store/lib, /store/bin, or /store/vendor");
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

static output_problem_t find_output(spn_pkg_unit_t* unit, sp_mem_t mem, sp_str_t guest, spn_dir_t* dir, sp_str_t* sub) {
  if (!spn_path_normal(guest)) {
    return OUTPUT_ABNORMAL;
  }
  spn_wasm_mounts_t mounts = spn_wasm_mounts(unit);
  sp_str_t rest = sp_zero;
  const spn_wasi_mount_t* mount = spn_wasi_find_mount(mounts.dirs, sp_carr_len(mounts.dirs), guest, &rest);
  if (!mount || !mount->writable) {
    return OUTPUT_ROOT;
  }
  spn_path_t host = spn_path_join(mem, mount->host, rest);
  spn_dir_t roots [] = { SPN_DIR_WORK, SPN_DIR_LIB, SPN_DIR_BIN, SPN_DIR_VENDOR };
  sp_carr_for(roots, it) {
    spn_path_rel_t rel = spn_path_within(spn_api_dir_path(unit, roots[it]), host);
    if (!rel.within) {
      continue;
    }
    if (sp_str_empty(rel.sub)) {
      return OUTPUT_UNNAMED;
    }
    *dir = roots[it];
    *sub = rel.sub;
    return OUTPUT_OK;
  }
  return OUTPUT_ROOT;
}

static void add_output(spn_node_t* node, const c8* fn, const c8* path, spn_dag_artifact_kind_t kind) {
  spn_user_node_t* info = spn_node_deref(node->ref);
  spn_pkg_unit_t* unit = node->ref.pkg;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_dir_t dir = SPN_DIR_NONE;
  sp_str_t sub = sp_zero;
  output_problem_t problem = find_output(unit, scratch.mem, sp_cstr_as_str(path), &dir, &sub);
  if (problem != OUTPUT_OK) {
    sp_str_t message = sp_fmt(scratch.mem, "{}: {} {}", sp_fmt_cstr(fn), sp_fmt_cstr(path), sp_fmt_str(output_problem_str(problem))).value;
    bool trapped = spn_wasm_trap(message);
    sp_mem_end_scratch(scratch);
    sp_assert(trapped);
    return;
  }
  si_da_push(unit->units->mem, info->outputs, ((spn_user_output_t) {
    .dir = dir,
    .sub = sp_str_copy(spn.mem, sub),
    .kind = kind,
    .path = spn_path_join(spn.mem, spn_api_dir_path(unit, dir), sub),
  }));
  sp_mem_end_scratch(scratch);
}

void spn_node_add_output(spn_node_t* node, const c8* path) {
  add_output(node, "spn_node_add_output", path, SPN_DAG_ARTIFACT_KIND_FILE);
}

void spn_node_add_output_dir(spn_node_t* node, const c8* path) {
  add_output(node, "spn_node_add_output_dir", path, SPN_DAG_ARTIFACT_KIND_TREE);
}

void spn_node_link(spn_node_t* from, spn_node_t* to) {
  spn_user_node_t* info = spn_node_deref(to->ref);
  si_da_push(to->ref.pkg->units->mem, info->deps, from->ref);
}

void spn_node_set_fn(spn_node_t* node, const c8* fn) {
  spn_user_node_t* info = spn_node_deref(node->ref);
  spn_pkg_unit_t* unit = node->ref.pkg;
  if (!unit->metaprogram) {
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    sp_str_t message = sp_fmt(scratch.mem, "spn_node_set_fn: node {} runs {}, but {} has no [package.build] script", SP_FMT_STR(info->tag), SP_FMT_CSTR(fn), SP_FMT_STR(unit->pkg.manifest->name.str)).value;
    bool trapped = spn_wasm_trap(message);
    sp_mem_end_scratch(scratch);
    sp_assert(trapped);
    return;
  }
  info->fn = spn_intern_cstr(fn).str;
}
