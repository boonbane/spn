#include "spn_test.h"

#include "event/event.h"
#include "git/remote.h"

#define max_replies 4
#define max_attempts 8

typedef enum {
  OP_CLONE,
  OP_FETCH,
  OP_TIP,
} op_t;

typedef enum {
  REPLY_OK,
  REPLY_FAILED,
  REPLY_DROPPED,
  REPLY_UNAUTHORIZED,
  REPLY_CANCEL,
} reply_t;

typedef struct {
  spn_err_t err;
  spn_git_failure_kind_t kind;
  spn_git_route_t route;
  const c8* routes;
  u32 waits [spn_net_max_retries];
} expect_t;

typedef struct {
  const c8* name;
  spn_git_transport_t transport;
  const c8* url;
  bool cancelled;
  reply_t replies [max_replies];
  expect_t expect;
} test_t;

typedef struct {
  const test_t* test;
  u32 replied;
  c8 routes [max_attempts + 1];
  u32 attempted;
  u32 waits [spn_net_max_retries];
  u32 waited;
} script_t;

sp_test_suite(git_remote, .serial = true);

static const test_t tests [] = {
  {
    .name = "builtin_transport_uses_libgit2",
    .expect = { .routes = "B" },
  },
  {
    .name = "ssh_url_uses_system_git",
    .url = "git@A:B",
    .expect = { .route = SPN_GIT_ROUTE_SSH, .routes = "S" },
  },
  {
    .name = "system_transport_uses_configured_git",
    .transport = SPN_GIT_TRANSPORT_SYSTEM,
    .expect = { .route = SPN_GIT_ROUTE_CONFIGURED, .routes = "C" },
  },
  {
    .name = "unauthorized_moves_to_login",
    .replies = { REPLY_UNAUTHORIZED },
    .expect = { .route = SPN_GIT_ROUTE_LOGIN, .routes = "BL" },
  },
  {
    .name = "dropped_is_retried",
    .replies = { REPLY_DROPPED, REPLY_DROPPED },
    .expect = { .routes = "BBB", .waits = { 1, 2 } },
  },
  {
    .name = "dropped_fails_after_the_schedule",
    .replies = { REPLY_DROPPED, REPLY_DROPPED, REPLY_DROPPED, REPLY_DROPPED },
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_DROPPED, .routes = "BBBB", .waits = { 1, 2, 3 } },
  },
  {
    .name = "failure_is_not_retried",
    .replies = { REPLY_FAILED },
    .expect = { .err = SPN_ERROR, .routes = "B" },
  },
  {
    .name = "cancelled_run_is_not_attempted",
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED, .routes = "" },
  },
  {
    .name = "cancel_ends_the_retries",
    .replies = { REPLY_CANCEL },
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED, .routes = "B", .waits = { 1 } },
  },
};

static spn_err_t attempt(script_t* script, spn_git_route_t route, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  static const c8 names [] = {
    [SPN_GIT_ROUTE_BUILTIN] = 'B',
    [SPN_GIT_ROUTE_CONFIGURED] = 'C',
    [SPN_GIT_ROUTE_SSH] = 'S',
    [SPN_GIT_ROUTE_LOGIN] = 'L',
  };
  sp_assert(script->attempted < max_attempts);
  script->routes[script->attempted++] = names[route];

  reply_t reply = script->replied < max_replies ? script->test->replies[script->replied++] : REPLY_OK;
  *failure = (spn_git_failure_t) { .message = "A" };
  switch (reply) {
    case REPLY_OK: {
      *failure = sp_zero_s(spn_git_failure_t);
      return SPN_OK;
    }
    case REPLY_FAILED: {
      return SPN_ERROR;
    }
    case REPLY_DROPPED: {
      failure->kind = SPN_GIT_DROPPED;
      return SPN_ERROR;
    }
    case REPLY_UNAUTHORIZED: {
      failure->kind = SPN_GIT_UNAUTHORIZED;
      return SPN_ERROR;
    }
    case REPLY_CANCEL: {
      sp_atomic_s32_store(cancelled, 1, SP_ATOMIC_SEQ_CST);
      failure->kind = SPN_GIT_DROPPED;
      return SPN_ERROR;
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t try_clone(void* user, spn_git_route_t route, sp_str_t url, sp_str_t path, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  return attempt((script_t*)user, route, cancelled, failure);
}

static spn_err_t try_fetch(void* user, spn_git_route_t route, sp_str_t repo, spn_git_fetch_t what, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  return attempt((script_t*)user, route, cancelled, failure);
}

static spn_err_t try_tip(void* user, spn_git_route_t route, sp_mem_t mem, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  return attempt((script_t*)user, route, cancelled, failure);
}

static void record_wait(void* user, u32 ms, sp_atomic_s32_t* cancelled) {
  script_t* script = (script_t*)user;
  sp_assert(script->waited < spn_net_max_retries);
  script->waits[script->waited++] = ms;
}

sp_test_each(git_remote, attempts, test_t, tests, .setup = spn_test_ctx_setup) {
  static const c8* ops [] = {
    [OP_CLONE] = "clone",
    [OP_FETCH] = "fetch",
    [OP_TIP] = "tip",
  };
  sp_str_t url = sp_cstr_as_str(it->url ? it->url : "A");

  sp_carr_for(ops, op) {
    sp_test_kv_c(t, "op", ops[op]);
    script_t script = { .test = it };
    spn_git_remote_t remote = { .transport = it->transport, .clone = try_clone, .fetch = try_fetch, .tip = try_tip, .user = &script };
    spn_net_backoff_t backoff = { .wait_ms = { 1, 2, 3 }, .wait = record_wait, .user = &script };
    spn_git_failure_t failure = sp_zero;
    sp_atomic_s32_t cancelled = sp_zero;
    sp_atomic_s32_store(&cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);

    spn_err_t err = SPN_OK;
    switch ((op_t)op) {
      case OP_CLONE: {
        err = spn_git_clone(remote, url, sp_str_lit("B"), backoff, &cancelled, &failure);
        break;
      }
      case OP_FETCH: {
        err = spn_git_fetch(remote, sp_str_lit("B"), url, backoff, &cancelled, &failure);
        break;
      }
      case OP_TIP: {
        sp_str_t rev = sp_zero;
        err = si_git_tip(remote, sp_test_arena(t), url, backoff, &cancelled, &rev, &failure);
        break;
      }
    }
    spn_event_buffer_drain(sp_test_arena(t), spn.events);

    sp_expect_eq(t, (u32)err, (u32)it->expect.err);
    sp_expect_eq(t, (u32)failure.kind, (u32)it->expect.kind);
    sp_expect_eq(t, (u32)failure.route, (u32)it->expect.route);
    sp_expect_str_eq_c(t, sp_cstr_as_str(script.routes), it->expect.routes);
    sp_for(at, spn_net_max_retries) {
      sp_expect_eq(t, script.waits[at], it->expect.waits[at]);
    }
  }
  return SP_OK;
}
