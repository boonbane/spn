#include "toolchain.h"

static spn_path_t generation_file(sp_mem_t mem, spn_path_t cache) {
  return spn_path_join(mem, cache, sp_str_lit("spn/generation"));
}

sp_test(toolchain_zig, generation_created_once, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_path_t cache = { .sub = sp_fs_join_path(mem, sp_test_dir(t), sp_str_lit("cache")) };

  sp_hash_t first = 0;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_toolchain_generation(mem, &roots, cache, &first));
  sp_expect(t, first != 0);
  sp_expect(t, sp_fs_exists(spn_path_str(&roots, mem, generation_file(mem, cache))));

  sp_hash_t second = 0;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_toolchain_generation(mem, &roots, cache, &second));
  sp_expect_eq(t, first, second);
  return SP_OK;
}

sp_test(toolchain_zig, generation_follows_the_dir, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_path_t cache = { .sub = sp_fs_join_path(mem, sp_test_dir(t), sp_str_lit("cache")) };

  sp_hash_t first = 0;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_toolchain_generation(mem, &roots, cache, &first));
  sp_must_ok(t, sp_fs_remove_dir(spn_path_str(&roots, mem, cache)));

  sp_hash_t second = 0;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_toolchain_generation(mem, &roots, cache, &second));
  sp_expect(t, first != second);
  return SP_OK;
}

sp_test(toolchain_zig, generation_reads_the_writer, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_path_t cache = { .sub = sp_fs_join_path(mem, sp_test_dir(t), sp_str_lit("cache")) };
  sp_str_t file = spn_path_str(&roots, mem, generation_file(mem, cache));
  sp_fs_create_dir(sp_fs_parent_path(file));
  sp_must_ok(t, sp_fs_create_file_str(file, sp_str_lit("42\n")));

  sp_hash_t generation = 0;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_toolchain_generation(mem, &roots, cache, &generation));
  sp_expect_eq(t, generation, 42);
  return SP_OK;
}

sp_test(toolchain_zig, generation_unreadable_is_an_error, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_path_t cache = { .sub = sp_fs_join_path(mem, sp_test_dir(t), sp_str_lit("cache")) };
  sp_str_t file = spn_path_str(&roots, mem, generation_file(mem, cache));
  sp_fs_create_dir(sp_fs_parent_path(file));
  sp_must_ok(t, sp_fs_create_file_str(file, sp_str_lit("not a number\n")));

  sp_hash_t generation = 0;
  sp_expect(t, spn_toolchain_generation(mem, &roots, cache, &generation) != SPN_OK);
  sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
  sp_expect_eq(t, sp_da_size(errs), 1);
  return SP_OK;
}
