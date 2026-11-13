#include "spn_test.h"

#include "external/http.h"

#define http_test_max_connections 2
#define http_test_request_max 4096
#define http_test_quiet_ms 200
#define http_test_hold_ms 20000

#define reply_a "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: close\r\n\r\nA"
#define reply_ab_cut "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nA"
#define reply_redirect "HTTP/1.1 302 Found\r\nLocation: /B\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
#define reply_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"

typedef enum {
  SERVE_NONE,
  SERVE_REPLY,
  SERVE_HOLD,
  SERVE_RESET,
  SERVE_CANCEL,
} serve_t;

typedef struct {
  serve_t kind;
  const c8* reply;
} connection_t;

typedef struct {
  spn_err_t err;
  spn_http_failure_kind_t kind;
  u32 connections;
  const c8* body;
} expect_t;

typedef struct {
  const c8* name;
  const c8* scheme;
  const c8* dest;
  spn_net_trust_kind_t trust;
  connection_t connections [http_test_max_connections];
  expect_t expect;
} test_t;

typedef struct {
  sp_sys_socket_t listener;
  const connection_t* connections;
  sp_atomic_s32_t* cancelled;
  sp_atomic_s32_t stop;
  u32 accepted;
  u32 expired;
} server_t;

static const test_t tests [] = {
  {
    .name = "downloads_body",
    .connections = { { .kind = SERVE_REPLY, .reply = reply_a } },
    .expect = { .connections = 1, .body = "A" },
  },
  {
    .name = "follows_redirect",
    .connections = { { .kind = SERVE_REPLY, .reply = reply_redirect }, { .kind = SERVE_REPLY, .reply = reply_a } },
    .expect = { .connections = 2, .body = "A" },
  },
  {
    .name = "http_error_fails",
    .connections = { { .kind = SERVE_REPLY, .reply = reply_404 } },
    .expect = { .err = SPN_ERROR, .connections = 1 },
  },
  {
    .name = "cut_body_is_dropped",
    .connections = { { .kind = SERVE_REPLY, .reply = reply_ab_cut } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "empty_reply_is_dropped",
    .connections = { { .kind = SERVE_REPLY, .reply = "" } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "reset_is_dropped",
    .connections = { { .kind = SERVE_RESET } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "stalled_body_is_dropped",
    .connections = { { .kind = SERVE_HOLD, .reply = reply_ab_cut } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "unanswered_tls_handshake_is_dropped",
    .scheme = "https",
    .trust = SPN_NET_TRUST_SYSTEM,
    .connections = { { .kind = SERVE_HOLD, .reply = "" } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "reset_tls_handshake_is_dropped",
    .scheme = "https",
    .trust = SPN_NET_TRUST_SYSTEM,
    .connections = { { .kind = SERVE_RESET } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_DROPPED, .connections = 1 },
  },
  {
    .name = "cancel_aborts_transfer",
    .connections = { { .kind = SERVE_CANCEL } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_CANCELLED, .connections = 1 },
  },
  {
    .name = "missing_directory_is_unwritable",
    .dest = "B/A",
    .connections = { { .kind = SERVE_REPLY, .reply = reply_a } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_UNWRITABLE },
  },
  {
    .name = "https_without_ca_fails_before_connecting",
    .scheme = "https",
    .connections = { { .kind = SERVE_HOLD, .reply = "" } },
    .expect = { .err = SPN_ERROR, .kind = SPN_HTTP_NO_CA },
  },
};

static void read_request(sp_sys_socket_t client) {
  c8 request [http_test_request_max] = sp_zero;
  u64 len = 0;
  while (len < sizeof(request) && !sp_str_contains(sp_str(request, len), sp_str_lit("\r\n\r\n"))) {
    u64 received = 0;
    if (sp_sys_socket_wait(client, true, http_test_quiet_ms) || sp_sys_socket_recv(client, request + len, sizeof(request) - len, &received) || !received) {
      break;
    }
    len += received;
  }
}

static s32 serve(void* user) {
  server_t* server = (server_t*)user;
  while (true) {
    sp_sys_socket_t client = SP_SYS_INVALID_SOCKET;
    if (sp_sys_socket_accept(server->listener, (sp_sys_handle_desc_t) { .mode = SP_SYS_BLOCKING }, &client)) {
      break;
    }
    if (sp_atomic_s32_load(&server->stop, SP_ATOMIC_SEQ_CST)) {
      sp_sys_socket_close(client);
      break;
    }

    connection_t connection = server->accepted < http_test_max_connections ? server->connections[server->accepted] : sp_zero_s(connection_t);
    server->accepted++;
    switch (connection.kind) {
      case SERVE_REPLY: {
        read_request(client);
        sp_sys_socket_send(client, connection.reply, sp_cstr_len(connection.reply), SP_NULLPTR);
        break;
      }
      case SERVE_CANCEL:
      case SERVE_HOLD: {
        if (connection.kind == SERVE_CANCEL) {
          sp_atomic_s32_store(server->cancelled, 1, SP_ATOMIC_SEQ_CST);
        }
        else {
          read_request(client);
          sp_sys_socket_send(client, connection.reply, sp_cstr_len(connection.reply), SP_NULLPTR);
        }
        u8 byte = 0;
        u64 received = 1;
        while (received) {
          if (sp_sys_socket_wait(client, true, http_test_hold_ms)) {
            server->expired++;
            break;
          }
          if (sp_sys_socket_recv(client, &byte, 1, &received)) {
            break;
          }
        }
        break;
      }
      case SERVE_RESET: {
        sp_sys_socket_wait(client, true, http_test_hold_ms);
        break;
      }
      case SERVE_NONE: {
        break;
      }
    }
    sp_sys_socket_close(client);
  }
  return 0;
}

sp_test_each(http, download, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_sys_ipv4_t local = { .octets = { 127, 0, 0, 1 } };
  sp_atomic_s32_t cancelled = sp_zero;
  server_t server = { .listener = SP_SYS_INVALID_SOCKET, .connections = it->connections, .cancelled = &cancelled };
  sp_must_ok(t, sp_sys_socket_open(&server.listener, (sp_sys_handle_desc_t) { .mode = SP_SYS_BLOCKING }));
  sp_must_ok(t, sp_sys_socket_bind(server.listener, local));
  sp_must_ok(t, sp_sys_socket_listen(server.listener, http_test_max_connections));
  sp_must_ok(t, sp_sys_socket_local_port(server.listener, &local.port));

  sp_thread_t thread = sp_zero;
  sp_thread_init(&thread, serve, &server);

  spn_http_config_t config = {
    .trust = { .kind = it->trust },
    .timeout_s = 1,
    .cancelled = &cancelled,
  };
  sp_str_t url = sp_fmt(mem, "{}://127.0.0.1:{}/A", sp_fmt_cstr(it->scheme ? it->scheme : "http"), sp_fmt_uint(local.port)).value;
  sp_path_t dest = sp_path_join(mem, sp_test_dir(t), sp_cstr_as_str(it->dest ? it->dest : "A"));
  spn_http_failure_t failure = sp_zero;
  spn_err_t err = spn_http_download(&config, url, dest, &failure);

  sp_atomic_s32_store(&server.stop, 1, SP_ATOMIC_SEQ_CST);
  sp_sys_socket_t poke = SP_SYS_INVALID_SOCKET;
  sp_must_ok(t, sp_sys_socket_open(&poke, (sp_sys_handle_desc_t) { .mode = SP_SYS_BLOCKING }));
  sp_sys_socket_connect(poke, local);
  sp_sys_socket_close(poke);
  sp_thread_join(&thread);
  sp_sys_socket_close(server.listener);

  sp_test_kv_c(t, "message", failure.message);
  sp_expect_eq(t, (u32)err, (u32)it->expect.err);
  sp_expect_eq(t, (u32)failure.kind, (u32)it->expect.kind);
  sp_expect_eq(t, server.accepted, it->expect.connections);
  sp_expect_eq(t, server.expired, 0u);
  if (it->expect.body) {
    sp_str_t body = sp_zero;
    sp_must_ok(t, sp_io_read_file_at(mem, dest, &body));
    sp_expect_str_eq_c(t, body, it->expect.body);
  }
  return SP_OK;
}

sp_test(http, downloads_file_url, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t source = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  sp_path_t dest = sp_path_join(mem, sp_test_dir(t), sp_str_lit("B"));
  sp_must_ok(t, sp_fs_create_file_str(source, sp_str_lit("A")));

  sp_atomic_s32_t cancelled = sp_zero;
  spn_http_config_t config = { .timeout_s = 1, .cancelled = &cancelled };
  spn_http_failure_t failure = sp_zero;
  spn_err_t err = spn_http_download(&config, sp_fmt(mem, "file://{}", sp_fmt_str(source)).value, dest, &failure);

  sp_test_kv_c(t, "message", failure.message);
  sp_must_eq(t, (u32)err, (u32)SPN_OK);
  sp_str_t body = sp_zero;
  sp_must_ok(t, sp_io_read_file_at(mem, dest, &body));
  sp_expect_str_eq_c(t, body, "A");
  return SP_OK;
}
