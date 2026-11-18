#include "spn_test.h"

#include "intern/intern.h"
#include "model/sync.h"
#include "paths/paths.h"
#include "toolchain/probe.h"
#include "toolchain/toolchain.h"

typedef struct {
  const c8* name;
  const c8* a;
  const c8* b;
  bool equal;
} test_t;

static const test_t tests [] = {
  { .name = "same_version_same_identity", .a = "A", .b = "A", .equal = true },
  { .name = "version_changes_identity",   .a = "A", .b = "B" },
};

static sp_err_t sync(sp_test_t* t, spn_toolchain_store_t* store, sp_str_t program, const c8* version, sp_hash_t* identity) {
  spn_toolchain_info_t info = {
    .name = sp_intern(spn.intern, sp_str_lit("T")),
    .version = sp_cstr_as_str(version),
    .driver = SPN_CC_DRIVER_GCC,
    .compiler = { .program = spn_arg_path((spn_path_t) { .sub = program }) },
    .archiver = { .program = spn_arg_path((spn_path_t) { .sub = program }) },
    .support = { .kind = SPN_TOOLCHAIN_SUPPORT_LOCAL },
  };
  spn_toolchain_unit_t unit = { .info = &info };
  sp_atomic_s32_t cancelled = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_sync_toolchain(store, &unit, &cancelled));
  sp_expect_str_eq_c(t, unit.version, version);
  *identity = unit.identity;
  return SP_OK;
}

sp_test_each(local, identity, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t root = test_dir_str(t);
  sp_str_t program = sp_fs_join_path(mem, root, sp_str_lit("A"));
  sp_must_ok(t, sp_fs_write(program, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));

  spn_toolchain_store_t store = { .mem = mem, .roots = &spn.roots };
  spn_probe_cache_load(&store.probes, &spn.roots, (spn_path_t) { .sub = sp_fs_join_path(mem, root, sp_str_lit("probe.cache")) }, mem);

  sp_hash_t a = sp_zero;
  sp_hash_t b = sp_zero;
  sp_try(sync(t, &store, program, it->a, &a));
  sp_try(sync(t, &store, program, it->b, &b));
  sp_expect_eq(t, it->equal, a == b);
  return SP_OK;
}
