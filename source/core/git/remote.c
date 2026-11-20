#include "sp.h"
#include "macro/macro.h"
#include "git/remote.h"

#include "external/git_cli.h"
#include "git/url.h"

static spn_git_route_t route_for(spn_git_transport_t transport, sp_str_t url) {
  switch (transport) {
    case SPN_GIT_TRANSPORT_BUILTIN: {
      return spn_git_url_is_ssh(url) ? SPN_GIT_ROUTE_SSH : SPN_GIT_ROUTE_BUILTIN;
    }
    case SPN_GIT_TRANSPORT_SYSTEM: {
      return SPN_GIT_ROUTE_CONFIGURED;
    }
  }
  sp_unreachable_return(SPN_GIT_ROUTE_BUILTIN);
}

static spn_err_t clone_over(void* user, spn_git_route_t route, sp_str_t url, sp_str_t path, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  switch (route) {
    case SPN_GIT_ROUTE_BUILTIN: {
      return spn_git_builtin_clone(url, path, cancelled, failure);
    }
    case SPN_GIT_ROUTE_CONFIGURED:
    case SPN_GIT_ROUTE_SSH:
    case SPN_GIT_ROUTE_LOGIN: {
      return spn_git_cli_clone(url, path, failure);
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t fetch_over(void* user, spn_git_route_t route, sp_str_t repo, spn_git_fetch_t what, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  switch (route) {
    case SPN_GIT_ROUTE_BUILTIN: {
      return spn_git_builtin_fetch(repo, what, cancelled, failure);
    }
    case SPN_GIT_ROUTE_CONFIGURED:
    case SPN_GIT_ROUTE_SSH:
    case SPN_GIT_ROUTE_LOGIN: {
      return spn_git_cli_fetch(repo, what, failure);
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t tip_over(void* user, spn_git_route_t route, sp_mem_t mem, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  switch (route) {
    case SPN_GIT_ROUTE_BUILTIN: {
      return si_git_builtin_tip(mem, url, cancelled, rev, failure);
    }
    case SPN_GIT_ROUTE_CONFIGURED:
    case SPN_GIT_ROUTE_SSH:
    case SPN_GIT_ROUTE_LOGIN: {
      return si_git_cli_tip(mem, url, rev, failure);
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

spn_git_remote_t si_git_remote(spn_git_transport_t transport) {
  return (spn_git_remote_t) { .transport = transport, .clone = clone_over, .fetch = fetch_over, .tip = tip_over };
}

typedef struct {
  spn_git_route_t route;
  u32 retries;
} attempt_t;

static bool again(attempt_t* attempt, const spn_git_failure_t* failure, spn_net_backoff_t backoff, sp_str_t url, sp_atomic_s32_t* cancelled) {
  if (failure->kind == SPN_GIT_UNAUTHORIZED) {
    attempt->route = SPN_GIT_ROUTE_LOGIN;
    return true;
  }
  return failure->kind == SPN_GIT_DROPPED && spn_net_backoff_wait(backoff, attempt->retries++, url, sp_cstr_as_str(failure->message), cancelled);
}

static spn_err_t settle(attempt_t attempt, spn_err_t err, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  if (err && sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    *failure = (spn_git_failure_t) { .kind = SPN_GIT_CANCELLED, .message = "cancelled" };
  }
  failure->route = attempt.route;
  return err;
}

static spn_err_t fetch(spn_git_remote_t remote, spn_git_route_t* route, sp_str_t repo, sp_str_t url, spn_git_fetch_t what, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_assert(remote.fetch);
  sp_assert(!backoff.wait_ms[0] || backoff.wait);
  attempt_t attempt = { .route = *route };
  spn_err_t err = SPN_ERROR;
  while (!sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    err = remote.fetch(remote.user, attempt.route, repo, what, cancelled, failure);
    if (!again(&attempt, failure, backoff, url, cancelled)) {
      break;
    }
  }
  *route = attempt.route;
  return settle(attempt, err, cancelled, failure);
}

spn_err_t si_git_clone(spn_git_remote_t remote, sp_str_t url, sp_str_t path, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_assert(remote.clone);
  sp_assert(!backoff.wait_ms[0] || backoff.wait);
  attempt_t attempt = { .route = route_for(remote.transport, url) };
  spn_err_t err = SPN_ERROR;
  while (!sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    err = remote.clone(remote.user, attempt.route, url, path, cancelled, failure);
    if (!again(&attempt, failure, backoff, url, cancelled)) {
      break;
    }
  }
  return settle(attempt, err, cancelled, failure);
}

spn_err_t si_git_tip(spn_git_remote_t remote, sp_mem_t mem, sp_str_t url, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  sp_assert(remote.tip);
  sp_assert(!backoff.wait_ms[0] || backoff.wait);
  attempt_t attempt = { .route = route_for(remote.transport, url) };
  spn_err_t err = SPN_ERROR;
  while (!sp_atomic_s32_load(cancelled, SP_ATOMIC_SEQ_CST)) {
    err = remote.tip(remote.user, attempt.route, mem, url, cancelled, rev, failure);
    if (!again(&attempt, failure, backoff, url, cancelled)) {
      break;
    }
  }
  return settle(attempt, err, cancelled, failure);
}

spn_err_t spn_git_fetch(spn_git_remote_t remote, sp_str_t repo, sp_str_t url, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  spn_git_route_t route = route_for(remote.transport, url);
  return fetch(remote, &route, repo, url, sp_zero_s(spn_git_fetch_t), backoff, cancelled, failure);
}

spn_err_t spn_git_fetch_rev(spn_git_remote_t remote, sp_str_t repo, sp_str_t url, sp_str_t rev, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  spn_git_route_t route = route_for(remote.transport, url);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_git_fetch_t tip = {
    .refspecs = { sp_str_to_cstr(scratch.mem, rev) },
    .depth = SPN_GIT_DEPTH_TIP,
    .no_tags = true,
  };
  spn_err_t shallow = fetch(remote, &route, repo, url, tip, backoff, cancelled, failure);
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
  return fetch(remote, &route, repo, url, mirror, backoff, cancelled, failure);
}
