#define SP_IMPLEMENTATION
#include "sp.h"
#include "sp/sp_test.h"

static sp_sys_vtable_t hermetic;

static sp_err_t refuse_socket(sp_sys_socket_t* socket, sp_sys_handle_desc_t desc) {
  sp_fatal("this test binary cannot open sockets; socket tests belong in test/live");
  return SP_ERR_SYS;
}

s32 main(s32 argc, const c8** argv) {
  hermetic = sp_sys_vtable_platform;
  hermetic.socket_open = refuse_socket;
  sp_sys_set_vtable(&hermetic);
  return sp_test_main(argc, argv, SP_NULLPTR);
}
