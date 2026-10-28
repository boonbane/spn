#include "spn_test.h"

#include "ctx/types.h"
#include "dag/dag.h"
#include "external/wasm/wasm.h"
#include "paths/paths.h"
#include "manifest.h"
#include "session/types.h"
#include "unit/types.h"
#include "wasm_emit.h"

#define WASM_TEST_MAX_OPS 2

typedef enum {
  MODULE_EMITTED,
  MODULE_MISSING,
  MODULE_INVALID,
} module_t;

typedef struct {
  spn_err_t open;
  bool exports;
  spn_err_t call;
  const c8* obs;
} expect_t;

typedef struct {
  const c8* name;
  module_t module;
  const c8* file;
  wasm_emit_op_t ops [WASM_TEST_MAX_OPS];
  const c8* fn;
  expect_t expect;
} test_t;

sp_test_suite(wasm_script, .serial = true);

static const test_t tests [] = {
  { .name = "missing_module", .module = MODULE_MISSING, .expect.open = SPN_ERR_WASM_READ_FAILED },
  { .name = "invalid_module", .module = MODULE_INVALID, .expect.open = SPN_ERR_WASM_MODULE_LOAD_FAILED },
  { .name = "export_called", .ops = { { WASM_EMIT_OPEN_WRITE, "O" } }, .fn = "run", .expect.exports = true },
  { .name = "export_missing", .ops = { { WASM_EMIT_OPEN_WRITE, "O" } }, .fn = "nope", .expect.call = SPN_ERR_WASM_EXPORT_NOT_FOUND },
  { .name = "nonzero_return", .ops = { { WASM_EMIT_OPEN_READ, "H" } }, .fn = "run", .expect = { .exports = true, .call = SPN_ERR_WASM_SCRIPT_ERROR, .obs = "work/H" } },
  { .name = "observes_reads", .file = "H", .ops = { { WASM_EMIT_OPEN_READ, "H" } }, .fn = "run", .expect = { .exports = true, .obs = "work/H" } },
};

static sp_test_once_t runtime_once;

static sp_err_t bring_up_runtime(void* user) {
  return spn_wasm_init() ? SP_ERR : SP_OK;
}

sp_test_each(wasm_script, lifecycle, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_must_ok(t, sp_test_once(&runtime_once, bring_up_runtime, SP_NULLPTR));

  sp_mem_t mem = sp_test_arena(t);
  spn_path_t sandbox = spn_path_make(&spn.roots, test_dir_str(t));
  spn_path_t source = spn_path_join(mem, sandbox, sp_str_lit("source"));
  spn_path_t store = spn_path_join(mem, sandbox, sp_str_lit("store"));
  si_pkg_info_t manifest = test_manifest_make(sp_str_lit("A"));
  spn_session_t session = { .ctx = &spn };
  spn_pkg_unit_t unit = {
    .session = &session,
    .pkg = { .manifest = &manifest },
    .paths = {
      .roots = { .recipe = source, .source = source },
      .work = spn_path_join(mem, sandbox, sp_str_lit("work")),
      .store = store,
      .lib = spn_path_join(mem, store, sp_str_lit("lib")),
      .bin = spn_path_join(mem, store, sp_str_lit("bin")),
      .vendor = spn_path_join(mem, store, sp_str_lit("vendor")),
    },
  };
  sp_fs_create_dir_at(spn_path_at(&spn.roots, source));
  sp_fs_create_dir_at(spn_path_at(&spn.roots, store));
  sp_fs_create_dir_at(spn_path_at(&spn.roots, unit.paths.work));
  if (it->file) {
    sp_fs_create_file_str_at(spn_path_at(&spn.roots, spn_path_join(mem, unit.paths.work, sp_cstr_as_str(it->file))), sp_str_lit("A"));
  }

  spn_path_t module = spn_path_join(mem, sandbox, sp_str_lit("M.wasm"));
  switch (it->module) {
    case MODULE_EMITTED: {
      u32 num_ops = 0;
      sp_carr_detect_len(it->ops, num_ops, it->ops[num_ops].kind);
      wasm_emit_fn_t run = { .name = "run", .ops = it->ops, .count = num_ops };
      sp_fs_create_file_str_at(spn_path_at(&spn.roots, module), wasm_emit_module(mem, &run, 1));
      break;
    }
    case MODULE_INVALID: {
      sp_fs_create_file_str_at(spn_path_at(&spn.roots, module), sp_str_lit("A"));
      break;
    }
    case MODULE_MISSING: {
      break;
    }
  }

  spn_dag_obs_table_t table = sp_zero;
  spn_dag_obs_table_init(&table, mem, &spn.roots, sp_zero_struct(spn_path_t));
  spn_dag_obs_set_t obs = { .table = &table };

  spn_wasm_script_t script = sp_zero;
  spn_err_t opened = spn_wasm_script_open(&unit, module, &obs, &script);
  sp_expect_eq(t, it->expect.open, opened);
  if (opened) {
    return SP_OK;
  }

  sp_str_t fn = sp_cstr_as_str(it->fn);
  sp_expect_eq(t, it->expect.exports, spn_wasm_script_exports(&script, fn));
  sp_expect_eq(t, it->expect.call, spn_wasm_script_call(&script, fn, SP_NULLPTR, 0));
  spn_wasm_script_close(&script);

  sp_expect_eq(t, it->expect.obs ? 1u : 0u, (u32)sp_da_size(obs.rows));
  if (it->expect.obs) {
    spn_path_t expected = spn_path_join(mem, sandbox, sp_cstr_as_str(it->expect.obs));
    sp_expect(t, spn_path_equal(obs.rows[0].path, expected));
  }
  return SP_OK;
}
