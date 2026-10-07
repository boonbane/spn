#include "target/target.h"

spn_target_key_t spn_target_key(sp_str_t name, spn_target_kind_t kind) {
  (void)name;
  (void)kind;
  SP_UNIMPLEMENTED();
  return sp_zero_struct(spn_target_key_t);
}

spn_target_info_t spn_target_info_new(sp_mem_t mem, sp_str_t name, spn_target_kind_t kind) {
  (void)mem;
  (void)name;
  (void)kind;
  SP_UNIMPLEMENTED();
  return sp_zero_struct(spn_target_info_t);
}
