#include "ctx/types.h"
#include "session/types.h"
#include "unit/types.h"

#include "codegen/codegen.h"
#include "compiler/driver.h"
#include "external/cc.h"
#include "paths/paths.h"
#include "profile/types.h"
#include "session/invocation.h"
#include "session/session.h"
#include "unit/unit.h"
#include "toolchain/search.h"

spn_err_t spn_session_write_compile_commands(const spn_path_roots_t* roots, spn_session_t* session, spn_path_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_io_writer_t* io = &buf.base;

  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_om_for(session->units.objects, it) {
    spn_compile_unit_t* unit = sp_om_at(session->units.objects, it);
    spn_build_unit_t* build = unit->target->pkg->build;
    spn_cc_compile_files_t files = {
      .source = unit->paths.file,
      .output = unit->paths.object,
    };
    spn_invocation_t invocation = spn_cc_render_compile_command(mem, &build->toolchain->cc, &build->profile, spn_session_get_object_plan(session, unit->id), &files);
    sp_da(sp_str_t) args = spn_invocation_args(roots, mem, &invocation);

    if (it) {
      sp_io_write_c8(io, ',');
    }
    sp_io_write_cstr(io, "\n  { \"directory\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, invocation.cwd));
    sp_io_write_cstr(io, ", \"file\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, files.source));
    sp_io_write_cstr(io, ", \"output\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, files.output));
    sp_io_write_cstr(io, ", \"arguments\": [", SP_NULLPTR);
    spn_codegen_json_str(io, spn_arg_str(roots, mem, invocation.program));
    sp_da_for(args, arg) {
      sp_io_write_cstr(io, ", ", SP_NULLPTR);
      spn_codegen_json_str(io, args[arg]);
    }
    sp_io_write_cstr(io, "] }", SP_NULLPTR);
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);

  spn_err_t err = sp_fs_create_file_str_at(spn_path_at(roots, path), sp_io_dyn_mem_writer_as_str(&buf)) ? SPN_ERROR : SPN_OK;
  sp_mem_end_scratch(scratch);
  return err;
}

sp_da(sp_str_t) spn_invocation_args(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation) {
  sp_da(sp_str_t) args = sp_da_new(mem, sp_str_t);
  sp_da_reserve(args, sp_da_size(invocation->args));
  sp_da_for(invocation->args, it) {
    sp_da_push(args, spn_arg_str(roots, mem, invocation->args[it]));
  }
  return args;
}

typedef struct {
  sp_str_t name;
  sp_str_t separator;
} env_key_t;

static env_key_t env_key(spn_env_key_t key) {
  switch (key) {
    case SPN_ENV_INCLUDE: return (env_key_t) { sp_str_lit("INCLUDE"), sp_str_lit(";") };
    case SPN_ENV_LIB: return (env_key_t) { sp_str_lit("LIB"), sp_str_lit(";") };
    case SPN_ENV_ZIG_LIBC: return (env_key_t) { sp_str_lit("ZIG_LIBC"), sp_str_lit("") };
    case SPN_ENV_ZIG_GLOBAL_CACHE_DIR: return (env_key_t) { sp_str_lit("ZIG_GLOBAL_CACHE_DIR"), sp_str_lit("") };
    case SPN_ENV_ZIG_LOCAL_CACHE_DIR: return (env_key_t) { sp_str_lit("ZIG_LOCAL_CACHE_DIR"), sp_str_lit("") };
  }
  sp_unreachable_return(sp_zero_struct(env_key_t));
}

sp_env_var_t spn_invocation_env_var(const spn_path_roots_t* roots, sp_mem_t mem, spn_invocation_env_t env) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  env_key_t key = env_key(env.key);
  sp_da(sp_str_t) values = sp_da_new(scratch.mem, sp_str_t);
  sp_da_for(env.values, it) {
    sp_da_push(values, spn_arg_str(roots, scratch.mem, env.values[it]));
  }
  sp_str_t value = sp_str_join_n(mem, values, sp_da_size(values), key.separator);
  sp_mem_end_scratch(scratch);
  return (sp_env_var_t) { .key = key.name, .value = value };
}

sp_str_t spn_invocation_to_str(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_da(sp_str_t) parts = sp_da_new(scratch.mem, sp_str_t);
  sp_da_for(invocation->env, it) {
    sp_env_var_t var = spn_invocation_env_var(roots, scratch.mem, invocation->env[it]);
    sp_da_push(parts, sp_fmt(scratch.mem, "{}={}", sp_fmt_str(var.key), sp_fmt_str(var.value)).value);
  }
  sp_da_push(parts, spn_arg_str(roots, scratch.mem, invocation->program));
  sp_da_for(invocation->args, it) {
    sp_da_push(parts, spn_arg_str(roots, scratch.mem, invocation->args[it]));
  }

  sp_str_t command = sp_str_join_n(mem, parts, sp_da_size(parts), sp_str_lit(" "));
  sp_mem_end_scratch(scratch);
  return command;
}

static sp_env_var_t path_var(sp_mem_t mem, sp_str_t program) {
  sp_assert(sp_fs_is_absolute(program));
  spn_search_rules_t rules = spn_search_rules(spn.host.os);
  sp_str_t path = sp_env_get(spn.env, sp_str_lit("PATH"));
  return (sp_env_var_t) {
    .key = sp_str_lit("PATH"),
    .value = spn_search_prepend(rules, mem, sp_fs_parent_path(program), path),
  };
}

static void ps_env_push(sp_ps_config_t* ps, sp_env_var_t var) {
  u32 slot = 0;
  while (slot < SP_PS_MAX_ENV && !sp_str_empty(ps->env.extra[slot].key)) {
    slot++;
  }
  sp_assert(slot < SP_PS_MAX_ENV);
  ps->env.extra[slot] = var;
}

static sp_ps_config_t invocation_ps(const spn_path_roots_t* roots, const spn_invocation_t* invocation, sp_mem_t mem) {
  sp_fs_create_dir_at(spn_path_at(roots, invocation->cwd));

  sp_ps_config_t ps = {
    .command = spn_arg_str(roots, mem, invocation->program),
    .dyn_args = spn_invocation_args(roots, mem, invocation),
    .cwd = spn_path_str(roots, mem, invocation->cwd),
    .io = {
      .in.mode = SP_PS_IO_MODE_NULL,
      .out.mode = SP_PS_IO_MODE_CREATE,
      .err.mode = SP_PS_IO_MODE_REDIRECT,
    }
  };
  sp_da_for(invocation->env, it) {
    ps_env_push(&ps, spn_invocation_env_var(roots, mem, invocation->env[it]));
  }
  ps_env_push(&ps, path_var(mem, ps.command));
  return ps;
}

spn_invocation_result_t spn_invocation_run(const spn_path_roots_t* roots, spn_invocation_t* invocation) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_ps_config_t ps = invocation_ps(roots, invocation, scratch.mem);

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_ps_output_t result = sp_ps_run(spn.mem, ps);
  u64 elapsed = sp_tm_read_timer(&timer);
  sp_mem_end_scratch(scratch);

  return (spn_invocation_result_t) {
    .result = result,
    .elapsed = elapsed,
  };
}

spn_invocation_result_t spn_invocation_run_progress(const spn_path_roots_t* roots, spn_invocation_t* invocation, spn_path_t log, sp_io_writer_t* progress) {
#if defined(SP_WIN32)
  return spn_invocation_run(roots, invocation);
#else
  spn_invocation_result_t failed = { .result = { .status = { .state = SP_PS_STATE_DONE, .exit_code = -1 } } };

  sp_sys_pipe_t pipe = sp_zero;
  if (sp_sys_pipe(&pipe, (sp_sys_pipe_desc_t) { .w = SP_SYS_INHERITED })) {
    return failed;
  }

  sp_path_t at = spn_path_at(roots, log);
  sp_sys_fd_t sink = SP_SYS_INVALID_FD;
  if (sp_sys_open_s(at.dir, at.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_TRUNCATE, &sink)) {
    sp_sys_close(pipe.r);
    sp_sys_close(pipe.w);
    return failed;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_ps_config_t ps = invocation_ps(roots, invocation, scratch.mem);
  ps.io.out = (sp_ps_io_out_config_t) { .mode = SP_PS_IO_MODE_EXISTING, .fd = sink };
  ps_env_push(&ps, (sp_env_var_t) {
    .key = sp_str_lit("ZIG_PROGRESS"),
    .value = sp_fmt(scratch.mem, "{}", sp_fmt_uint((u64)pipe.w)).value,
  });

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_ps_t child = sp_ps_create(spn.mem, ps);
  sp_sys_close(pipe.w);
  if (!child.os) {
    sp_sys_close(pipe.r);
    sp_sys_close(sink);
    sp_mem_end_scratch(scratch);
    return failed;
  }

  u8 buf [4096];
  while (true) {
    u64 bytes = 0;
    if (sp_sys_read(pipe.r, buf, sizeof(buf), &bytes) || !bytes) {
      break;
    }
    sp_io_write(progress, buf, bytes, SP_NULLPTR);
  }
  sp_sys_close(pipe.r);

  sp_ps_output_t output = { .status = sp_ps_wait(&child) };
  u64 elapsed = sp_tm_read_timer(&timer);
  sp_ps_free(&child);
  sp_sys_close(sink);
  if (output.status.exit_code) {
    sp_io_read_file_at(spn.mem, at, &output.out);
  }
  sp_mem_end_scratch(scratch);

  return (spn_invocation_result_t) {
    .result = output,
    .elapsed = elapsed,
  };
#endif
}
