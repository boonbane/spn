#include "ctx/types.h"
#include "core/core.h"
#include "event/event.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "external/zig.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "triple/triple.h"
#include "graph/nodes/nodes.h"

static void pump(sp_sys_fd_t fd, spn_dag_env_t* env, sp_mem_t mem) {
  spn_zig_progress_t* progress = sp_alloc_type(mem, spn_zig_progress_t);
  spn_zig_progress_init(progress);

  u64 last = 0;
  u8 buf [4096] = sp_zero;
  while (true) {
    u64 bytes = 0;
    if (sp_sys_read(fd, buf, sizeof(buf), &bytes) || !bytes) {
      break;
    }
    if (!spn_zig_progress_feed(progress, buf, bytes) || !env->progress) {
      continue;
    }

    u64 ticks = spn_zig_progress_ticks(progress);
    sp_atomic_u64_add(&env->progress->warm, ticks - last, SP_ATOMIC_SEQ_CST);
    last = ticks;
    if (env->wake) {
      spn_wake_ring(env->wake);
    }
  }

  if (env->progress && last) {
    sp_atomic_u64_add(&env->progress->warm, 0 - last, SP_ATOMIC_SEQ_CST);
    if (env->wake) {
      spn_wake_ring(env->wake);
    }
  }
}

static sp_ps_output_t stub_exec(spn_invocation_t* invocation, sp_str_t dir, spn_dag_env_t* env, sp_mem_t mem) {
  sp_sys_pipe_t pipe = sp_zero;
  if (sp_sys_pipe(&pipe, (sp_sys_pipe_desc_t) { .w = SP_SYS_INHERITED })) {
    return spn_invocation_run(invocation).result;
  }

  sp_str_t log = sp_fs_join_path(mem, dir, sp_str_lit("log"));
  sp_sys_fd_t sink = sp_zero;
  if (sp_sys_open_s(sp_sys_get_root(0), log, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_TRUNCATE, &sink)) {
    sp_sys_close(pipe.r);
    sp_sys_close(pipe.w);
    return spn_invocation_run(invocation).result;
  }

  sp_ps_config_t ps = spn_invocation_ps(invocation, mem);
  ps.io.out = (sp_ps_io_out_config_t) { .mode = SP_PS_IO_MODE_EXISTING, .fd = sink };

  // spn_invocation_ps fills the invocation's env and then PATH; the progress fd goes after both
  u64 vars = sp_da_size(invocation->env) + 1;
  sp_assert(vars + 1 <= SP_PS_MAX_ENV);
  ps.env.extra[vars] = (sp_env_var_t) {
    .key = sp_str_lit("ZIG_PROGRESS"),
    .value = sp_fmt(mem, "{}", sp_fmt_uint((u64)pipe.w)).value,
  };

  sp_ps_t child = sp_ps_create(spn.mem, ps);
  sp_sys_close(pipe.w);
  if (!child.os) {
    sp_sys_close(pipe.r);
    sp_sys_close(sink);
    return (sp_ps_output_t) { .status = { .state = SP_PS_STATE_DONE, .exit_code = -1 } };
  }

  pump(pipe.r, env, mem);
  sp_sys_close(pipe.r);

  sp_ps_status_t status = sp_ps_wait(&child);
  sp_ps_free(&child);
  sp_sys_close(sink);

  sp_ps_output_t output = { .status = status };
  if (status.exit_code) {
    sp_io_read_file(spn.mem, log, &output.out);
  }
  return output;
}

static void warm_failed(spn_cc_toolchain_t* cc, sp_str_t triple, sp_str_t name, s32 rc, sp_str_t command, sp_str_t out) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_FAILED,
    .warm_failed = {
      .toolchain = cc->name,
      .triple = triple,
      .stub = name,
      .rc = rc,
      .command = command,
      .out = out,
    },
  });
}

static s32 run_stub(spn_build_unit_t* build, const spn_zig_stub_t* stub, sp_str_t name, sp_str_t triple, sp_str_t dir, spn_dag_env_t* env, sp_mem_t mem) {
  spn_cc_toolchain_t* cc = &build->toolchain->cc;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_START,
    .warm = {
      .toolchain = cc->name,
      .triple = triple,
      .stub = name,
    },
  });

  sp_str_t source = sp_fs_join_path(mem, dir, sp_str_lit("stub.c"));
  if (sp_fs_create_file_str(source, sp_str_lit("int main(void) { return 0; }\n"))) {
    warm_failed(cc, triple, name, 1, sp_str_lit(""), sp_fmt(spn.mem, "failed to write {}", sp_fmt_str(source)).value);
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
  if (spn_cc_render_link(mem, cc, spn.host, &build->profile, &link, &files, &invocation)) {
    return 1;
  }
  invocation.cwd = spn_path_make(&spn.roots, dir);

  sp_ps_output_t run = stub_exec(&invocation, dir, env, mem);
  if (run.status.exit_code) {
    warm_failed(cc, triple, name, run.status.exit_code, spn_invocation_to_str(spn.mem, &invocation), run.out);
  }
  return run.status.exit_code;
}

s32 spn_warm_stub_run(spn_build_unit_t* build, const spn_zig_stub_t* stub, sp_str_t name, spn_path_t stamp, spn_path_t output, spn_dag_env_t* env) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  spn_profile_info_t* profile = &build->profile;
  spn_triple_t triple = { profile->arch, profile->os, profile->abi };
  sp_str_t triple_str = spn_triple_to_str(spn.mem, triple);

  sp_str_t out = spn_path_str(&spn.roots, mem, output);
  s32 rc = 0;
  if (!sp_fs_exists(spn_path_str(&spn.roots, mem, stamp))) {
    rc = run_stub(build, stub, name, triple_str, sp_fs_parent_path(out), env, mem);
  }
  if (!rc && sp_fs_create_file_str(out, name)) {
    warm_failed(&build->toolchain->cc, triple_str, name, 1, sp_str_lit(""), sp_fmt(spn.mem, "failed to write {}", sp_fmt_str(out)).value);
    rc = 1;
  }

  sp_mem_end_scratch(scratch);
  return rc;
}
