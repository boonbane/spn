#include "ctx/types.h"
#include "event/event.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "external/zig.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "triple/triple.h"
#include "graph/nodes/nodes.h"

static s32 run_stub(spn_build_unit_t* build, const spn_zig_stub_t* stub, sp_str_t name, sp_str_t dir, sp_mem_t mem) {
  spn_profile_info_t* profile = &build->profile;
  spn_triple_t triple = spn_profile_triple(profile);
  sp_str_t triple_str = spn_triple_to_str(spn.mem, triple);
  spn_cc_toolchain_t* cc = &build->toolchain->cc;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_START,
    .warm = {
      .toolchain = cc->name,
      .triple = triple_str,
      .stub = name,
    },
  });

  sp_str_t source = sp_fs_join_path(mem, dir, sp_str_lit("stub.c"));
  if (sp_fs_create_file_str(source, sp_str_lit("int main(void) { return 0; }\n"))) {
    return 1;
  }

  spn_cc_link_t link = {
    .lang = stub->lang,
    .kind = stub->kind,
    .system_libs = stub->system_libs,
  };
  spn_cc_link_files_t files = {
    .output = spn_path_make(&spn.roots, sp_fs_join_path(mem, dir, sp_str_lit("stub.bin"))),
  };
  sp_da_init(mem, files.objects);
  sp_da_push(files.objects, spn_path_make(&spn.roots, source));

  spn_invocation_t invocation = sp_zero;
  if (spn_cc_render_link(mem, cc, spn.host, profile, &link, &files, &invocation)) {
    return 1;
  }
  invocation.cwd = spn_path_make(&spn.roots, dir);

  spn_invocation_result_t run = spn_invocation_run(&invocation);
  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_WARM_FAILED,
      .warm_failed = {
        .toolchain = cc->name,
        .triple = triple_str,
        .stub = name,
        .rc = run.result.status.exit_code,
        .command = spn_invocation_to_str(spn.mem, &invocation),
        .out = run.result.out,
      },
    });
  }
  return run.result.status.exit_code;
}

s32 spn_warm_stub_run(spn_build_unit_t* build, const spn_zig_stub_t* stub, sp_str_t name, spn_path_t stamp, spn_path_t output) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  sp_str_t out = spn_path_str(&spn.roots, mem, output);
  s32 rc = 0;
  if (!sp_fs_exists(spn_path_str(&spn.roots, mem, stamp))) {
    rc = run_stub(build, stub, name, sp_fs_parent_path(out), mem);
  }
  if (!rc && sp_fs_create_file_str(out, name)) {
    rc = 1;
  }

  sp_mem_end_scratch(scratch);
  return rc;
}
