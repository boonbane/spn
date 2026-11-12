#include "toolchain.h"

#define SHELL_MAX_VARS 8

typedef struct {
  const c8* name;
  const c8* value;
} var_t;

typedef struct {
  const c8* name;
  var_t env [SHELL_MAX_VARS];
  struct {
    bool left;
    var_t env [SHELL_MAX_VARS];
    const c8* gone [SHELL_MAX_VARS];
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "outside_a_shell_nothing_changes",
    .env = { { "PATH", "C:/bin" }, { "INCLUDE", "C:/mine" }, { "VCPKG_ROOT", "C:/vcpkg" } },
    .expect = {
      .env = { { "PATH", "C:/bin" }, { "INCLUDE", "C:/mine" }, { "VCPKG_ROOT", "C:/vcpkg" } },
    },
  },
  {
    .name = "shell_variables_go_and_path_is_restored",
    .env = {
      { "VSCMD_VER", "17.12.3" },
      { "__VSCMD_PREINIT_PATH", "C:/bin" },
      { "PATH", "C:/VS/bin;C:/bin" },
      { "INCLUDE", "C:/VS/include" },
      { "EXTERNAL_INCLUDE", "C:/VS/include" },
      { "LIBPATH", "C:/VS/lib" },
      { "VS170COMNTOOLS", "C:/VS/Common7/Tools/" },
      { "USERPROFILE", "C:/Users/me" },
    },
    .expect = {
      .left = true,
      .env = { { "PATH", "C:/bin" }, { "USERPROFILE", "C:/Users/me" } },
      .gone = { "VSCMD_VER", "__VSCMD_PREINIT_PATH", "INCLUDE", "EXTERNAL_INCLUDE", "LIBPATH", "VS170COMNTOOLS" },
    },
  },
  {
    .name = "shell_without_a_saved_path_keeps_path",
    .env = { { "VSCMD_VER", "17.12.3" }, { "PATH", "C:/VS/bin;C:/bin" }, { "LIB", "C:/VS/lib" } },
    .expect = {
      .left = true,
      .env = { { "PATH", "C:/VS/bin;C:/bin" } },
      .gone = { "VSCMD_VER", "LIB" },
    },
  },
};

sp_test_each(sdk_shell, leave, test_t, tests) {
  sp_env_t env = sp_zero;
  sp_env_init(sp_test_arena(t), &env);
  sp_carr_for_until(it->env, at, it->env[at].name) {
    sp_env_insert_c(&env, it->env[at].name, it->env[at].value);
  }

  sp_expect_eq(t, it->expect.left, spn_msvc_leave_shell(&env));
  sp_carr_for_until(it->expect.env, at, it->expect.env[at].name) {
    sp_test_kv(t, "name", sp_cstr_as_str(it->expect.env[at].name));
    sp_expect_str_eq_c(t, sp_env_get_c(&env, it->expect.env[at].name), it->expect.env[at].value);
  }
  sp_carr_for_until(it->expect.gone, at, it->expect.gone[at]) {
    sp_test_kv(t, "name", sp_cstr_as_str(it->expect.gone[at]));
    sp_expect(t, !sp_env_contains_c(&env, it->expect.gone[at]));
  }
  return SP_OK;
}
