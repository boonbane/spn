#include "host/host.h"

static struct {
  bool cache;
  u64 min_age;
} args;

static sp_cli_result_t clean(sp_cli_t* cli) {
  if (args.min_age && !args.cache) {
    return spn_cli_usage("{.cyan} only applies with {.cyan}", sp_fmt_cstr("--min-age"), sp_fmt_cstr("--cache"));
  }

  if (args.cache) {
    try(spn_cli_open(true));
    return spn_cli_op(spn_clean_cache(host.ctx, (spn_clean_request_t) { .min_age_seconds = args.min_age }));
  }

  try(spn_cli_open(false));

  if (sp_str_empty(host.args.profile.name)) {
    return spn_cli_op(spn_clean(host.ctx));
  }

  try(spn_cli_session(sp_zero_s(spn_session_config_t)));
  return spn_cli_op(spn_clean_profile(host.session));
}

sp_cli_cmd_t spn_cmd_clean = {
  .name = "clean",
  .summary = "Remove the project build directory",
  .opts = {
    {
      .brief = 'p',
      .name = "profile",
      .kind = SP_CLI_OPT_STR,
      .summary = "Only remove this profile's outputs",
      .placeholder = "PROFILE",
      .ptr = &host.args.profile.name,
    },
    {
      .name = "cache",
      .kind = SP_CLI_OPT_BOOLEAN,
      .summary = "Evict everything unused from the shared build cache instead",
      .ptr = &args.cache,
    },
    {
      .name = "min-age",
      .kind = SP_CLI_OPT_U64,
      .summary = "Seconds an entry may go unused before --cache evicts it",
      .placeholder = "SECONDS",
      .ptr = &args.min_age,
    },
  },
  .handler = clean,
};
