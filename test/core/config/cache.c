#include "spn_test.h"

#include "codegen/lower.h"
#include "config.gen.h"
#include "intern/intern.h"
#include "toml/loader.h"

typedef struct {
  u64 max_bytes;
  u64 min_age_s;
  u64 sweep_interval_s;
} expect_t;

typedef struct {
  const c8* name;
  const c8* config;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unset_fields_are_the_defaults",
    .config = "cache_empty",
    .expect = { .max_bytes = 10ull << 30, .min_age_s = 3600, .sweep_interval_s = 600 },
  },
  {
    .name = "max_size_is_gigabytes",
    .config = "cache_max_size",
    .expect = { .max_bytes = 1ull << 30, .min_age_s = 3600, .sweep_interval_s = 600 },
  },
  {
    .name = "zero_max_size_is_unlimited",
    .config = "cache_max_size_zero",
    .expect = { .min_age_s = 3600, .sweep_interval_s = 600 },
  },
  {
    .name = "max_size_above_max_saturates",
    .config = "cache_max_size_over_max",
    .expect = { .max_bytes = 1ull << 63, .min_age_s = 3600, .sweep_interval_s = 600 },
  },
  {
    .name = "min_age_is_read",
    .config = "cache_min_age",
    .expect = { .max_bytes = 10ull << 30, .min_age_s = 7200, .sweep_interval_s = 600 },
  },
  {
    .name = "min_age_below_least_saturates",
    .config = "cache_min_age_under_least",
    .expect = { .max_bytes = 10ull << 30, .min_age_s = 600, .sweep_interval_s = 600 },
  },
  {
    .name = "sweep_interval_is_read",
    .config = "cache_sweep_interval",
    .expect = { .max_bytes = 10ull << 30, .min_age_s = 3600, .sweep_interval_s = 60 },
  },
};

sp_test_each(config, cache, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_toml_loader_t loader = spn_toml_loader(mem, sp_intern_new(mem), &roots);

  sp_str_t file = sp_fmt(mem, "{}.toml", sp_fmt_cstr(it->config)).value;
  sp_str_t path = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(CONFIG_DIR)), file);
  spn_cg_config_t cg = sp_zero;
  spn_toml_load_config(&loader, (spn_path_t) { .sub = path }, &cg);
  sp_must(t, sp_da_empty(loader.issues));

  spn_cache_config_t cache = spn_cache_lower(&cg.cache);
  sp_expect_eq(t, cache.max_bytes, it->expect.max_bytes);
  sp_expect_eq(t, cache.min_age_s, it->expect.min_age_s);
  sp_expect_eq(t, cache.sweep_interval_s, it->expect.sweep_interval_s);
  return SP_OK;
}
