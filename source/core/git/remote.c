#include "sp.h"
#include "macro/macro.h"
#include "git/remote.h"

#include "external/git_cli.h"

static spn_err_t fetch(spn_git_transport_t transport, sp_str_t repo, sp_str_t url, spn_git_fetch_t what, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  spn_err_t err = SPN_ERROR;
  u32 retry = 0;
  while (!sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    switch (transport) {
      case SPN_GIT_TRANSPORT_BUILTIN: {
        err = spn_git_builtin_fetch(repo, what, cancelled, failure);
        break;
      }
      case SPN_GIT_TRANSPORT_SYSTEM: {
        err = spn_git_cli_fetch(repo, what, failure);
        break;
      }
    }
    if (failure->kind != SPN_GIT_DROPPED || !spn_net_backoff_wait(backoff, retry++, url, sp_cstr_as_str(failure->message), cancelled)) {
      break;
    }
  }
  if (err && sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    *failure = (spn_git_failure_t) { .kind = SPN_GIT_CANCELLED, .message = "cancelled" };
  }
  return err;
}

spn_err_t spn_git_clone(spn_git_transport_t transport, sp_str_t url, sp_str_t path, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  spn_err_t err = SPN_ERROR;
  u32 retry = 0;
  while (!sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    switch (transport) {
      case SPN_GIT_TRANSPORT_BUILTIN: {
        err = spn_git_builtin_clone(url, path, cancelled, failure);
        break;
      }
      case SPN_GIT_TRANSPORT_SYSTEM: {
        err = spn_git_cli_clone(url, path, failure);
        break;
      }
    }
    if (failure->kind != SPN_GIT_DROPPED || !spn_net_backoff_wait(backoff, retry++, url, sp_cstr_as_str(failure->message), cancelled)) {
      break;
    }
  }
  if (err && sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    *failure = (spn_git_failure_t) { .kind = SPN_GIT_CANCELLED, .message = "cancelled" };
  }
  return err;
}

spn_err_t spn_git_fetch(spn_git_transport_t transport, sp_str_t repo, sp_str_t url, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  return fetch(transport, repo, url, sp_zero_s(spn_git_fetch_t), backoff, cancelled, failure);
}

spn_err_t spn_git_fetch_rev(spn_git_transport_t transport, sp_str_t repo, sp_str_t url, sp_str_t rev, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_git_fetch_t tip = {
    .refspecs = { sp_str_to_cstr(scratch.mem, rev) },
    .depth = SPN_GIT_DEPTH_TIP,
    .no_tags = true,
  };
  spn_err_t shallow = fetch(transport, repo, url, tip, backoff, cancelled, failure);
  sp_mem_end_scratch(scratch);

  if (shallow && failure->kind != SPN_GIT_FAILED) {
    return SPN_ERROR;
  }
  if (spn_git_has_rev(repo, rev)) {
    return SPN_OK;
  }

  spn_git_fetch_t mirror = {
    .refspecs = { "+refs/heads/*:refs/heads/*", "+refs/tags/*:refs/tags/*" },
    .depth = spn_git_is_shallow(repo) ? SPN_GIT_DEPTH_UNSHALLOW : SPN_GIT_DEPTH_FULL,
  };
  return fetch(transport, repo, url, mirror, backoff, cancelled, failure);
}
