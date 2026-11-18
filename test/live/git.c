#include "spn_test.h"

#include "external/git.h"

sp_test(git, clone_from_closed_port_is_dropped, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t local = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));

  sp_sys_socket_t probe = SP_SYS_INVALID_SOCKET;
  sp_sys_ipv4_t address = { .octets = { 127, 0, 0, 1 } };
  sp_must_ok(t, sp_sys_socket_open(&probe, (sp_sys_handle_desc_t) { .mode = SP_SYS_BLOCKING }));
  sp_must_ok(t, sp_sys_socket_bind(probe, address));
  sp_must_ok(t, sp_sys_socket_local_port(probe, &address.port));
  sp_must_ok(t, sp_sys_socket_close(probe));
  sp_str_t url = sp_fmt(mem, "git://127.0.0.1:{}/A", sp_fmt_uint(address.port)).value;

  spn_git_failure_t failure = sp_zero;
  sp_atomic_s32_t cancelled = sp_zero;
  spn_err_t err = spn_git_builtin_clone(url, local, &cancelled, &failure);

  sp_test_kv_c(t, "message", failure.message);
  sp_expect_eq(t, (u32)err, (u32)SPN_ERROR);
  sp_expect_eq(t, (u32)failure.kind, (u32)SPN_GIT_DROPPED);
  sp_expect(t, failure.remote);
  return SP_OK;
}
