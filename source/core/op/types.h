#ifndef SPN_OP_TYPES_H
#define SPN_OP_TYPES_H

#include "sp.h"
#include "queue/queue.h"
#include "spn/types.h"

#include "core/types.h"

struct spn_op_t {
  sp_queue_node_t node;
  spn_ctx_t* ctx;
  spn_session_t* session;
  sp_mem_t mem;

  union {
    spn_add_request_t add;
    spn_publish_request_t publish;
    spn_sync_request_t indexes;
    spn_scaffold_request_t scaffold;
    spn_path_request_t path;
    spn_update_request_t update;
    spn_clean_request_t clean;
  } request;

  spn_op_result_t result;
  sp_atomic_s32_t cancelled;
  spn_children_t children;
  sp_atomic_u32_t done;
};

#endif
