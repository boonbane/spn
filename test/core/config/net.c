#include "spn_test.h"

#include "codegen/lower.h"
#include "config.gen.h"
#include "intern/intern.h"
#include "toml/loader.h"

typedef struct {
  u32 timeout_s;
} expect_t;

typedef struct {
  const c8* name;
  const c8* config;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unset_timeout_is_the_default",
    .config = "net_empty",
    .expect = { .timeout_s = 30 },
  },
  {
    .name = "timeout_is_read",
    .config = "net_timeout",
    .expect = { .timeout_s = 1 },
  },
  {
    .name = "zero_timeout_is_not_the_default",
    .config = "net_timeout_zero",
  },
  {
    .name = "timeout_above_max_saturates",
    .config = "net_timeout_over_max",
    .expect = { .timeout_s = 65535 },
  },
};

sp_test_each(config, net, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_toml_loader_t loader = spn_toml_loader(mem, sp_intern_new(mem), &roots);

  sp_str_t file = sp_fmt(mem, "{}.toml", sp_fmt_cstr(it->config)).value;
  sp_str_t path = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(CONFIG_DIR)), file);
  spn_cg_config_t cg = sp_zero;
  spn_toml_load_config(&loader, (spn_path_t) { .sub = path }, &cg);
  sp_must(t, sp_da_empty(loader.issues));

  sp_expect_eq(t, spn_net_lower(&cg.net).timeout_s, it->expect.timeout_s);
  return SP_OK;
}
