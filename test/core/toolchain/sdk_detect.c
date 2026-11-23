#include "toolchain.h"

sp_test(sdk_detect, macos_env_off_apple) {
  sp_mem_t mem = sp_test_arena(t);
  sp_env_t env = sp_zero;
  sp_env_init(mem, &env);
  sp_env_insert(&env, sp_str_lit("SPN_MACOS_SDK"), sp_str_lit("/S"));

  spn_path_roots_t roots = sp_zero;
  spn_sdk_host_t host = si_sdk_detect(mem, &roots, &env, (spn_triple_t) HOST_X64_LINUX);
  return test_check_path(t, host.macos.root, (test_path_t) { "/S" });
}
