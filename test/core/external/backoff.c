#include "spn_test.h"

#include "event/event.h"
#include "external/net.h"

#define backoff_test_cancel_ms 30
#define backoff_test_long_ms 60000

typedef struct {
  bool retry;
  u32 waited_ms;
  u32 remaining;
} expect_t;

typedef struct {
  const c8* name;
  u32 wait_ms [spn_net_max_retries];
  u32 retry;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "each_retry_has_its_own_wait",
    .wait_ms = { 1, 2 },
    .retry = 1,
    .expect = { .retry = true, .waited_ms = 2, .remaining = 1 },
  },
  {
    .name = "notice_counts_the_tries_left",
    .wait_ms = { 1, 2, 3 },
    .retry = 1,
    .expect = { .retry = true, .waited_ms = 2, .remaining = 2 },
  },
  {
    .name = "zero_wait_ends_the_schedule",
    .wait_ms = { 1 },
    .retry = 1,
  },
  {
    .name = "full_schedule_ends_after_its_last_wait",
    .wait_ms = { 1, 2, 3 },
    .retry = spn_net_max_retries,
  },
};

static void record_wait(void* user, u32 ms, sp_atomic_s32_t* cancelled) {
  *(u32*)user += ms;
}

sp_test_each(backoff, wait, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_atomic_s32_t cancelled = sp_zero;
  u32 waited_ms = 0;
  spn_net_backoff_t backoff = { .wait = record_wait, .user = &waited_ms };
  sp_carr_for(it->wait_ms, at) {
    backoff.wait_ms[at] = it->wait_ms[at];
  }
  bool retry = spn_net_backoff_wait(backoff, it->retry, sp_str_lit("A"), sp_str_lit("B"), &cancelled);

  u32 notices = 0;
  u32 remaining = 0;
  sp_da(spn_event_t) events = spn_event_buffer_drain(sp_test_arena(t), spn.events);
  sp_da_for(events, at) {
    if (events[at].kind == SPN_EVENT_SYNC_RETRY) {
      notices++;
      remaining = events[at].sync_retry.remaining;
      sp_expect_str_eq(t, events[at].sync_retry.url, sp_str_lit("A"));
      sp_expect_str_eq(t, events[at].sync_retry.cause, sp_str_lit("B"));
    }
  }

  sp_expect_eq(t, retry, it->expect.retry);
  sp_expect_eq(t, waited_ms, it->expect.waited_ms);
  sp_expect_eq(t, notices, it->expect.remaining ? 1u : 0u);
  sp_expect_eq(t, remaining, it->expect.remaining);
  return SP_OK;
}

static s32 cancel_later(void* user) {
  sp_os_sleep_ms(backoff_test_cancel_ms);
  sp_atomic_s32_store((sp_atomic_s32_t*)user, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

sp_test(backoff, cancel_during_the_wait_ends_it) {
  sp_atomic_s32_t cancelled = sp_zero;
  sp_thread_t thread = sp_zero;
  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_thread_init(&thread, cancel_later, &cancelled);

  spn_net_wait(SP_NULLPTR, backoff_test_long_ms, &cancelled);
  u64 waited_ms = sp_tm_ns_to_ms(sp_tm_read_timer(&timer));
  sp_thread_join(&thread);

  sp_expect_ge(t, waited_ms, (u64)backoff_test_cancel_ms);
  sp_expect_lt(t, waited_ms, (u64)backoff_test_long_ms);
  return SP_OK;
}
