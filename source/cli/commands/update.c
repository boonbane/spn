#include "host/host.h"

static sp_cli_result_t update(sp_cli_t* cli) {
  try(spn_cli_open(false));
  try(spn_cli_session(sp_zero_s(spn_session_config_t)));
  return spn_cli_op(spn_update(host.session, (spn_update_request_t) { .names = spn_cli_rest_names(cli) }));
}

sp_cli_cmd_t spn_cmd_update = {
  .name = "update",
  .summary = "Move spn.lock to the newest versions the manifest allows",
  .opts = {
    {
      .brief = 'p',
      .name = "profile",
      .kind = SP_CLI_OPT_STR,
      .summary = "Profile to resolve with",
      .placeholder = "PROFILE",
      .ptr = &host.args.profile.name,
    },
  },
  .args = {
    {
      .name = "name",
      .arity = SP_CLI_ARG_REST,
      .summary = "Packages to update (default: all)",
    },
  },
  .handler = update,
};
