#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "compiler/toc.h"
#include "dag/dag.h"
#include "error/error.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "hash_table/hash_table.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "unit/package.h"

static spn_err_t write_exports(sp_mem_t mem, spn_dag_t* g, si_exports_t* ctx, spn_path_t output) {
  spn_target_unit_t* target = ctx->target;
  spn_build_unit_t* build = target->pkg->build;
  const spn_path_roots_t* roots = g->roots;

  spn_path_t archive = spn_path_concat(mem, output, ".a");
  spn_cc_archive_files_t files = { .output = archive };
  switch (ctx->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(mem, g, ctx->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = spn_dag_find_artifact(g, ctx->objects.rsp)->materialized;
      break;
    }
  }
  spn_invocation_t invocation = sp_zero;
  si_cc_render_archive(mem, &build->toolchain->cc, &build->profile, &files, &invocation);
  invocation.cwd = target->pkg->paths.work;

  spn_invocation_result_t run = spn_invocation_run(roots, &invocation);
  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .pkg = target->pkg->info->name.str,
      .link_failed = {
        .target = target->info->name.str,
        .exit_code = run.result.status.exit_code,
        .command = spn_invocation_to_str(roots, spn.events->mem, &invocation),
        .out = run.result.out,
        .err = run.result.err,
      }
    });
    return SPN_ERR_DAG_ACTION;
  }

  si_da(spn_path_t) archives = sp_zero;
  si_da_push(mem, archives, archive);
  si_da_for(ctx->whole_archives, it) {
    si_da_push(mem, archives, spn_dag_find_artifact(g, ctx->whole_archives[it])->materialized);
  }

  si_str_ht(u8) seen = sp_zero;
  si_da(sp_str_t) symbols = sp_zero;
  si_da_for(archives, it) {
    sp_io_file_reader_t reader = sp_zero;
    if (sp_io_file_reader_from_path_at(&reader, spn_path_at(roots, archives[it]))) {
      spn_err_emit(&spn, (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs.path = spn_path_copy(spn.events->mem, archives[it]) });
      return SPN_ERR_DAG_ACTION;
    }

    spn_toc_parser_t toc = sp_zero;
    spn_err_t err = spn_toc_init(&toc, &reader.base);
    sp_str_t symbol = sp_zero;
    while (spn_toc_next(&toc, &symbol)) {
      sp_str_t interned = spn_intern(symbol).str;
      if (si_str_ht_get(seen, interned)) {
        continue;
      }
      si_str_ht_insert(mem, seen, interned, (u8)true);
      si_da_push(mem, symbols, interned);
    }
    if (!err) {
      err = toc.err;
    }
    sp_io_file_reader_close(&reader);
    if (err) {
      spn_err_emit(&spn, (spn_err_union_t) { .kind = err, .fs.path = spn_path_copy(spn.events->mem, archives[it]) });
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(roots, output))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  spn_cc_render_exports(&writer.base, roots, &build->toolchain->cc, &build->profile, target->kind, target->info->name.str, symbols);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}

spn_err_t si_on_write_exports(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_exports_t* ctx = (si_exports_t*)user_data;

  spn_pkg_unit_announce_compile(ctx->target->pkg);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = write_exports(s.mem, g, ctx, outputs[0]);
  sp_mem_end_scratch(s);
  return err;
}
