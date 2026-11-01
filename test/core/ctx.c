#include "spn_test.h"

#include "ctx/ctx.h"

sp_test(ctx, host) {
  spn_triple_t host = spn_host_detect();
  sp_expect_ne(t, host.arch, SPN_ARCH_NONE);
  switch (host.os) {
    case SPN_OS_LINUX: {
      sp_expect(t, host.abi == SPN_ABI_GNU || host.abi == SPN_ABI_MUSL);
      break;
    }
    case SPN_OS_MACOS: {
      sp_expect_eq(t, host.abi, SPN_ABI_APPLE);
      break;
    }
    case SPN_OS_WINDOWS: {
      sp_expect_eq(t, host.abi, SPN_ABI_NONE);
      break;
    }
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: {
      sp_unreachable_case();
    }
  }
  return SP_OK;
}
