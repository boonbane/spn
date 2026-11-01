#ifndef SPN_TEST_HARNESS_H
#define SPN_TEST_HARNESS_H

#include "sp.h"
#include "sp/sp_test.h"
#include "fixture.h"
#include "caps.h"
#include "env.h"
#include "event/types.h"

#define SPN_TEST_MAX_ACTIONS 32
#define SPN_TEST_MAX_COPY 16
#define SPN_TEST_MAX_PATHS 8
#define SPN_TEST_MAX_EVENTS 4
#define SPN_TEST_MAX_FILES 4
#define SPN_TEST_MAX_CC 4
#define SPN_TEST_MAX_PACKAGES 4
#define SPN_TEST_MAX_REBUILDS 3
#define SPN_TEST_MAX_CHANGES 4
#define SPN_TEST_MAX_WATCHES 4
#define SPN_TEST_MAX_BUILDS 3
#define SPN_TEST_MAX_CONTAINS 4
#define SPN_TEST_MAX_WITH 4

typedef enum {
  PATH_NONE,
  PATH_LITERAL,
  PATH_EXE,
  PATH_TEST_EXE,
  PATH_EXAMPLE_EXE,
  PATH_STAGED_LIB,
  PATH_TEST_LIB,
  PATH_STORE,
  PATH_STATIC_LIB,
  PATH_SHARED_LIB,
  PATH_WORK,
} path_kind_t;

typedef struct {
  path_kind_t kind;
  const c8* name;
  const c8* pkg;
  const c8* profile;
  const c8* triple;
  const c8* dir;
  bool exe;
} path_t;

#define lit(path)                                     { PATH_LITERAL, .name = path }
#define exe(name_)                                    { PATH_EXE, .name = name_ }
#define profile_exe(profile_, name_)                  { PATH_EXE, .name = name_, .profile = profile_ }
#define target_exe(name_, triple_)                    { PATH_EXE, .name = name_, .triple = triple_ }
#define test_exe(name_)                               { PATH_TEST_EXE, .name = name_ }
#define example_exe(name_)                            { PATH_EXAMPLE_EXE, .name = name_ }
#define staged_lib(name_)                             { PATH_STAGED_LIB, .name = name_ }
#define test_lib(name_)                               { PATH_TEST_LIB, .name = name_ }
#define pkg_store_file(pkg_, rest)                    { PATH_STORE, .pkg = pkg_, .name = rest }
#define pkg_store_exe(pkg_, name_)                    { PATH_STORE, .pkg = pkg_, .name = name_, .exe = true }
#define pkg_static_lib(pkg_, name_)                   { PATH_STATIC_LIB, .pkg = pkg_, .name = name_ }
#define pkg_profile_static_lib(profile_, pkg_, name_) { PATH_STATIC_LIB, .pkg = pkg_, .name = name_, .profile = profile_ }
#define pkg_shared_lib(pkg_, name_)                   { PATH_SHARED_LIB, .pkg = pkg_, .name = name_ }
#define work_file(rest)                               { PATH_WORK, .name = rest }

typedef enum {
  BARE_EXPECT_RUNS,
  BARE_EXPECT_NOT_LOADABLE,
} bare_expect_t;

typedef enum {
  ACTION_NONE,
  ACTION_CREATE_FILE,
  ACTION_REMOVE_DIR,
  ACTION_CLEAR_WARM,
  ACTION_RUN_CLI,
  ACTION_RUN_BIN,
  ACTION_STAGE_BARE_RUN,
  ACTION_VERIFY_EXISTS,
  ACTION_VERIFY_NOT_EXISTS,
  ACTION_VERIFY_NO_INTERP,
  ACTION_VERIFY_ELF_ENTRY,
  ACTION_VERIFY_ELF_NO_SYMBOL,
  ACTION_VERIFY_FILE_CONTAINS,
  ACTION_VERIFY_FILE_NOT_CONTAINS,
  ACTION_VERIFY_CC_ARG,
  ACTION_VERIFY_NO_CC_ARG,
  ACTION_VERIFY_PKG_LOCKED,
  ACTION_VERIFY_STORE,
  ACTION_VERIFY_EVENT,
  ACTION_VERIFY_NO_EVENT,
  ACTION_VERIFY_RESULT,
} action_kind_t;

typedef struct {
  action_kind_t kind;

  union {
    struct { const c8* file; const c8* content; const c8* from; } create;
    struct { const c8* dir; } rm;
    struct { const c8* cmd; const c8* args [8]; const c8* env [4]; const c8* path; const c8* cwd; s32 rc; } cli;
    struct { path_t file; s32 rc; } bin;
    struct { path_t file; path_t with [SPN_TEST_MAX_WITH]; bare_expect_t expect; } bare;
    path_t exists;
    path_t verify_no_interp;
    struct { path_t file; u64 entry; } verify_elf_entry;
    struct { path_t file; const c8* prefix; } verify_elf_no_symbol;
    struct { const c8* file; const c8* needle; } verify_file_contains;
    const c8* verify_cc_arg [4];
    struct { const c8* name; const c8* version; } verify_locked;
    struct { const c8* pkg; u32 count; } verify_store;
    struct { spn_event_kind_t event; const c8* key; const c8* value; bool lane; } verify_event;
    struct { spn_err_t err; } verify_result;
  };
} action_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [SPN_TEST_MAX_COPY];
  const c8* config;
  const c8* toolchain;
  test_when_t when;
  action_t actions [SPN_TEST_MAX_ACTIONS];
} test_t;

typedef struct {
  spn_event_kind_t event;
  const c8* key;
  const c8* value;
  bool lane;
  bool absent;
} command_event_t;

typedef struct {
  path_t file;
  const c8* content;
  bool json;
} command_file_t;

typedef struct {
  path_t file;
  const c8* args [SPN_TEST_MAX_CC];
  const c8* include;
  bool absent;
} command_cc_t;

typedef struct {
  const c8* name;
  const c8* version;
} command_lock_t;

typedef struct {
  s32 rc;
  spn_err_t err;
  const c8* contains [SPN_TEST_MAX_CONTAINS];
  const c8* excludes [SPN_TEST_MAX_CONTAINS];
  command_event_t events [SPN_TEST_MAX_EVENTS];
  command_file_t files [SPN_TEST_MAX_FILES];
  command_cc_t cc [SPN_TEST_MAX_CC];
  path_t exists [SPN_TEST_MAX_PATHS];
  path_t missing [SPN_TEST_MAX_PATHS];
  command_lock_t locked [SPN_TEST_MAX_PACKAGES];
} command_expect_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [SPN_TEST_MAX_COPY];
  test_when_t when;
  const c8* args [SPN_TEST_COMMAND_MAX_ARGS];
  const c8* env [SPN_TEST_COMMAND_MAX_ENV];
  bool human;
  command_expect_t expect;
} command_test_t;

typedef enum {
  REBUILD_MTIME_NONE,
  REBUILD_MTIME_CHANGED,
} rebuild_mtime_t;

typedef struct {
  const c8* file;
  const c8* content;
} rebuild_write_t;

typedef struct {
  const c8* from;
  const c8* to;
} rebuild_move_t;

typedef struct {
  path_t remove_files [SPN_TEST_MAX_CHANGES];
  rebuild_move_t moves [SPN_TEST_MAX_CHANGES];
  rebuild_write_t writes [SPN_TEST_MAX_CHANGES];
  const c8* remove_dirs [SPN_TEST_MAX_CHANGES];
} rebuild_change_t;

typedef struct {
  rebuild_change_t change;
  command_test_t command;
} rebuild_step_t;

typedef struct {
  path_t file;
  rebuild_mtime_t mtime;
} rebuild_watch_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [SPN_TEST_MAX_COPY];
  const c8* toolchain;
  test_when_t when;
  command_test_t first;
  rebuild_step_t rebuilds [SPN_TEST_MAX_REBUILDS];
  rebuild_watch_t watches [SPN_TEST_MAX_WATCHES];
} rebuild_test_t;

typedef struct {
  const c8* profile;
  const c8* manifest;
  const c8* target;
  bool alternate;
  bool present;
  test_when_t when;
  command_expect_t expect;
} opt_build_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [4];
  test_when_t when;
  opt_build_t builds [SPN_TEST_MAX_BUILDS];
} opt_test_t;

typedef struct {
  const c8* pkg;
  u32 count;
} identity_count_t;

typedef struct {
  command_lock_t locked [SPN_TEST_MAX_PACKAGES];
  identity_count_t store [SPN_TEST_MAX_PACKAGES];
  identity_count_t configured [SPN_TEST_MAX_PACKAGES];
} identity_expect_t;

typedef struct {
  const c8* name;
  const c8* project;
  const c8* copy [SPN_TEST_MAX_COPY];
  test_when_t when;
  const c8* builds [SPN_TEST_MAX_BUILDS][SPN_TEST_COMMAND_MAX_ARGS];
  identity_expect_t expect;
} identity_test_t;

sp_str_t path_render(fixture_t* fixture, path_t path);
sp_err_t expect_exists(sp_test_t* t, fixture_t* fixture, sp_str_t path, bool expected, const c8* file, u32 line);

sp_err_t test_when(sp_test_t* t, test_when_t when);
sp_err_t run_command(sp_test_t* t, fixture_t* fixture, command_test_t test);
sp_err_t run_command_test(sp_test_t* t, command_test_t test);
sp_err_t run_rebuild_test(sp_test_t* t, rebuild_test_t test);
sp_err_t run_actions(sp_test_t* t, fixture_t* fixture, const action_t* actions);
sp_err_t run_test(sp_test_t* t, test_t test);
sp_err_t run_opt_test(sp_test_t* t, opt_test_t test);
sp_err_t run_identity_test(sp_test_t* t, identity_test_t test);

#endif
