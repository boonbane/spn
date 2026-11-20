#ifndef SPN_TOOLCHAIN_MSVC_H
#define SPN_TOOLCHAIN_MSVC_H

#include "sp.h"
#include "sp/sp_msvc.h"
#include "toolchain/types.h"

typedef enum {
  SPN_MSVC_MATCH_OK,
  SPN_MSVC_MATCH_ABSENT,
  SPN_MSVC_MATCH_TOOLSET,
  SPN_MSVC_MATCH_SDK,
} spn_msvc_match_t;

typedef struct {
  const spn_msvc_toolset_t* toolset;
  const sp_msvc_sdk_t* kit;
} spn_msvc_pick_t;

sp_msvc_arch_t   spn_msvc_arch(spn_arch_t arch);
bool             spn_msvc_toolset_valid(sp_str_t toolset);
bool             spn_msvc_sdk_valid(sp_str_t sdk);
spn_msvc_match_t spn_msvc_match(const spn_msvc_installs_t* installs, spn_arch_t arch, spn_msvc_request_t request, bool compiler, spn_msvc_pick_t* pick);

#endif
