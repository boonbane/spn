#ifndef SPN_PROFILE_TYPES_H
#define SPN_PROFILE_TYPES_H

#include "core/types.h"
#include "intern/types.h"
#include "ordered_map.h"
#include "toolchain/types.h"
#include "when/types.h"

typedef sp_intern_id_t spn_profile_id_t;

struct spn_profile_info {
  spn_profile_id_t id;
  sp_intern_str_t name;
  spn_toolchain_ref_t toolchain;
  spn_cc_driver_t driver;
  spn_ld_family_t linker;
  spn_triple_t target;
  spn_linking_t request;
  spn_linkage_t demand;
  spn_linking_t linking;
  spn_c_standard_t standard;
  spn_mode_t mode;
  spn_opt_level_t opt;
  spn_sanitizer_set_t sanitizers;
  bool sanitizers_set;
  spn_when_t options;
  bool targeted;
  spn_sdk_t sdk;
  sp_opt(spn_libc_t) libc;
};

typedef struct {
  spn_profile_id_t id;
  sp_intern_str_t name;
  spn_os_t os;
  spn_arch_t arch;
  spn_gated_list_t toolchain;
  spn_gated_list_t abi;
  spn_gated_list_t linkage;
  spn_gated_list_t runtime;
  spn_gated_list_t libc;
  spn_gated_list_t standard;
  spn_gated_list_t mode;
  spn_gated_list_t opt;
  spn_sanitizer_set_t sanitizers;
  bool sanitizers_set;
  spn_when_t options;
} spn_profile_decl_t;

typedef si_om(spn_profile_id_t, spn_profile_decl_t) spn_profile_map_t;

#endif
