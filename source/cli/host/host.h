#ifndef SPN_CLI_UTIL_H
#define SPN_CLI_UTIL_H

#include "sp.h"
#include "sp/sp_cli.h"

#include "spn/host.h"

#include "tui/types.h"

#define try(expr) \
  do { \
    sp_cli_result_t __result = (expr); \
    if (__result != SP_CLI_OK) { \
      return __result; \
    } \
  } while (0)

#define spn_cli_target_opts \
  { \
    .name = "target", \
    .kind = SP_CLI_OPT_STR, \
    .summary = "Target triple (e.g. aarch64-linux-gnu)", \
    .placeholder = "TRIPLE", \
    .ptr = &host.args.profile.target, \
  }, \
  { \
    .name = "os", \
    .kind = SP_CLI_OPT_CHOICE, \
    .summary = "Override target OS", \
    .placeholder = "OS", \
    .ptr = &host.args.profile.os, \
    .choices = { \
      { .name = "linux", .value = SPN_OS_LINUX }, \
      { .name = "macos", .value = SPN_OS_MACOS }, \
      { .name = "windows", .value = SPN_OS_WINDOWS }, \
      { .name = "wasi", .value = SPN_OS_WASI }, \
      { .name = "freestanding", .value = SPN_OS_FREESTANDING }, \
    }, \
  }, \
  { \
    .name = "arch", \
    .kind = SP_CLI_OPT_CHOICE, \
    .summary = "Override target architecture", \
    .placeholder = "ARCH", \
    .ptr = &host.args.profile.arch, \
    .choices = { \
      { .name = "x86_64", .value = SPN_ARCH_X64 }, \
      { .name = "aarch64", .value = SPN_ARCH_ARM64 }, \
      { .name = "wasm32", .value = SPN_ARCH_WASM32 }, \
    }, \
  }, \
  { \
    .name = "abi", \
    .kind = SP_CLI_OPT_CHOICE, \
    .summary = "Override target ABI", \
    .placeholder = "ABI", \
    .ptr = &host.args.profile.abi, \
    .choices = { \
      { .name = "gnu", .value = SPN_ABI_GNU }, \
      { .name = "musl", .value = SPN_ABI_MUSL }, \
      { .name = "msvc", .value = SPN_ABI_MSVC }, \
      { .name = "apple", .value = SPN_ABI_APPLE }, \
      { .name = "none", .value = SPN_ABI_BARE }, \
      { .name = "elf", .value = SPN_ABI_ELF }, \
    }, \
  }

typedef struct {
  sp_str_t name;
  sp_str_t toolchain;
  sp_cli_choice_t mode;
  sp_cli_choice_t opt;
  sp_str_t sanitize;
  sp_str_t target;
  sp_cli_choice_t os;
  sp_cli_choice_t arch;
  sp_cli_choice_t abi;
} spn_cli_profile_t;

typedef struct {
  struct {
    sp_str_t project_dir;
    bool json;
    bool no_color;
    bool verbose;
    bool quiet;
    bool version;
    u32 refresh;
    spn_cli_profile_t profile;
  } args;

  spn_ctx_t* ctx;
  spn_session_t* session;
  sp_mem_t mem;
  sp_sys_event_t doorbell;
  sp_atomic_s32_t interrupted;
  s32 code;
} spn_cli_host_t;

extern spn_cli_host_t host;
extern spn_tui_t tui;

void spn_cli_boot();
s32 spn_cli_shutdown(bool ok);

sp_cli_result_t spn_cli_usage(const c8* fmt, ...);
sp_cli_result_t spn_cli_parse_profile(spn_profile_override_t* profile);
spn_str_arr_t spn_cli_rest_names(sp_cli_t* cli);
sp_cli_result_t spn_cli_open(bool project_optional);
sp_cli_result_t spn_cli_session(spn_session_config_t config);
sp_cli_result_t spn_cli_refresh_indexes();
sp_cli_result_t spn_cli_op(spn_op_t* op);
spn_err_t spn_cli_wait(spn_op_t* op);

#endif
