#define SP_IMPLEMENTATION
#include "sp.h"
#include "sp/sp_test.h"

#include "sim/sim.h"

static sp_sim_t sim;
static sp_sys_vtable_t sim_vtable;
static sp_sys_vtable_t sim_base;

static sp_err_t sim_path(c8* buf, u64 size, u64* len, sp_str_t path) {
  if (size < path.len) {
    return SP_ERR_SYS_NAME_TOO_LONG;
  }
  sp_mem_copy(buf, path.data, path.len);
  *len = path.len;
  return SP_OK;
}

static sp_err_t sim_get_fd_path(sp_sys_fd_t fd, c8* buf, u64 size, u64* len) {
  if (fd == sp_fs_get_cwd()) {
    return sim_path(buf, size, len, sp_str_lit("/sim"));
  }
  return sim_base.get_fd_path(fd, buf, size, len);
}

static sp_err_t sim_get_exe_path(c8* buf, u64 size, u64* len) {
  return sim_path(buf, size, len, sp_str_lit("/sim/executable"));
}

s32 main(s32 argc, const c8** argv) {
  sp_sim_init(&sim, sp_mem_os_new());
  sp_sim_install(&sim);
  sim_base = *sp_rt.vt;
  sim_vtable = *sp_rt.vt;
  sim_vtable.get_fd_path = sim_get_fd_path;
  sim_vtable.get_exe_path = sim_get_exe_path;
  sp_sys_set_vtable(&sim_vtable);
  return sp_test_main(argc, argv, SP_NULLPTR);
}
