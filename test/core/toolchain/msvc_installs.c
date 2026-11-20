#include "toolchain.h"

#define max_found 3

typedef struct {
  const c8* name;
  const c8* toolsets [max_found];
  const c8* kits [max_found];
} test_t;

static const test_t tests [] = {
  { .name = "every_toolset_and_kit_is_kept", .toolsets = { "14.2", "14.1" }, .kits = { "10.2", "10.1" } },
};

sp_test_each(msvc_installs, add, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_msvc_t* found = sp_alloc_type(mem, sp_msvc_t);
  sp_carr_detect_len(it->toolsets, found->num_installations, it->toolsets[found->num_installations]);
  sp_carr_detect_len(it->kits, found->num_sdks, it->kits[found->num_sdks]);
  sp_for(at, found->num_installations) {
    found->installations[at].version.tools = fixture_msvc_version(it->toolsets[at]);
  }
  sp_for(at, found->num_sdks) {
    found->sdks[at].version = fixture_msvc_version(it->kits[at]);
  }

  spn_msvc_installs_t installs = { .toolsets = sp_da_new(mem, sp_msvc_vs_t), .kits = sp_da_new(mem, sp_msvc_sdk_t) };
  spn_msvc_installs_add(&installs, found);

  sp_must_eq(t, found->num_installations, (u32)sp_da_size(installs.toolsets));
  sp_must_eq(t, found->num_sdks, (u32)sp_da_size(installs.kits));
  sp_for(at, found->num_installations) {
    sp_expect_str_eq_c(t, sp_msvc_version_str(&installs.toolsets[at].version.tools), it->toolsets[at]);
  }
  sp_for(at, found->num_sdks) {
    sp_expect_str_eq_c(t, sp_msvc_version_str(&installs.kits[at].version), it->kits[at]);
  }
  return SP_OK;
}
