#include "spn_test.h"

#include "contend.h"
#include "event/event.h"
#include "paths/paths.h"
#include "toolchain/provision.h"
#include "toolchain/toolchain.h"

typedef struct {
  u32 notices;
} expect_t;

typedef struct {
  const c8* name;
  bool contended;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "free_claim_is_silent",
  },
  {
    .name = "held_claim_says_it_waits",
    .contended = true,
    .expect = { .notices = 1 },
  },
};

typedef struct {
  spn_toolchain_store_t* store;
  sp_mem_t mem;
  spn_artifact_t artifact;
  spn_toolchain_claim_t claim;
  sp_atomic_s32_t done;
} job_t;

static s32 run(void* user) {
  job_t* job = (job_t*)user;
  job->claim = spn_toolchain_claim(job->store, job->mem, sp_str_lit("A"), job->artifact);
  sp_atomic_s32_store(&job->done, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

sp_test_each(claim, wait, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"))));
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &roots,
  };

  job_t job = {
    .store = &store,
    .mem = mem,
    .artifact = { .sha256 = test_sha256_hex(mem, sp_str_lit("A")) },
  };
  if (it->contended) {
    spn_path_t lock = spn_path_concat(mem, spn_toolchain_artifact_root(mem, job.artifact), ".lock");
    sp_try(test_run_contended(t, spn_path_str(&roots, mem, lock), run, &job, &job.done, true));
  }
  else {
    run(&job);
  }
  spn_toolchain_release(&store, &job.claim);

  u32 notices = 0;
  sp_da(spn_event_t) events = spn_event_buffer_drain(mem, spn.events);
  sp_da_for(events, at) {
    if (events[at].kind == SPN_EVENT_LOCK_WAIT) {
      notices++;
    }
  }
  sp_expect_eq(t, notices, it->expect.notices);

  spn_path_roots_close(&roots);
  return SP_OK;
}
