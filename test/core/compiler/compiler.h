#pragma once

#include "spn_test.h"
#include "triples.h"

#include "compiler/driver.h"
#include "toolchain/toolchain.h"
#include "compiler/exports.h"
#include "compiler/toc.h"
#include "ctx/types.h"
#include "paths/paths.h"
#include "graph/invocation.h"
#include "triple/triple.h"

#define render_args_max 20
#define render_env_max 4
#define render_path_max 4

typedef struct {
  const c8* command;
  const c8* args [render_args_max];
  const c8* env [render_env_max];
  const c8* path [render_path_max];
  const c8* echo;
} render_expect_t;

typedef struct {
  spn_triple_t target;
  spn_linking_t request;
  spn_c_standard_t standard;
  spn_mode_t mode;
  spn_opt_level_t opt;
  spn_strip_t strip;
  spn_sanitizer_set_t sanitizers;
  const c8* sdk;
  const c8* spelling;
  struct {
    const c8* flags [4];
    const c8* link_flags [4];
  } exceptions;
  struct {
    const c8* bin;
    const c8* tools;
    const c8* atl;
  } msvc;
} test_profile_t;

spn_path_t      test_arg_path(const c8* value);
sp_da(sp_str_t) test_strs(sp_mem_t mem, const c8* const* values, u32 max);
void            test_push_str(sp_mem_t mem, si_da(sp_str_t)* strs, const c8* value);
void            test_push_path(sp_mem_t mem, si_da(spn_path_t)* paths, const c8* value);
spn_cc_t        test_toolchain(spn_cc_driver_t driver);
sp_err_t        test_profile(sp_test_t* t, spn_cc_driver_t driver, test_profile_t desc, spn_profile_info_t* profile);
sp_err_t        expect_invocation(sp_test_t* t, const spn_invocation_t* invocation, render_expect_t expect);
