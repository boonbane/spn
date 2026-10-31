#include "sp.h"
#include "macro/macro.h"
#include "cc.h"
#include "ctx/types.h"
#include "spn/core.h"
#include "event/types.h"
#include "core/types.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/node.h"
#include "enum/enum.h"
#include "event/event.h"
#include "paths/paths.h"
#include "pkg/types.h"
#include "session/invocation.h"
#include "session/session.h"
#include "unit/unit.h"
#include "graph/build.h"
#include "toolchain/linker.h"
#include "profile/types.h"
#include "triple/triple.h"

si_da(spn_path_t) si_dag_build_paths(sp_mem_t mem, const si_dag_ctx_t* c, si_da(spn_dag_id_t) ids) {
  si_da(spn_path_t) paths = sp_zero;
  si_da_for(ids, it) {
    si_da_push(mem, paths, si_dag_resolve_input(c, ids[it]));
  }
  return paths;
}
