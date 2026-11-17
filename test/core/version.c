#include "spn_test.h"

#include "external/tom.h"
#include "version/version.h"

sp_test(core, version_matches_manifest) {
  sp_mem_t mem = sp_test_arena(t);

  toml_table_t* root = spn_toml_parse(sp_path_from_str(test_repo_path(mem, sp_str_lit("spn.toml"))));
  sp_must(t, root != SP_NULLPTR);

  toml_table_t* package = toml_table_table(root, "package");
  sp_must(t, package != SP_NULLPTR);

  toml_value_t version = toml_table_string(package, "version");
  sp_must(t, version.ok);
  sp_expect_str_eq_c(t, sp_str(version.u.s, (u32)version.u.sl), SPN_VERSION);

  toml_free(root);
  return SP_OK;
}
