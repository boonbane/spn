#include "ctx/types.h"
#include "spn/errors.h"
#include "spn/core.h"
#include "unit/types.h"
#include "session/types.h"

#include "external/cc.h"
#include "compiler/driver.h"
#include "compiler/exports.h"
#include "compiler/toc.h"
#include "dag/dag.h"
#include "error/error.h"
#include "event/event.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "unit/package.h"
#include "str/str.h"
#include "unit/unit.h"

static spn_err_t run_link(spn_target_unit_t* target, spn_invocation_t* invocation, spn_invocation_result_t* run) {
  *run = spn_invocation_run(invocation);
  if (!run->result.status.exit_code) {
    return SPN_OK;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_FAILED,
    .pkg = target->pkg->info->name,
    .link_failed = {
      .target = target->info->name,
      .exit_code = run->result.status.exit_code,
      .command = spn_invocation_to_str(spn.mem, invocation),
      .out = run->result.out,
      .err = run->result.err,
    }
  });
  return SPN_ERROR;
}

static spn_err_t run_target(sp_mem_t scratch, spn_target_unit_t* target, spn_invocation_t* invocation) {
  spn_pkg_unit_announce_compile(target->pkg);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_START,
    .pkg = target->pkg->info->name,
    .link_start = {
      .target = target->info->name,
    }
  });

  spn_invocation_result_t run = sp_zero;
  spn_try(run_link(target, invocation, &run));

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_PASSED,
    .pkg = target->pkg->info->name,
    .link_passed = {
      .target = target->info->name,
      .output_path = spn_path_str(&spn.roots, spn.mem, spn_target_output_path(scratch, target)),
      .command = spn_invocation_to_str(spn.mem, invocation),
      .out = run.result.out,
      .time = run.elapsed,
    }
  });
  return SPN_OK;
}

typedef sp_str_ht(u8) spn_symbol_set_t;

static spn_err_t read_archive_symbols(spn_path_t path, sp_da(sp_str_t)* symbols, spn_symbol_set_t* seen) {
  sp_io_file_reader_t reader = sp_zero;
  if (spn_path_open_reader(&spn.roots, path, &reader)) {
    return spn_err_emit(&spn, (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs.path = spn_path_str(&spn.roots, spn.mem, path) });
  }

  spn_toc_parser_t toc;
  spn_err_t err = spn_toc_init(&toc, &reader.base);

  sp_str_t symbol = sp_zero;
  while (spn_toc_next(&toc, &symbol)) {
    sp_str_t interned = spn_intern(symbol);
    if (sp_str_ht_exists(*seen, interned)) {
      continue;
    }
    sp_str_ht_insert(*seen, interned, (u8)true);
    sp_da_push(*symbols, interned);
  }
  if (!err) {
    err = toc.err;
  }

  sp_io_file_reader_close(&reader);
  if (err) {
    return spn_err_emit(&spn, (spn_err_union_t) { .kind = err, .fs.path = spn_path_str(&spn.roots, spn.mem, path) });
  }
  return SPN_OK;
}

static spn_err_t link_target_exec(sp_mem_t scratch, spn_target_unit_t* target, spn_cc_link_files_t files) {
  if (target->kind == SPN_CC_OUTPUT_REACTOR) {
    sp_str_t content = sp_zero;
    if (spn_path_read(&spn.roots, scratch, files.exports.path, &content)) {
      return spn_err_emit(&spn, (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs.path = spn_path_str(&spn.roots, spn.mem, files.exports.path) });
    }
    sp_da_init(scratch, files.exports.symbols);
    sp_str_for_line(content, line) {
      if (!sp_str_empty(line.line)) {
        sp_da_push(files.exports.symbols, line.line);
      }
    }
    files.exports.path = (spn_path_t) sp_zero;
  }

  spn_invocation_t invocation = spn_target_link_invocation(scratch, target, &files);
  return run_target(scratch, target, &invocation);
}

spn_err_t on_archive_target(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_archive_ctx_t* archive = (spn_dag_archive_ctx_t*)user_data;

  spn_cc_archive_files_t files = archive->files;
  files.output = outputs[0];

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_invocation_t invocation = spn_target_archive_invocation(scratch.mem, archive->target, &files);
  spn_err_t result = run_target(scratch.mem, archive->target, &invocation);
  sp_mem_end_scratch(scratch);
  return result ? SPN_ERR_DAG_ACTION : SPN_OK;
}

spn_err_t on_link_target(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_link_ctx_t* link = (spn_dag_link_ctx_t*)user_data;

  spn_cc_link_files_t files = link->files;
  files.output = outputs[0];
  if (!spn_path_empty(files.implib)) {
    files.implib = outputs[1];
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t result = link_target_exec(scratch.mem, link->target, files);
  sp_mem_end_scratch(scratch);
  return result ? SPN_ERR_DAG_ACTION : SPN_OK;
}















static spn_err_t link_exports_exec(sp_mem_t s, spn_target_unit_t* target, spn_cc_archive_files_t files, spn_path_t output) {
  spn_invocation_t invocation = spn_target_archive_invocation(s, target, &files);

  spn_invocation_result_t run = sp_zero;
  spn_try(run_link(target, &invocation, &run));

  spn_symbol_set_t seen;
  sp_str_ht_init(s, seen);
  sp_da(sp_str_t) symbols = sp_da_new(s, sp_str_t);
  spn_try(read_archive_symbols(files.output, &symbols, &seen));
  sp_da_for(target->link.archives, it) {
    spn_try(read_archive_symbols(target->link.archives[it], &symbols, &seen));
  }

  sp_io_file_writer_t writer = sp_zero;
  if (spn_path_open_writer(&spn.roots, output, &writer)) {
    // @spader I hate this error. Ultimately useless.
    return spn_err_emit(&spn, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs.path = spn_path_str(&spn.roots, spn.mem, output) });
  }

  switch (spn_target_exports_format(target)) {
    case SPN_CC_EXPORTS_VERSION_SCRIPT: {
      spn_exports_render_version_script(&writer.base, symbols);
      break;
    }
    case SPN_CC_EXPORTS_SYMBOL_LIST:
    case SPN_CC_EXPORTS_WASM: {
      spn_exports_render_symbol_list(&writer.base, symbols);
      break;
    }
    case SPN_CC_EXPORTS_DEF: {
      spn_exports_render_def(&writer.base, target->info->name, symbols);
      break;
    }
  }
  sp_io_file_writer_close(&writer);

  return SPN_OK;
}

spn_err_t on_render_exports(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_archive_ctx_t* archive = (spn_dag_archive_ctx_t*)user_data;

  spn_pkg_unit_announce_compile(archive->target->pkg);

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_cc_archive_files_t files = archive->files;
  files.output = spn_target_exports_archive(scratch.mem, outputs[0]);
  spn_err_t result = link_exports_exec(scratch.mem, archive->target, files, outputs[0]);
  sp_mem_end_scratch(scratch);
  return result ? SPN_ERR_DAG_ACTION : SPN_OK;
}

