#include "spn_test.h"

#include "external/net.h"

#define backoff_test_cancel_ms 30

typedef enum {
  CANCEL_NEVER,
  CANCEL_BEFORE,
  CANCEL_DURING,
} cancel_t;

typedef struct {
  bool retry;
  u32 at_least_ms;
  u32 under_ms;
} expect_t;

typedef struct {
  const c8* name;
  spn_net_backoff_t backoff;
  u32 retry;
  cancel_t cancel;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "first_retry_waits",
    .backoff = { .wait_ms = { 30 } },
    .expect = { .retry = true, .at_least_ms = 30 },
  },
  {
    .name = "each_retry_has_its_own_wait",
    .backoff = { .wait_ms = { 1, 30 } },
    .retry = 1,
    .expect = { .retry = true, .at_least_ms = 30 },
  },
  {
    .name = "empty_schedule_has_no_retry",
  },
  {
    .name = "zero_wait_ends_the_schedule",
    .backoff = { .wait_ms = { 1 } },
    .retry = 1,
  },
  {
    .name = "full_schedule_ends_after_its_last_wait",
    .backoff = { .wait_ms = { 1, 1, 1 } },
    .retry = spn_net_max_retries,
  },
  {
    .name = "cancel_before_the_wait_ends_it",
    .backoff = { .wait_ms = { 60000 } },
    .cancel = CANCEL_BEFORE,
    .expect = { .retry = true, .under_ms = 60000 },
  },
  {
    .name = "cancel_during_the_wait_ends_it",
    .backoff = { .wait_ms = { 60000 } },
    .cancel = CANCEL_DURING,
    .expect = { .retry = true, .at_least_ms = backoff_test_cancel_ms, .under_ms = 60000 },
  },
};

static s32 cancel_later(void* user) {
  sp_os_sleep_ms(backoff_test_cancel_ms);
  sp_atomic_s32_store((sp_atomic_s32_t*)user, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

sp_test_each(backoff, wait, test_t, tests) {
  sp_atomic_s32_t cancelled = sp_zero;
  sp_thread_t thread = sp_zero;
  sp_tm_timer_t timer = sp_tm_start_timer();
  switch (it->cancel) {
    case CANCEL_NEVER: {
      break;
    }
    case CANCEL_BEFORE: {
      sp_atomic_s32_store(&cancelled, 1, SP_ATOMIC_SEQ_CST);
      break;
    }
    case CANCEL_DURING: {
      sp_thread_init(&thread, cancel_later, &cancelled);
      break;
    }
  }

  bool retry = spn_net_backoff_wait(it->backoff, it->retry, &cancelled);
  u64 waited_ms = sp_tm_ns_to_ms(sp_tm_read_timer(&timer));
  if (it->cancel == CANCEL_DURING) {
    sp_thread_join(&thread);
  }

  sp_expect_eq(t, retry, it->expect.retry);
  sp_expect_ge(t, waited_ms, (u64)it->expect.at_least_ms);
  if (it->expect.under_ms) {
    sp_expect_lt(t, waited_ms, (u64)it->expect.under_ms);
  }
  return SP_OK;
}
