#ifndef SPN_OPTIONS_OPTIONS_H
#define SPN_OPTIONS_OPTIONS_H

#include "sp.h"
#include "spn/core.h"

#include "options/types.h"
#include "pkg/types.h"
#include "profile/types.h"
#include "when/types.h"

void spn_options_merge(
  sp_mem_t mem,
  spn_option_subject_t subject,
  const spn_profile_info_t* profile,
  si_da(spn_pkg_config_entry_t) config,
  spn_option_requests_t requests,
  spn_merged_options_t* merged);

void spn_options_env_set(spn_when_env_t* env, const spn_resolved_options_t* options);

#endif
