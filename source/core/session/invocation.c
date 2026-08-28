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
#include "graph/build.h"
#include "toolchain/search.h"
#include "triple/triple.h"

static spn_cc_compile_t compile_desc(sp_mem_t mem, spn_compile_unit_t* unit) {
  spn_pkg_unit_t* pkg = unit->target->pkg;
  spn_build_unit_t* build = pkg->build;

  spn_triple_t target = spn_profile_triple(&build->profile);
  spn_cc_compile_t compile = {
    .lang = unit->lang,
    .cxx = unit->target->info->cxx,
    .pic = unit->target->info->kind == SPN_TARGET_KIND_LIB && spn_triple_pic(target),
  };
  if (build->profile.os == SPN_OS_MACOS) {
    compile.min_os = unit->target->link.cc.min_os;
  }
  compile.include = unit->target->include;
  sp_da_init(mem, compile.define);
  sp_da_init(mem, compile.args);

  sp_da_for(build->define, it) {
    sp_da_push(compile.define, build->define[it]);
  }
  sp_da_for(pkg->info->define, it) {
    sp_da_push(compile.define, pkg->info->define[it]);
  }
  sp_da_for(unit->target->info->define, it) {
    sp_da_push(compile.define, unit->target->info->define[it]);
  }
  sp_da_for(unit->target->info->flags, it) {
    sp_da_push(compile.args, unit->target->info->flags[it]);
  }
  sp_da_for(pkg->deps, it) {
    if (!spn_dep_kind_applies(pkg->deps[it].kind, unit->target->info->kind)) {
      continue;
    }
    sp_da_for(pkg->deps[it].unit->info->public_define, jt) {
      sp_da_push(compile.define, pkg->deps[it].unit->info->public_define[jt]);
    }
  }

  return compile;
}

spn_err_t spn_build_render_compile(sp_mem_t mem, spn_compile_unit_t* unit, spn_invocation_t* invocation) {
  spn_pkg_unit_t* pkg = unit->target->pkg;
  spn_build_unit_t* build = pkg->build;

  spn_cc_compile_t compile = compile_desc(mem, unit);
  spn_cc_render_compile(mem, &build->toolchain->cc, &build->profile, &compile, invocation);
  invocation->cwd = pkg->paths.work;
  return SPN_OK;
}

spn_err_t spn_pkg_unit_write_compile_commands(const spn_path_roots_t* roots, spn_pkg_unit_t* unit, sp_str_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_io_writer_t* io = &buf.base;

  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_da(spn_compile_unit_t*) objects = spn_pkg_unit_objects(mem, unit);
  sp_da_for(objects, it) {
    spn_compile_unit_t* object = objects[it];
    spn_build_unit_t* build = object->target->pkg->build;
    spn_cc_compile_files_t files = {
      .source = object->paths.file,
      .output = object->paths.object,
    };
    spn_invocation_t invocation = spn_cc_render_compile_command(mem, &build->toolchain->cc, &build->profile, &object->invocation, &files);
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

  spn_err_t err = sp_fs_create_file_str(path, sp_io_dyn_mem_writer_as_str(&buf)) ? SPN_ERROR : SPN_OK;
  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_compile_commands_merge(sp_da(sp_str_t) fragments, sp_str_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(scratch.mem, &buf);
  sp_io_writer_t* io = &buf.base;

  spn_err_t err = SPN_OK;
  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_da_for(fragments, it) {
    sp_str_t content = sp_zero;
    if (sp_io_read_file(scratch.mem, fragments[it], &content)) {
      err = SPN_ERROR;
      break;
    }
    sp_assert(sp_str_starts_with(content, sp_str_lit("[")) && sp_str_ends_with(content, sp_str_lit("\n]\n")));
    if (it) {
      sp_io_write_c8(io, ',');
    }
    sp_io_write_str(io, sp_str_sub(content, 1, (s32)content.len - 4), SP_NULLPTR);
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);

  if (!err && sp_fs_create_file_str(path, sp_io_dyn_mem_writer_as_str(&buf))) {
    err = SPN_ERROR;
  }
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

sp_str_t spn_invocation_to_str(sp_mem_t mem, const spn_invocation_t* invocation) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_da(sp_str_t) parts = sp_da_new(scratch.mem, sp_str_t);
  sp_da_for(invocation->env, it) {
    sp_env_var_t var = spn_invocation_env_var(&spn.roots, scratch.mem, invocation->env[it]);
    sp_da_push(parts, sp_fmt(scratch.mem, "{}={}", sp_fmt_str(var.key), sp_fmt_str(var.value)).value);
  }
  sp_da_push(parts, spn_arg_str(&spn.roots, scratch.mem, invocation->program));
  sp_da_for(invocation->args, it) {
    sp_da_push(parts, spn_arg_str(&spn.roots, scratch.mem, invocation->args[it]));
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

sp_ps_config_t spn_invocation_ps(const spn_invocation_t* invocation, sp_mem_t mem) {
  const spn_path_roots_t* roots = &spn.roots;
  sp_str_t cwd = spn_path_str(roots, mem, invocation->cwd);
  sp_fs_create_dir(cwd);

  sp_ps_config_t ps = {
    .command = spn_arg_str(roots, mem, invocation->program),
    .dyn_args = spn_invocation_args(roots, mem, invocation),
    .cwd = cwd,
    .io = {
      .in.mode = SP_PS_IO_MODE_NULL,
      .err.mode = SP_PS_IO_MODE_REDIRECT,
    }
  };
  sp_assert(sp_da_size(invocation->env) < SP_PS_MAX_ENV);
  sp_da_for(invocation->env, it) {
    ps.env.extra[it] = spn_invocation_env_var(roots, mem, invocation->env[it]);
  }
  ps.env.extra[sp_da_size(invocation->env)] = path_var(mem, ps.command);
  return ps;
}

spn_invocation_result_t spn_invocation_run(spn_invocation_t* invocation) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_ps_config_t ps = spn_invocation_ps(invocation, scratch.mem);

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_ps_output_t result = sp_ps_run(spn.mem, ps);
  u64 elapsed = sp_tm_read_timer(&timer);
  sp_mem_end_scratch(scratch);

  return (spn_invocation_result_t) {
    .result = result,
    .elapsed = elapsed,
  };
}
