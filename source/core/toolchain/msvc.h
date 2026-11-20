#ifndef SPN_TOOLCHAIN_MSVC_H
#define SPN_TOOLCHAIN_MSVC_H

#include "sp.h"
#include "sp/sp_msvc.h"
#include "spn/errors.h"
#include "toolchain/types.h"

typedef enum {
  SPN_MSVC_MATCH_OK,
  SPN_MSVC_MATCH_ABSENT,
  SPN_MSVC_MATCH_TOOLSET,
  SPN_MSVC_MATCH_SDK,
} spn_msvc_match_t;

typedef struct {
  const sp_msvc_vs_t* toolset;
  const sp_msvc_sdk_t* kit;
} spn_msvc_pick_t;

sp_msvc_arch_t                si_msvc_arch(spn_arch_t arch);
bool                          si_msvc_toolset_valid(sp_str_t toolset);
bool                          si_msvc_sdk_valid(sp_str_t sdk);
spn_msvc_match_t              si_msvc_match(const spn_msvc_installs_t* installs, spn_arch_t arch, spn_msvc_request_t request, bool compiler, spn_msvc_pick_t* pick);
sp_da(spn_err_msvc_install_t) si_msvc_installed_toolsets(sp_mem_t mem, const spn_msvc_installs_t* installs, spn_arch_t arch, bool compiler);
sp_da(spn_err_msvc_install_t) si_msvc_installed_sdks(sp_mem_t mem, const spn_msvc_installs_t* installs, spn_arch_t arch);

#endif
