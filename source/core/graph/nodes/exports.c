#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"

#include "compiler/driver.h"
#include "compiler/toc.h"
#include "dag/node.h"
#include "error/error.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "hash_table/hash_table.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "session/invocation.h"

static spn_err_t write_exports(sp_mem_t mem, const si_dag_ctx_t* c, si_exports_t* exports) {
  const spn_path_roots_t* roots = c->roots;

  spn_path_t output = si_dag_resolve_output(c, exports->output);
  spn_path_t archive = spn_path_concat(mem, output, ".a");
  spn_cc_archive_files_t files = { .cwd = c->scratch, .output = archive };
  switch (exports->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(mem, c, exports->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = si_dag_resolve_input(c, exports->objects.rsp);
      break;
    }
  }
  spn_invocation_t invocation = spn_cc_render_archive_command(mem, exports->archive, &files);

  spn_invocation_result_t run = spn_invocation_run(roots, &invocation);
  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .link_failed = { .subject = exports->subject, .run = spn_event_run(roots, &invocation, &run) },
    });
    return SPN_ERR_DAG_ACTION;
  }

  si_da(spn_path_t) archives = sp_zero;
  si_da_push(mem, archives, archive);
  si_da_for(exports->whole_archives, it) {
    si_da_push(mem, archives, si_dag_resolve_input(c, exports->whole_archives[it]));
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
  spn_cc_render_exports(&writer.base, roots, exports->format, exports->rsp, exports->library, symbols);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}

spn_err_t si_on_write_exports(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = write_exports(s.mem, c, (si_exports_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
