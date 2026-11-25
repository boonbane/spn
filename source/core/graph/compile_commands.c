#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "codegen/codegen.h"
#include "compiler/driver.h"
#include "error/error.h"
#include "graph/dag.h"
#include "dag/run.h"
#include "paths/paths.h"
#include "graph/invocation.h"
#include "session/types.h"

static spn_err_t write_compile_commands(sp_mem_t mem, spn_session_t* session, const spn_dag_lowered_t* lowered, u32 count) {
  const spn_path_roots_t* roots = &session->ctx->roots;

  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_io_writer_t* io = &buf.base;
  sp_io_write_cstr(io, "[", SP_NULLPTR);
  u32 written = 0;
  sp_for(lt, count) {
    spn_dag_build_t* b = lowered[lt].build;

    si_ht(spn_dag_id_t, si_da(sp_str_t)) answers = sp_zero;
    si_ht_for_kv(b->ids.stdinc, it) {
      spn_dag_id_t id = *it.val;
      spn_path_t location = spn_dag_run_artifact(&b->run, id)->location;
      if (spn_path_empty(location)) {
        continue;
      }
      sp_str_t content = sp_zero;
      if (sp_fs_read_at(mem, spn_path_at(roots, location), &content)) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_DAG_STORE_READ,
          .dag = { .path = spn_path_str(roots, session->ctx->events->mem, location) },
        });
      }
      si_da(sp_str_t) args = sp_zero;
      sp_str_for_split(content, '\n', line) {
        si_da_push(mem, args, line.value);
      }
      si_ht_insert(mem, answers, id, args);
    }

    si_da_for_value(lowered[lt].plan->targets, target) {
      si_dag_build_ids_t* build = si_ht_getp(b->ids.builds, target->pkg->build);
      sp_da_for(target->objects, it) {
        spn_compile_unit_t* unit = target->objects[it];
        si_da(sp_str_t)* stdinc = si_ht_getp(answers, build->stdinc[unit->lang]);
        spn_cc_compile_files_t files = { .source = unit->file };
        spn_dag_embed_ids_t* embed = sp_ht_getp(b->ids.embeds, target);
        if (embed) {
          spn_path_t header = spn_dag_run_artifact(&b->run, embed->header)->location;
          if (!spn_path_empty(header)) {
            files.include = spn_path_parent(header);
          }
        }
        spn_invocation_t invocation = si_cc_render_compile_source(mem, &unit->plan, &files, stdinc ? *stdinc : SP_NULLPTR);
        sp_da(sp_str_t) args = spn_invocation_args(roots, mem, &invocation);

        if (written++) {
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
    }
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);
  sp_str_t rendered = sp_io_dyn_mem_writer_as_str(&buf);

  spn_path_t path = spn_path_project_compile_commands();
  sp_str_t existing = sp_zero;
  sp_fs_read_at(mem, spn_path_at(roots, path), &existing);

  if (!sp_str_equal(existing, rendered) && sp_fs_write_at(spn_path_at(roots, path), rendered, SP_FS_WRITE_REPLACE, sp_sys_default_file_perms)) {
    return spn_err_emit(session->ctx, (spn_err_union_t) {
      .kind = SPN_ERR_DAG_OUTPUT_WRITE,
      .dag = { .path = spn_path_str(roots, session->ctx->events->mem, path) },
    });
  }
  return SPN_OK;
}

spn_err_t spn_dag_build_write_compile_commands(spn_session_t* session, const spn_dag_lowered_t* lowered, u32 count) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = write_compile_commands(s.mem, session, lowered, count);
  sp_mem_end_scratch(s);
  return err;
}
