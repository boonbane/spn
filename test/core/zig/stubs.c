#include "spn_test.h"

#include "external/zig.h"

#define STUB_TEST_MAX_LINKS 4
#define STUB_TEST_MAX_LIBS 4

typedef struct {
  spn_cc_output_kind_t kind;
  spn_lang_t lang;
  const c8* libs [STUB_TEST_MAX_LIBS];
} link_t;

typedef struct {
  const c8* name;
  spn_os_t os;
  link_t links [STUB_TEST_MAX_LINKS];
  link_t expect [STUB_TEST_MAX_LINKS];
} stub_test_t;

static const stub_test_t tests [] = {
  {
    .name = "dedupe",
    .os = SPN_OS_LINUX,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    },
  },
  {
    .name = "kinds",
    .os = SPN_OS_LINUX,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
      { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_C },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
      { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_C },
    },
  },
  {
    .name = "langs",
    .os = SPN_OS_LINUX,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_CXX },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_CXX },
    },
  },
  {
    .name = "windows_libs_canonical",
    .os = SPN_OS_WINDOWS,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "B", "A" } },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A", "B" } },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A", "B" } },
    },
  },
  {
    .name = "windows_libs_distinct",
    .os = SPN_OS_WINDOWS,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A" } },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "B" } },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A" } },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "B" } },
    },
  },
  {
    .name = "windows_libs_dedupe",
    .os = SPN_OS_WINDOWS,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A", "A", "B" } },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A", "B" } },
    },
  },
  {
    .name = "libs_dropped_elsewhere",
    .os = SPN_OS_LINUX,
    .links = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "A" } },
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C, { "B" } },
    },
    .expect = {
      { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    },
  },
};

sp_test_each(zig_stubs, stubs, stub_test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_da(spn_zig_stub_t) links = sp_da_new(mem, spn_zig_stub_t);
  u32 num_links = 0;
  sp_carr_detect_len(it->links, num_links, it->links[num_links].kind);
  sp_for(l, num_links) {
    spn_zig_stub_t link = {
      .kind = it->links[l].kind,
      .lang = it->links[l].lang,
      .system_libs = sp_da_new(mem, sp_str_t),
    };
    u32 libs = 0;
    sp_carr_detect_len(it->links[l].libs, libs, it->links[l].libs[libs]);
    sp_for(b, libs) {
      sp_da_push(link.system_libs, sp_cstr_as_str(it->links[l].libs[b]));
    }
    sp_da_push(links, link);
  }

  sp_da(spn_zig_stub_t) stubs = spn_zig_stubs(mem, it->os, links);

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].kind);
  sp_must_eq(t, sp_da_size(stubs), expected);
  sp_for(s, expected) {
    const link_t* expect = &it->expect[s];
    sp_test_kv_c(t, "stub", expect->lang == SPN_LANG_CXX ? "cxx" : "c");
    sp_expect_eq(t, stubs[s].kind, expect->kind);
    sp_expect_eq(t, stubs[s].lang, expect->lang);

    u32 libs = 0;
    sp_carr_detect_len(expect->libs, libs, expect->libs[libs]);
    sp_must_eq(t, sp_da_size(stubs[s].system_libs), libs);
    sp_for(b, libs) {
      sp_expect_str_eq_c(t, stubs[s].system_libs[b], expect->libs[b]);
    }
  }

  return SP_OK;
}
