#include "spn_test.h"

#include "profile/profile.h"
#include "toolchain/types.h"

#define LINUX { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU }
#define MACOS { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_APPLE }

typedef struct {
  const c8* name;
  spn_triple_t host;
  bool offered;
  bool selected;
  spn_host_refusal_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "host_row_links_host_libs",           .host = LINUX, .offered = true, .selected = true },
  { .name = "another_row_of_a_host_toolchain",    .host = LINUX, .offered = true, .expect = SPN_HOST_REFUSAL_TARGET },
  { .name = "toolchain_without_a_host_row",       .host = LINUX, .expect = SPN_HOST_REFUSAL_TOOLCHAIN },
  { .name = "host_os_is_checked_before_the_row",  .host = MACOS, .offered = true, .selected = true, .expect = SPN_HOST_REFUSAL_OS },
};

sp_test_each(profile_host, refusal, test_t, tests) {
  spn_toolchain_info_t info = { .driver = SPN_CC_DRIVER_GCC };
  sp_da_init(sp_test_arena(t), info.rows);
  sp_da_push(info.rows, ((spn_toolchain_row_t) { .triple = it->host, .host = it->offered }));
  spn_toolchain_selection_t selection = { .toolchain = &info, .row = { .triple = it->host, .host = it->selected } };

  spn_profile_info_t profile = sp_zero;
  spn_profile_finalize(&profile, it->host, &selection);
  sp_expect_eq(t, (u32)it->expect, (u32)profile.host_refusal);
  return SP_OK;
}
