#ifndef SPN_TOOLCHAIN_PROVISION_H
#define SPN_TOOLCHAIN_PROVISION_H

#include "sp.h"
#include "spn/core.h"
#include "toolchain/types.h"

sp_str_t spn_artifact_resolve_url(sp_mem_t mem, spn_artifact_t artifact, sp_str_t mirror);
sp_da(sp_str_t) spn_artifact_mirror_urls(sp_mem_t mem, spn_artifact_t artifact, sp_str_t listing, u64 seed);
spn_toolchain_claim_t spn_toolchain_claim(spn_toolchain_store_t* store, sp_mem_t mem, sp_str_t name, spn_artifact_t artifact);
void spn_toolchain_release(spn_toolchain_store_t* store, spn_toolchain_claim_t* claim);
spn_err_t spn_toolchain_verify(spn_toolchain_store_t* store, const spn_toolchain_claim_t* claim, sp_mem_t mem, sp_str_t* actual);
spn_err_t spn_toolchain_install(spn_toolchain_store_t* store, spn_toolchain_claim_t* claim, sp_str_t name, sp_str_t url);

#endif
