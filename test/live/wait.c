#include "spn_test.h"

#include "external/net.h"

#define wait_test_cancel_ms 30
#define wait_test_long_ms 60000

static s32 cancel_later(void* user) {
  sp_os_sleep_ms(wait_test_cancel_ms);
  sp_atomic_s32_store((sp_atomic_s32_t*)user, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

sp_test(wait, cancel_ends_it) {
  sp_atomic_s32_t cancelled = sp_zero;
  sp_thread_t thread = sp_zero;
  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_thread_init(&thread, cancel_later, &cancelled);

  spn_net_wait(SP_NULLPTR, wait_test_long_ms, &cancelled);
  u64 waited_ms = sp_tm_ns_to_ms(sp_tm_read_timer(&timer));
  sp_thread_join(&thread);

  sp_expect_ge(t, waited_ms, (u64)wait_test_cancel_ms);
  sp_expect_lt(t, waited_ms, (u64)wait_test_long_ms);
  return SP_OK;
}
