#ifndef SPN_TEST_CONTEND_H
#define SPN_TEST_CONTEND_H

#include "sp.h"
#include "sp/sp_test.h"

#include "fs/fs.h"

#define test_contend_wait_ms 50
#define test_contend_grace_ms 5000

static inline sp_err_t test_run_contended(sp_test_t* t, sp_str_t lock, sp_thread_fn_t run, void* job, sp_atomic_s32_t* done, bool waits) {
  sp_fs_lock_t held = sp_zero;
  sp_must_ok(t, sp_fs_lock_acquire(&held, sp_path_from_str(lock)));

  u32 window = waits ? test_contend_wait_ms : test_contend_grace_ms;
  sp_thread_t thread = sp_zero;
  sp_thread_init(&thread, run, job);
  sp_for(elapsed, window) {
    if (sp_atomic_s32_load(done, SP_ATOMIC_SEQ_CST)) {
      break;
    }
    sp_os_sleep_ms(1);
  }
  sp_expect_eq(t, !sp_atomic_s32_load(done, SP_ATOMIC_SEQ_CST), waits);

  sp_fs_lock_release(&held);
  sp_thread_join(&thread);
  return SP_OK;
}

#endif
