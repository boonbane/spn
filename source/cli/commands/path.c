#include "host/host.h"

#include "tui/tui.h"

static struct {
  sp_str_t name;
  bool tool;
  bool tool_dep;
} args;

static sp_cli_result_t path(sp_cli_t* cli) {
  try(spn_cli_open(false));

  if (args.tool && args.tool_dep) {
    return spn_cli_usage("pass at most one of {.cyan} and {.cyan}", sp_fmt_cstr("--tool"), sp_fmt_cstr("--tool-dep"));
  }

  spn_path_request_t request = {
    .name = args.name,
    .kind = args.tool_dep ? SPN_DEP_KIND_TOOL : SPN_DEP_KIND_PACKAGE,
  };
  spn_op_t* op = SP_NULLPTR;
  if (args.tool) {
    try(spn_cli_session((spn_session_config_t) {
      .selection = {
        .kinds = spn_target_kind_bit(SPN_TARGET_KIND_TOOL),
        .names = { .items = &args.name, .count = 1 },
      },
    }));
    op = spn_tool_path(host.session, request);
  }
  else {
    try(spn_cli_session(sp_zero_s(spn_session_config_t)));
    op = spn_source_path(host.session, request);
  }

  spn_err_t err = spn_cli_wait(op);
  sp_str_t located = spn_op_result(op).path.path;
  spn_op_free(op);
  if (err) {
    return SP_CLI_ERR;
  }

  spn_tui_handoff(&tui);
  spn_print(&tui, "{}", sp_fmt_str(located));
  return SP_CLI_OK;
}

sp_cli_cmd_t spn_cmd_path = {
  .name = "path",
  .summary = "Print where a package's source tree or a built tool is",
  .opts = {
    {
      .name = "tool",
      .summary = "Build the tool with this name and print its executable",
      .kind = SP_CLI_OPT_BOOLEAN,
      .ptr = &args.tool,
    },
    {
      .name = "tool-dep",
      .summary = "Print the source tree of the tool dependency with this name",
      .kind = SP_CLI_OPT_BOOLEAN,
      .ptr = &args.tool_dep,
    },
    {
      .brief = 'p',
      .name = "profile",
      .kind = SP_CLI_OPT_STR,
      .summary = "Profile to resolve and build with",
      .placeholder = "PROFILE",
      .ptr = &host.args.profile.name,
    },
    {
      .name = "toolchain",
      .kind = SP_CLI_OPT_STR,
      .summary = "Override toolchain",
      .placeholder = "NAME",
      .ptr = &host.args.profile.toolchain,
    },
    spn_cli_locked_opt,
  },
  .args = {
    {
      .name = "name",
      .kind = SP_CLI_OPT_STR,
      .summary = "This package or one of its package dependencies; with a flag, a tool or tool dependency",
      .ptr = &args.name,
    },
  },
  .handler = path,
};
