#include "spn_test.h"

#include "core.embed.h"
#include "jtd.h"
#include "enum/enum.h"
#include "triple/triple.h"

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
