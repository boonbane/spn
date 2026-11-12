#ifndef SPN_TOOLCHAIN_SDK_H
#define SPN_TOOLCHAIN_SDK_H

#include "sp.h"
#include "sp/sp_msvc.h"
#include "paths/types.h"
#include "toolchain/types.h"

spn_sdk_kind_t spn_sdk_kind(spn_triple_t target);
sp_str_t       spn_msvc_arch_name(spn_arch_t arch);
spn_sdk_msvc_t spn_sdk_from_msvc(sp_mem_t mem, const sp_msvc_sdk_t* kits, const sp_msvc_vs_t* vs, spn_arch_t arch);
spn_sdk_host_t spn_sdk_detect(sp_mem_t mem, const spn_path_roots_t* roots, sp_env_t* env, spn_triple_t host);
spn_sdk_t      spn_sdk_at(sp_mem_t mem, spn_triple_t target, spn_path_t root);
bool           spn_sdk_served(const spn_sdk_host_t* sdks, spn_triple_t host, spn_triple_t target, spn_sdk_t* sdk);
bool           spn_sdk_default(const spn_sdk_host_t* sdks, spn_triple_t target, spn_sdk_t* sdk);
sp_hash_t      spn_sdk_hash(const spn_sdk_t* sdk);
bool           spn_sdk_libc(spn_cc_cap_set_t caps, const spn_sdk_t* sdk, spn_libc_t* libc);
spn_dir_list_t spn_sdk_tool_dirs(const spn_sdk_t* sdk);
spn_dir_list_t spn_msvc_include_dirs(const spn_sdk_msvc_t* msvc);
spn_dir_list_t spn_msvc_lib_dirs(const spn_sdk_msvc_t* msvc);
u32            spn_msvc_shell_vars(const c8* const** vars);
bool           spn_msvc_leave_shell(sp_env_t* env);
spn_wasi_spelling_t spn_sdk_wasi_spelling(const spn_path_roots_t* roots, sp_mem_t mem, const spn_sdk_t* sdk);

#endif
