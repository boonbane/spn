#include "../git.h"

#include "paths/paths.h"

typedef enum {
  FAULT_NONE,
  FAULT_PATCH,
  FAULT_REV,
  FAULT_DIR,
} fault_t;

typedef struct {
  const c8* name;
  fault_t fault;
} test_t;

static const test_t tests [] = {
  {
    .name = "patch_that_does_not_apply",
    .fault = FAULT_PATCH,
  },
  {
    .name = "rev_the_remote_does_not_have",
    .fault = FAULT_REV,
  },
  {
    .name = "subdirectory_the_commit_does_not_have",
    .fault = FAULT_DIR,
  },
};

sp_test_each(git_cache, failure, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  git_repo_fixture_t fixture = {
    .commits = {
      { .message = "A", .files = { { "D/F", "A" } } },
    },
  };
  git_repo_result_t remote = git_repo_build_at(test_dir_str(t), "A", &fixture);

  spn_git_checkout_id_t sound = { .url = remote.path, .rev = remote.commits[0] };
  spn_git_checkout_id_t faulty = sound;
  switch (it->fault) {
    case FAULT_NONE: {
      break;
    }
    case FAULT_PATCH: {
      u32 missing = 0;
      sp_str_t fits = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B.patch"));
      sp_try(git_edit_write(t, (git_edit_t) { .file = "D/F", .from = "A", .to = "B" }, fits));
      si_da_push(mem, sound.patches.files, fits);
      sound.patches.hash = spn_git_patch_hash(sound.patches.files, &missing);

      sp_str_t conflicts = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("C.patch"));
      sp_try(git_edit_write(t, (git_edit_t) { .file = "D/F", .from = "B", .to = "C" }, conflicts));
      si_da_push(mem, faulty.patches.files, conflicts);
      faulty.patches.hash = spn_git_patch_hash(faulty.patches.files, &missing);
      break;
    }
    case FAULT_REV: {
      faulty.rev = sp_str_lit("B");
      break;
    }
    case FAULT_DIR: {
      sound.dir = sp_str_lit("D");
      faulty.dir = sp_str_lit("E");
      break;
    }
  }

  spn_path_t root = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")) };
  spn_path_roots_t roots = sp_zero;
  sp_atomic_s32_t cancelled = sp_zero;
  bool fetching = false;
  sp_for(attempt, 2) {
    spn_git_cache_t cache = sp_zero;
    spn_git_cache_init(&cache, mem, SP_NULLPTR, &roots, spn_path_join(mem, root, sp_str_lit("db")), spn_path_join(mem, root, sp_str_lit("checkouts")), spn_path_join(mem, root, sp_str_lit("locks")));

    spn_git_checkout_t* control = spn_git_cache_checkout(&cache, sound, &fetching);
    spn_err_t err = spn_git_checkout_ensure(&cache, control, &cancelled);
    sp_test_kv_c(t, "message", control->failure.message);
    sp_must_eq(t, (u32)err, (u32)SPN_OK);

    spn_git_checkout_t* checkout = spn_git_cache_checkout(&cache, faulty, &fetching);
    err = spn_git_checkout_ensure(&cache, checkout, &cancelled);
    sp_test_kv_c(t, "message", checkout->failure.message);
    sp_expect_eq(t, (u32)err, (u32)SPN_ERROR);
    sp_expect_eq(t, (u32)checkout->failure.kind, (u32)SPN_GIT_FAILED);
    sp_expect(t, !checkout->failure.remote);
    sp_expect(t, checkout->failure.message[0] != 0);
  }
  return SP_OK;
}
