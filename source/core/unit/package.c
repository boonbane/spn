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

// @spader I think this is wrong; it's called in four places and deduplicated with an atomic,
// but really we just want to add one graph node to log before anything in a package is compiled.
// I think of of the existing nodes would even suffice for this.
void spn_pkg_unit_announce_compile(spn_pkg_unit_t* unit) {
  if (!sp_atomic_s32_cas(&unit->compile_announced, 0, 1, SP_ATOMIC_SEQ_CST)) return;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_COMPILE_START,
    .pkg = unit->info->name,
    .compile_start = {
      .version = spn_semver_to_str(spn.mem, unit->info->version),
    },
  });
}
