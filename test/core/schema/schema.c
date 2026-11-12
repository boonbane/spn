#include "spn_test.h"

#include "core.embed.h"
#include "jtd.h"
#include "enum/enum.h"
#include "triple/triple.h"
#include "yyjson.h"

#define SCHEMA_ENUMS \
  X(arch,           manifest) \
  X(os,             manifest) \
  X(abi,            manifest) \
  X(cc_driver,      config) \
  X(linkage,        manifest) \
  X(runtime,        manifest) \
  X(c_standard,     manifest) \
  X(cxx_standard,   manifest) \
  X(mode,           manifest) \
  X(option_type,    manifest) \
  X(tree,           manifest) \
  X(index_protocol, manifest) \
  X(index_dep_kind, release)

#define X(ID, SCHEMA) \
  static u32 schema_##ID##_from(sp_str_t str) { return spn_##ID##_from_str(str); } \
  static sp_str_t schema_##ID##_to(u32 value) { return spn_##ID##_to_str((spn_##ID##_t)value); }
SCHEMA_ENUMS
#undef X

typedef struct {
  const c8* name;
  sp_str_t schema;
  const c8* def;
  u32 (*from)(sp_str_t);
  sp_str_t (*to)(u32);
} test_t;

static const test_t tests [] = {
  #define X(ID, SCHEMA) { \
    .name = #ID, \
    .schema = { .data = (const c8*)schema_##SCHEMA##_jtd_json, .len = sizeof(schema_##SCHEMA##_jtd_json) }, \
    .def = #ID, \
    .from = schema_##ID##_from, \
    .to = schema_##ID##_to, \
  },
  SCHEMA_ENUMS
  #undef X
};

sp_test_each(schema, roundtrip, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  jtd_result_t jtd = jtd_parse(mem, it->schema);
  sp_must(t, jtd.ok);

  jtd_schema_t* def = jtd_definition(&jtd, sp_cstr_as_str(it->def));
  sp_must(t, def != SP_NULLPTR);
  sp_must_eq(t, JTD_FORM_ENUM, def->form);

  sp_expect(t, !sp_da_empty(def->as.enumeration.values));
  sp_da_for(def->as.enumeration.values, v) {
    sp_str_t value = def->as.enumeration.values[v];
    sp_expect_str_eq(t, it->to(it->from(value)), value);
  }

  return SP_OK;
}

sp_test(schema, abi_handles) {
  const c8* kinds [] = { "ctx", "config", "node", "profile" };

  sp_mem_t mem = sp_test_arena(t);
  sp_str_t path = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(SCHEMA_DIR)), sp_str_lit("abi.json"));
  sp_str_t json = sp_zero;
  sp_must_eq(t, SP_OK, sp_io_read_file(mem, path, &json));

  yyjson_doc* doc = yyjson_read(json.data, json.len, 0);
  sp_must(t, doc != SP_NULLPTR);

  yyjson_val* handles = yyjson_obj_get(yyjson_doc_get_root(doc), "handles");
  sp_must_eq(t, (u32)sp_carr_len(kinds), (u32)yyjson_arr_size(handles));
  sp_carr_for(kinds, k) {
    sp_expect(t, sp_cstr_equal(kinds[k], yyjson_get_str(yyjson_arr_get(handles, k))));
  }
  yyjson_doc_free(doc);

  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_err_t err;
  s32 code;
} code_t;

static const code_t codes [] = {
  { .name = "ok",               .err = SPN_OK },
  { .name = "error",            .err = SPN_ERROR,                .code = 1 },
  { .name = "handle_null",      .err = SPN_ERR_HANDLE_NULL,      .code = 2 },
  { .name = "handle_kind",      .err = SPN_ERR_HANDLE_KIND,      .code = 3 },
  { .name = "node_no_script",   .err = SPN_ERR_NODE_NO_SCRIPT,   .code = 4 },
  { .name = "node_no_outputs",  .err = SPN_ERR_NODE_NO_OUTPUTS,  .code = 5 },
  { .name = "path_not_normal",  .err = SPN_ERR_PATH_NOT_NORMAL,  .code = 6 },
  { .name = "path_not_mounted", .err = SPN_ERR_PATH_NOT_MOUNTED, .code = 7 },
  { .name = "output_outside",   .err = SPN_ERR_OUTPUT_OUTSIDE,   .code = 8 },
  { .name = "output_is_root",   .err = SPN_ERR_OUTPUT_IS_ROOT,   .code = 9 },
  { .name = "io_write",         .err = SPN_ERR_IO_WRITE,         .code = 10 },
  { .name = "str_null",         .err = SPN_ERR_STR_NULL,         .code = 11 },
};

sp_test_each(schema, guest_codes, code_t, codes) {
  sp_expect_eq(t, (s32)it->err, it->code);
  return SP_OK;
}
