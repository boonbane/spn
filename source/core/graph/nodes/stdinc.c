#include "ctx/types.h"
#include "event/event.h"
#include "spn/core.h"
#include "spn/err.h"

#include "cdb_fragment.gen.h"
#include "compiler/driver.h"
#include "compiler/stdinc.h"
#include "dag/node.h"
#include "graph/invocation.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "reflect/reflect.h"

static const c8* const fragment_flags [] = {
  "-nostdinc",
  "-nostdinc++",
};

static const c8* const fragment_options [] = {
  "-isystem",
  "-iframework",
  "-D",
};

static bool is_listed(const c8* const* names, u32 count, sp_str_t arg) {
  sp_for(it, count) {
    if (sp_str_equal_cstr(arg, names[it])) {
      return true;
    }
  }
  return false;
}

static spn_cc_stdinc_files_t stdinc_files(const si_dag_ctx_t* c, const si_stdinc_t* stdinc) {
  spn_cc_stdinc_files_t files = { .cwd = c->scratch };
  if (stdinc->libc.occupied) {
    files.libc = si_dag_resolve_input(c, stdinc->libc);
  }
  return files;
}

static spn_err_t fail(const si_dag_ctx_t* c, const si_stdinc_t* stdinc, const spn_invocation_t* invocation, const spn_invocation_result_t* run) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_STDINC_FAILED,
    .stdinc_failed = {
      .toolchain = stdinc->toolchain,
      .run = spn_event_run(c->roots, invocation, run),
    },
  });
  return SPN_ERR_DAG_ACTION;
}

static spn_err_t write_args(sp_mem_t mem, const si_dag_ctx_t* c, const si_stdinc_t* stdinc, si_da(sp_str_t) args) {
  sp_str_t content = sp_str_join_n(mem, args, (u32)si_da_size(args), sp_str_lit("\n"));
  if (sp_fs_write_at(si_dag_output_at(c, stdinc->output), content, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms)) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  return SPN_OK;
}

static spn_err_t ask_fragment(sp_mem_t mem, const si_dag_ctx_t* c, const si_stdinc_t* stdinc) {
  spn_cc_stdinc_files_t files = stdinc_files(c, stdinc);
  files.fragment = spn_path_join(mem, c->scratch, sp_str_lit("fragment.json"));
  spn_invocation_t query = si_cc_render_stdinc_command(mem, &stdinc->plan.query, &files);
  spn_invocation_result_t run = spn_invocation_run(c->roots, c->children, &query);

  sp_str_t content = sp_zero;
  if (run.result.status.exit_code || sp_fs_read_at(mem, spn_path_at(c->roots, files.fragment), &content)) {
    return fail(c, stdinc, &query, &run);
  }
  spn_cg_cdb_fragment_t fragment = sp_zero;
  sp_str_t json = sp_str_strip_right(sp_str_trim(content), sp_str_lit(","));
  if (spn_reflect_json_read(json, &spn_reflect_cdb_fragment, &fragment, mem)) {
    return fail(c, stdinc, &query, &run);
  }

  si_da(sp_str_t) args = sp_zero;
  u32 count = (u32)sp_da_size(fragment.arguments);
  u32 it = 0;
  while (it < count) {
    sp_str_t arg = fragment.arguments[it++];
    if (is_listed(fragment_flags, sp_carr_len(fragment_flags), arg)) {
      si_da_push(mem, args, arg);
    }
    else if (is_listed(fragment_options, sp_carr_len(fragment_options), arg) && it < count) {
      si_da_push(mem, args, sp_str_concat(mem, arg, fragment.arguments[it++]));
    }
  }
  return write_args(mem, c, stdinc, args);
}

static spn_err_t ask_search_list(sp_mem_t mem, const si_dag_ctx_t* c, const si_stdinc_t* stdinc) {
  spn_cc_stdinc_files_t files = stdinc_files(c, stdinc);
  spn_invocation_t builtin = si_cc_render_stdinc_command(mem, &stdinc->plan.builtin, &files);
  spn_invocation_result_t dir = spn_invocation_run(c->roots, c->children, &builtin);
  if (dir.result.status.exit_code) {
    return fail(c, stdinc, &builtin, &dir);
  }

  spn_invocation_t query = si_cc_render_stdinc_command(mem, &stdinc->plan.query, &files);
  spn_invocation_result_t list = spn_invocation_run(c->roots, c->children, &query);
  si_da(sp_str_t) args = sp_zero;
  if (list.result.status.exit_code || si_cc_parse_search_list(mem, list.result.out, sp_str_trim(dir.result.out), &args)) {
    return fail(c, stdinc, &query, &list);
  }
  return write_args(mem, c, stdinc, args);
}

spn_err_t si_on_stdinc_fragment(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = ask_fragment(s.mem, c, (si_stdinc_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}

spn_err_t si_on_stdinc_search_list(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = ask_search_list(s.mem, c, (si_stdinc_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
