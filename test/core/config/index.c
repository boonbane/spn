#include "spn_test.h"

#include "codegen/lower.h"
#include "config.gen.h"
#include "index/types.h"
#include "intern/intern.h"
#include "toml/loader.h"

typedef struct {
  spn_err_t code;
  const c8* path;
} issue_t;

typedef struct {
  const c8* name;
  const c8* config;
  issue_t issue;
} test_t;

static const test_t tests [] = {
  {
    .name = "releases_need_no_rev",
    .config = "index_releases",
  },
  {
    .name = "manifests_at_a_rev_are_read",
    .config = "index_manifests_pinned",
  },
  {
    .name = "manifests_without_a_rev_are_refused",
    .config = "index_manifests_unpinned",
    .issue = { SPN_ERR_CODEGEN_MISSING_KEY, "index[0].rev" },
  },
};

sp_test_each(config, index, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_toml_loader_t loader = spn_toml_loader(mem, sp_intern_new(mem), &roots);

  sp_str_t file = sp_fmt(mem, "{}.toml", sp_fmt_cstr(it->config)).value;
  sp_str_t path = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(CONFIG_DIR)), file);
  spn_cg_config_t cg = sp_zero;
  spn_toml_load_config(&loader, (spn_path_t) { .sub = path }, &cg);
  sp_must(t, sp_da_empty(loader.issues));

  sp_must_eq(t, 1, (u32)sp_da_size(cg.index));
  spn_index_lower(&loader, 0, SPN_INDEX_KIND_USER, &cg.index[0]);
  if (it->issue.code) {
    sp_must_eq(t, 1, (u32)sp_da_size(loader.issues));
    sp_expect_eq(t, (u32)it->issue.code, (u32)loader.issues[0].code);
    sp_expect_str_eq_c(t, spn_toml_path_str(mem, &loader.issues[0].at), it->issue.path);
    return SP_OK;
  }
  sp_must(t, sp_da_empty(loader.issues));
  return SP_OK;
}
