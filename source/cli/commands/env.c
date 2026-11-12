#include "host/host.h"

#include "tui/tui.h"

typedef enum {
  FORMAT_SH,
  FORMAT_POWERSHELL,
} format_t;

static struct {
  sp_cli_choice_t format;
} args;

#define selection_opts \
  { \
    .brief = 'p', \
    .name = "profile", \
    .kind = SP_CLI_OPT_STR, \
    .summary = "Profile whose toolchain to use", \
    .placeholder = "PROFILE", \
    .ptr = &host.args.profile.name, \
  }, \
  { \
    .name = "toolchain", \
    .kind = SP_CLI_OPT_STR, \
    .summary = "Override toolchain", \
    .placeholder = "NAME", \
    .ptr = &host.args.profile.toolchain, \
  }, \
  spn_cli_target_opts

static sp_cli_result_t resolve(spn_env_result_t* env) {
  try(spn_cli_open(false));
  try(spn_cli_session(sp_zero_s(spn_session_config_t)));
  spn_op_t* op = spn_env(host.session);
  spn_err_t err = spn_cli_wait(op);
  *env = spn_op_result(op).env;
  spn_op_free(op);
  return err ? SP_CLI_ERR : SP_CLI_OK;
}

static sp_str_t quoted(sp_mem_t mem, sp_str_t value, const c8* escape) {
  sp_da(sp_str_t) parts = sp_str_split_c8(mem, value, '\'');
  return sp_fmt(mem, "'{}'", sp_fmt_str(sp_str_join_n(mem, parts, sp_da_size(parts), sp_cstr_as_str(escape)))).value;
}

static sp_cli_result_t env(sp_cli_t* cli) {
  spn_env_result_t env = sp_zero;
  try(resolve(&env));
  spn_tui_handoff(&tui);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  bool windows = sp_os_get_path_kind() == SP_FS_PATH_WINDOWS;
  switch ((format_t)args.format.value) {
    case FORMAT_SH: {
      sp_str_t dirs [SPN_ENV_MAX_PATH] = sp_zero;
      sp_for(it, env.path.count) {
        sp_str_t dir = env.path.items[it];
        if (windows && dir.len > 1 && dir.data[1] == ':') {
          c8 drive = sp_c8_to_lower(dir.data[0]);
          dir = sp_fmt(s.mem, "/{}{}", sp_fmt_char(drive), sp_fmt_str(sp_str_suffix(dir, dir.len - 2))).value;
        }
        dirs[it] = dir;
      }
      spn_print(&tui, "export PATH={}\":$PATH\"", sp_fmt_str(quoted(s.mem, sp_str_join_n(s.mem, dirs, env.path.count, sp_str_lit(":")), "'\\''")));
      sp_for(it, env.vars.count) {
        spn_print(&tui, "export {}={}", sp_fmt_str(env.vars.items[it].name), sp_fmt_str(quoted(s.mem, env.vars.items[it].value, "'\\''")));
      }
      break;
    }
    case FORMAT_POWERSHELL: {
      sp_str_t sep = windows ? sp_str_lit(";") : sp_str_lit(":");
      sp_str_t dirs = sp_fmt(s.mem, "{}{}", sp_fmt_str(sp_str_join_n(s.mem, env.path.items, env.path.count, sep)), sp_fmt_str(sep)).value;
      spn_print(&tui, "$env:PATH = {} + $env:PATH", sp_fmt_str(quoted(s.mem, dirs, "''")));
      sp_for(it, env.vars.count) {
        spn_print(&tui, "$env:{} = {}", sp_fmt_str(env.vars.items[it].name), sp_fmt_str(quoted(s.mem, env.vars.items[it].value, "''")));
      }
      break;
    }
  }
  sp_mem_end_scratch(s);
  return SP_CLI_OK;
}

static sp_cli_result_t run(sp_cli_t* cli) {
  if (!cli->rest[0]) {
    return spn_cli_usage("missing a command to run");
  }

  spn_env_result_t env = sp_zero;
  try(resolve(&env));
  spn_tui_handoff(&tui);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  bool windows = sp_os_get_path_kind() == SP_FS_PATH_WINDOWS;
  sp_str_t sep = windows ? sp_str_lit(";") : sp_str_lit(":");

  sp_str_t name = sp_cstr_as_str(cli->rest[0]);
  sp_str_t command = name;
  if (sp_fs_get_name(name).len == name.len) {
    const c8* suffixes [] = { "", windows ? ".exe" : "" };
    sp_for(it, env.path.count) {
      sp_carr_for(suffixes, at) {
        sp_str_t candidate = sp_fmt(s.mem, "{}/{}{}", sp_fmt_str(env.path.items[it]), sp_fmt_str(name), sp_fmt_cstr(suffixes[at])).value;
        if (sp_str_equal(command, name) && sp_fs_is_file(candidate)) {
          command = candidate;
        }
      }
    }
  }

  sp_env_t vars = sp_env_capture(s.mem);
  sp_env_insert(&vars, sp_str_lit("PATH"), sp_fmt(s.mem, "{}{}{}",
    sp_fmt_str(sp_str_join_n(s.mem, env.path.items, env.path.count, sep)),
    sp_fmt_str(sep),
    sp_fmt_str(sp_env_get(&vars, sp_str_lit("PATH")))).value);
  sp_for(it, env.vars.count) {
    sp_env_insert(&vars, env.vars.items[it].name, env.vars.items[it].value);
  }

  sp_ps_config_t config = {
    .command = command,
    .dyn_args = sp_da_new(s.mem, sp_str_t),
    .env = { .env = vars, .mode = SP_PS_ENV_EXISTING },
    .io = {
      .in = { .mode = SP_PS_IO_MODE_INHERIT },
      .out = { .mode = SP_PS_IO_MODE_INHERIT },
      .err = { .mode = SP_PS_IO_MODE_INHERIT },
    },
  };
  for (const c8** arg = cli->rest + 1; *arg; arg++) {
    sp_da_push(config.dyn_args, sp_cstr_as_str(*arg));
  }

  sp_ps_t ps = sp_ps_create(s.mem, config);
  if (!ps.os) {
    sp_mem_end_scratch(s);
    host.code = 127;
    return spn_cli_usage("failed to run {.red}", sp_fmt_str(name));
  }
  s32 code = sp_ps_wait(&ps).exit_code;
  sp_mem_end_scratch(s);
  host.code = !windows && code < 0 ? 128 - code : code;
  return SP_CLI_OK;
}

sp_cli_cmd_t spn_cmd_env = {
  .name = "env",
  .summary = "Print the environment that puts a profile's toolchain in a shell",
  .opts = {
    selection_opts,
    {
      .name = "format",
      .kind = SP_CLI_OPT_CHOICE,
      .summary = "Shell syntax to print",
      .placeholder = "SHELL",
      .ptr = &args.format,
      .choices = {
        { .name = "sh", .value = FORMAT_SH },
        { .name = "powershell", .value = FORMAT_POWERSHELL },
      },
    },
  },
  .handler = env,
};

sp_cli_cmd_t spn_cmd_run = {
  .name = "run",
  .summary = "Run a command with a profile's toolchain in its environment",
  .opts = {
    selection_opts,
  },
  .args = {
    {
      .name = "command",
      .arity = SP_CLI_ARG_REST,
      .summary = "Command to run, and its arguments",
    },
  },
  .handler = run,
};
