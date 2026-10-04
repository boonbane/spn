#include "paths/paths_test.h"

typedef struct {
  spn_path_root_t root;
  const c8* label;
} label_test_t;

static const label_test_t label_tests [] = {
  { SPN_PATH_ROOT_NONE,      "absolute" },
  { SPN_PATH_ROOT_PROJECT,   "project" },
  { SPN_PATH_ROOT_STORAGE,   "storage" },
  { SPN_PATH_ROOT_TOOLCHAIN, "toolchain" },
};

sp_test(paths_root_label, is_injective) {
  sp_must_eq(t, (u32)SP_CARR_LEN(label_tests), (u32)SPN_PATH_ROOT_COUNT);
  sp_carr_for(label_tests, it) {
    sp_test_kv_c(t, "label", label_tests[it].label);
    sp_expect_str_eq_c(t, spn_path_root_label(label_tests[it].root), label_tests[it].label);
    sp_carr_for(label_tests, jt) {
      if (it == jt) {
        continue;
      }
      sp_expect(t, !sp_str_equal(spn_path_root_label(label_tests[it].root), spn_path_root_label(label_tests[jt].root)));
    }
  }
  return SP_OK;
}
