#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "codegen/codegen.h"
#include "compiler/driver.h"
#include "error/error.h"
#include "graph/dag.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/types.h"

static void render(sp_mem_t mem, spn_dag_build_t* b, sp_io_writer_t* io) {
  spn_session_t* session = b->session;
  const spn_path_roots_t* roots = b->graph->roots;

  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_om_for(session->units.objects, it) {
    spn_compile_unit_t* unit = sp_om_at(session->units.objects, it);
    spn_cc_compile_files_t files = { .source = unit->file };
    spn_dag_embed_ids_t* embed = sp_ht_getp(b->ids.embeds, unit->target);
    if (embed) {
      spn_dag_artifact_t* header = spn_dag_find_artifact(b->graph, embed->header);
      if (!spn_path_empty(header->materialized)) {
        files.include = spn_path_parent(header->materialized);
      }
    }
    spn_invocation_t invocation = spn_cc_render_compile_source(mem, &unit->plan, &files);
    sp_da(sp_str_t) args = spn_invocation_args(roots, mem, &invocation);

    if (it) {
      sp_io_write_c8(io, ',');
    }
    sp_io_write_cstr(io, "\n  { \"directory\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, session->paths.root));
    sp_io_write_cstr(io, ", \"file\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, unit->file));
    sp_io_write_cstr(io, ", \"arguments\": [", SP_NULLPTR);
    spn_codegen_json_str(io, spn_arg_str(roots, mem, invocation.program));
    sp_da_for(args, arg) {
      sp_io_write_cstr(io, ", ", SP_NULLPTR);
      spn_codegen_json_str(io, args[arg]);
    }
    sp_io_write_cstr(io, "] }", SP_NULLPTR);
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);
}

spn_err_t spn_dag_build_write_compile_commands(spn_dag_build_t* b) {
  spn_session_t* session = b->session;
  const spn_path_roots_t* roots = b->graph->roots;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(s.mem, &buf);
  render(s.mem, b, &buf.base);
  sp_str_t rendered = sp_io_dyn_mem_writer_as_str(&buf);

  spn_path_t path = spn_path_join(s.mem, session->paths.root, sp_str_lit("compile_commands.json"));
  sp_str_t existing = sp_zero;
  sp_io_read_file_at(s.mem, spn_path_at(roots, path), &existing);

  spn_err_t err = SPN_OK;
  if (!sp_str_equal(existing, rendered) && sp_fs_write_atomic_at(spn_path_at(roots, path), rendered)) {
    err = spn_err_emit(session->ctx, (spn_err_union_t) {
      .kind = SPN_ERR_DAG_OUTPUT_WRITE,
      .dag = { .path = spn_path_str(roots, session->ctx->events->mem, path) },
    });
  }
  sp_mem_end_scratch(s);
  return err;
}
