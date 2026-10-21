#include "unit/package.h"

#include "core/core.h"
#include "ctx/ctx.h"
#include "sp.h"
#include "spn/core.h"
#include "event/event.h"
#include "external/cc.h"
#include "external/git.h"
#include "external/tom.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "semver/convert.h"
#include "session/session.h"
#include "unit/types.h"
#include "unit/unit.h"

spn_user_output_t spn_pkg_unit_node_stamp(spn_pkg_unit_t* ctx, spn_user_node_t* node) {
  return (spn_user_output_t) {
    .dir = SPN_DIR_WORK,
    .sub = sp_fs_join_path(spn.mem, sp_str_lit("stamp"), node->tag),
    .kind = SPN_DAG_ARTIFACT_KIND_FILE,
    .path = spn_path_join(spn.mem, ctx->paths.stamp, node->tag),
    .stamp = true,
  };
}

