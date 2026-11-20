#include "toolchain.h"

#define max_installs 4

typedef struct {
  const c8* install;
  const c8* version;
  bool arm64;
  bool preferred;
  bool bare;
} toolset_t;

typedef struct {
  spn_msvc_match_t match;
  const c8* install;
  const c8* toolset;
  const c8* kit;
} expect_t;

typedef struct {
  const c8* name;
  toolset_t toolsets [max_installs];
  const c8* kits [max_installs];
  const c8* toolset;
  const c8* sdk;
  bool arm64;
  bool compiler;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unpinned_takes_the_first_preferred",
    .toolsets = { { "A", "14.44" }, { "B", "14.29", .preferred = true }, { "C", "14.16", .preferred = true } },
    .kits = { "10.0" },
    .expect = { .install = "B", .toolset = "14.29", .kit = "10.0" },
  },
  {
    .name = "unpinned_without_a_preferred_takes_the_first",
    .toolsets = { { "A", "14.29" }, { "A", "14.44" } },
    .kits = { "10.0" },
    .expect = { .install = "A", .toolset = "14.29", .kit = "10.0" },
  },
  {
    .name = "generation_takes_its_range",
    .toolsets = { { "A", "14.52" }, { "A", "14.29" }, { "A", "14.30" } },
    .kits = { "10.0" },
    .toolset = "v142",
    .expect = { .install = "A", .toolset = "14.29", .kit = "10.0" },
  },
  {
    .name = "year_is_its_generation",
    .toolsets = { { "A", "14.29" }, { "A", "14.44" } },
    .kits = { "10.0" },
    .toolset = "2022",
    .expect = { .install = "A", .toolset = "14.44", .kit = "10.0" },
  },
  {
    .name = "newest_generation_is_open_ended",
    .toolsets = { { "A", "14.44" }, { "A", "14.61" } },
    .kits = { "10.0" },
    .toolset = "v145",
    .expect = { .install = "A", .toolset = "14.61", .kit = "10.0" },
  },
  {
    .name = "prefix_takes_the_highest",
    .toolsets = { { "A", "14.29.1" }, { "A", "14.29.2" }, { "A", "14.30.0" } },
    .kits = { "10.0" },
    .toolset = "14.29",
    .expect = { .install = "A", .toolset = "14.29.2", .kit = "10.0" },
  },
  {
    .name = "prefix_matches_whole_components",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0" },
    .toolset = "14.2",
    .expect = { .match = SPN_MSVC_MATCH_TOOLSET },
  },
  {
    .name = "equal_versions_take_the_first",
    .toolsets = { { "A", "14.29" }, { "B", "14.29" } },
    .kits = { "10.0" },
    .toolset = "14.29",
    .expect = { .install = "A", .toolset = "14.29", .kit = "10.0" },
  },
  {
    .name = "toolset_without_a_match",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0" },
    .toolset = "v141",
    .expect = { .match = SPN_MSVC_MATCH_TOOLSET },
  },
  {
    .name = "sdk_unpinned_takes_the_newest",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0.1.0", "10.0.3.0", "10.0.2.0" },
    .expect = { .install = "A", .toolset = "14.29", .kit = "10.0.3.0" },
  },
  {
    .name = "sdk_prefix_takes_the_highest",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0.1.0", "10.0.1.5", "10.0.2.0" },
    .sdk = "10.0.1",
    .expect = { .install = "A", .toolset = "14.29", .kit = "10.0.1.5" },
  },
  {
    .name = "sdk_without_a_match",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0.1.0" },
    .sdk = "10.0.2",
    .expect = { .match = SPN_MSVC_MATCH_SDK },
  },
  {
    .name = "other_arch_is_absent",
    .toolsets = { { "A", "14.29" } },
    .kits = { "10.0" },
    .arm64 = true,
    .expect = { .match = SPN_MSVC_MATCH_ABSENT },
  },
  {
    .name = "no_kit_is_absent",
    .toolsets = { { "A", "14.29" } },
    .expect = { .match = SPN_MSVC_MATCH_ABSENT },
  },
  {
    .name = "compiler_skips_bare_toolsets",
    .toolsets = { { "A", "14.44", .preferred = true, .bare = true }, { "B", "14.29", .preferred = true } },
    .kits = { "10.0" },
    .compiler = true,
    .expect = { .install = "B", .toolset = "14.29", .kit = "10.0" },
  },
  {
    .name = "bare_toolsets_serve_without_a_compiler",
    .toolsets = { { "A", "14.44", .bare = true } },
    .kits = { "10.0" },
    .expect = { .install = "A", .toolset = "14.44", .kit = "10.0" },
  },
};

static spn_arch_t arch_of(bool arm64) {
  return arm64 ? SPN_ARCH_ARM64 : SPN_ARCH_X64;
}

sp_test_each(msvc_match, pick, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_msvc_installs_t installs = { .toolsets = sp_da_new(mem, sp_msvc_vs_t), .kits = sp_da_new(mem, sp_msvc_sdk_t) };
  sp_carr_for(it->toolsets, at) {
    const toolset_t* toolset = &it->toolsets[at];
    if (!toolset->install) {
      break;
    }
    sp_da_push(installs.toolsets, ((sp_msvc_vs_t) {
      .version.tools = fixture_msvc_version(toolset->version),
      .install_path = fixture_msvc_path(toolset->install),
      .target = spn_msvc_arch(arch_of(toolset->arm64)),
      .preferred = toolset->preferred,
      .compiler = !toolset->bare,
    }));
  }
  sp_carr_for(it->kits, at) {
    if (!it->kits[at]) {
      break;
    }
    sp_da_push(installs.kits, ((sp_msvc_sdk_t) { .version = fixture_msvc_version(it->kits[at]), .arch = spn_msvc_arch(arch_of(it->arm64)) }));
  }

  spn_msvc_request_t request = { .toolset = sp_cstr_as_str(it->toolset), .sdk = sp_cstr_as_str(it->sdk) };
  spn_msvc_pick_t pick = sp_zero;
  sp_must_eq(t, (u32)it->expect.match, (u32)spn_msvc_match(&installs, arch_of(it->arm64), request, it->compiler, &pick));
  if (it->expect.match) {
    return SP_OK;
  }
  sp_expect_str_eq_c(t, sp_msvc_path_str(&pick.toolset->install_path), it->expect.install);
  sp_expect_str_eq_c(t, sp_msvc_version_str(&pick.toolset->version.tools), it->expect.toolset);
  sp_expect_str_eq_c(t, sp_msvc_version_str(&pick.kit->version), it->expect.kit);
  return SP_OK;
}
