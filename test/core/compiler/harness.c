#include "compiler.h"

#include "profile/profile.h"
#include "toolchain/linker.h"
#include "toolchain/sdk.h"

spn_path_t test_arg_path(const c8* value) {
  return (spn_path_t) { .sub = sp_cstr_as_str(value) };
}

sp_da(sp_str_t) test_strs(sp_mem_t mem, const c8* const* values, u32 max) {
  sp_da(sp_str_t) strs = sp_da_new(mem, sp_str_t);
  sp_for(it, max) {
    if (!values[it]) {
      break;
    }
    sp_da_push(strs, sp_cstr_as_str(values[it]));
  }
  return strs;
}

void test_push_str(sp_mem_t mem, si_da(sp_str_t)* strs, const c8* value) {
  if (value) {
    si_da_push(mem, *strs, sp_cstr_as_str(value));
  }
}

void test_push_path(sp_mem_t mem, si_da(spn_path_t)* paths, const c8* value) {
  if (value) {
    si_da_push(mem, *paths, test_arg_path(value));
  }
}

spn_cc_t test_toolchain(spn_cc_driver_t driver) {
  return (spn_cc_t) {
    .driver = driver,
    .compiler = { .program = spn_arg_lit(sp_str_lit("cc")) },
    .cxx = { .program = spn_arg_lit(sp_str_lit("c++")) },
    .archiver = { .program = spn_arg_lit(sp_str_lit("ar")) },
    .archiver_driver = driver == SPN_CC_DRIVER_MSVC ? SPN_AR_DRIVER_MSVC : SPN_AR_DRIVER_GNU,
  };
}

sp_err_t test_profile(sp_test_t* t, spn_cc_driver_t driver, test_profile_t desc, spn_profile_info_t* profile) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_info_t toolchain = { .driver = driver };
  spn_toolchain_selection_t selection = {
    .toolchain = &toolchain,
    .row = {
      .triple = desc.target,
      .spelling = sp_cstr_as_str(desc.spelling),
      .exceptions = {
        .flags = test_strs(mem, desc.exceptions.flags, sp_carr_len(desc.exceptions.flags)),
        .link_flags = test_strs(mem, desc.exceptions.link_flags, sp_carr_len(desc.exceptions.link_flags)),
      },
    },
  };
  spn_linkings_t linkings = sp_zero;
  sp_must_eq(t, (u32)si_ld_linkings(desc.target, desc.request, SPN_LIB_KIND_NONE, &linkings), (u32)SPN_LINKING_REFUSAL_NONE);
  selection.linking = linkings.items[0];
  if (desc.sdk) {
    selection.sdk = spn_sdk_at(mem, desc.target, test_arg_path(desc.sdk));
  }
  if (desc.msvc.bin || desc.msvc.tools || desc.msvc.atl) {
    sp_must_eq(t, (u32)selection.sdk.kind, (u32)SPN_SDK_MSVC);
    spn_sdk_msvc_t* msvc = &selection.sdk.msvc;
    msvc->bin = test_arg_path(desc.msvc.bin);
    msvc->tools.sdk = test_arg_path(desc.msvc.tools);
    msvc->lib.atlmfc = test_arg_path(desc.msvc.atl);
  }

  *profile = (spn_profile_info_t) {
    .target = desc.target,
    .standard = desc.standard,
    .mode = desc.mode,
    .opt = desc.opt,
    .strip = desc.strip,
    .sanitizers = desc.sanitizers,
  };
  si_profile_finalize(profile, &selection);
  return SP_OK;
}

sp_err_t expect_invocation(sp_test_t* t, const spn_invocation_t* invocation, render_expect_t expect) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  sp_expect_str_eq_c(t, spn_arg_str(&roots, mem, invocation->program), expect.command);
  sp_da(sp_str_t) args = spn_invocation_args(&roots, mem, invocation);
  sp_must_strs_eq(t, args, sp_da_size(args), expect.args);
  sp_da(sp_str_t) env = sp_da_new(mem, sp_str_t);
  sp_da_for(invocation->env, it) {
    sp_env_var_t var = spn_invocation_env_var(&roots, mem, invocation->env[it]);
    sp_da_push(env, sp_fmt(mem, "{}={}", sp_fmt_str(var.key), sp_fmt_str(var.value)).value);
  }
  sp_must_strs_eq(t, env, sp_da_size(env), expect.env);
  sp_da(sp_str_t) path = sp_da_new(mem, sp_str_t);
  sp_for(it, invocation->path.count) {
    sp_da_push(path, spn_path_str(&roots, mem, invocation->path.items[it]));
  }
  sp_must_strs_eq(t, path, sp_da_size(path), expect.path);
  sp_expect_eq(t, invocation->echo.some, expect.echo != SP_NULLPTR);
  sp_expect_str_eq_c(t, invocation->echo.value, expect.echo);
  return SP_OK;
}
