#include "spn/errors.h"

#include "dag/dag.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "toolchain/libc.h"

spn_err_t si_on_write_libc(const si_dag_ctx_t* c, void* user_data) {
  si_libc_t* libc = (si_libc_t*)user_data;

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(c->roots, si_dag_resolve_output(c, libc->output)))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  spn_libc_render(&writer.base, c->roots, libc->libc);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}
