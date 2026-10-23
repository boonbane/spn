#include "spn_test.h"

#include "ctx/types.h"
#include "intern/intern.h"
#include "pkg/patch.h"
#include "profile/types.h"
#include "when/when.h"

sp_test_suite(pkg, .serial = true);

typedef struct {
  spn_pkg_patch_stamp_result_t result;
  sp_hash_t hash;
} stamp_expect_t;

typedef struct {
  const c8* name;
  const c8* patches [2];
  const c8* qualified;
  spn_pkg_root_kind_t tree;
  stamp_expect_t expect;
} stamp_test_t;

static const stamp_test_t stamp_tests [] = {
  { .name = "stamp_matching_patch",  .patches = { "core/a" }, .qualified = "core/a", .tree = SPN_PKG_ROOT_GIT,   .expect = { SPN_PKG_PATCH_STAMP_APPLIED, 1 } },
  { .name = "skip_unmatched_patch",  .patches = { "core/a" }, .qualified = "core/b", .tree = SPN_PKG_ROOT_GIT,   .expect = { SPN_PKG_PATCH_STAMP_NONE } },
  { .name = "stamp_second_patch",    .patches = { "core/a", "core/b" }, .qualified = "core/b", .tree = SPN_PKG_ROOT_GIT, .expect = { SPN_PKG_PATCH_STAMP_APPLIED, 2 } },
  { .name = "reject_tree_none",      .patches = { "core/a" }, .qualified = "core/a", .tree = SPN_PKG_ROOT_NONE,  .expect = { SPN_PKG_PATCH_STAMP_NOT_GIT } },
  { .name = "reject_tree_local",     .patches = { "core/a" }, .qualified = "core/a", .tree = SPN_PKG_ROOT_LOCAL, .expect = { SPN_PKG_PATCH_STAMP_NOT_GIT } },
  { .name = "skip_empty_patches",    .qualified = "core/a",   .tree = SPN_PKG_ROOT_NONE, .expect = { SPN_PKG_PATCH_STAMP_NONE } },
};

sp_test_each(pkg, patch_stamp, stamp_test_t, stamp_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  si_da(spn_pkg_patch_t) patches = SP_NULLPTR;
  sp_carr_for(it->patches, p) {
    if (!it->patches[p]) break;
    si_da_push(mem, patches, ((spn_pkg_patch_t) {
      .qualified = spn_intern(sp_cstr_as_str(it->patches[p])),
      .set.hash = (sp_hash_t)(p + 1),
    }));
  }

  spn_pkg_root_t source = { .kind = it->tree };
  spn_pkg_patch_stamp_result_t result = spn_pkg_patch_stamp(patches, spn_intern(sp_cstr_as_str(it->qualified)).id, &source);

  sp_expect_eq(t, (u32)it->expect.result, (u32)result);
  if (it->tree == SPN_PKG_ROOT_GIT) {
    sp_expect_eq(t, it->expect.hash, source.git.patches.hash);
  }

  return SP_OK;
}
