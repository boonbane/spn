#include "compiler.h"

#include "intern/intern.h"

spn_path_t test_arg_path(const c8* value) {
  return (spn_path_t) { .sub = sp_cstr_as_str(value) };
}

sp_err_t expect_args(sp_test_t* t, spn_invocation_t* invocation, render_expect_t expect) {
  sp_mem_t mem = sp_test_arena(t);
  const spn_path_roots_t* roots = &spn.roots;
  sp_da(sp_str_t) args = spn_invocation_args(roots, mem, invocation);
  sp_expect_str_eq_c(t, spn_arg_str(roots, mem, invocation->program), expect.command);
  sp_must_strs_eq(t, args, sp_da_size(args), expect.args);
  sp_da(sp_str_t) env = sp_da_new(mem, sp_str_t);
  sp_da_for(invocation->env, it) {
    sp_env_var_t var = spn_invocation_env_var(roots, mem, invocation->env[it]);
    sp_da_push(env, sp_fmt(mem, "{}={}", sp_fmt_str(var.key), sp_fmt_str(var.value)).value);
  }
  sp_must_strs_eq(t, env, sp_da_size(env), expect.env);
  return SP_OK;
}

spn_profile_info_t test_profile(test_profile_t desc) {
  spn_profile_info_t profile = {
    .target = desc.target,
    .linking = desc.linking,
    .standard = desc.standard,
    .mode = desc.mode,
    .opt = desc.opt,
    .strip = desc.strip,
    .sanitizers = desc.sanitizers,
    .spelling = sp_cstr_as_str(desc.spelling),
    .exceptions = {
      .flags = sp_da_new(spn.mem, sp_str_t),
      .link_flags = sp_da_new(spn.mem, sp_str_t),
    },
  };
  sp_carr_for(desc.exceptions.flags, it) {
    if (desc.exceptions.flags[it]) {
      sp_da_push(profile.exceptions.flags, sp_cstr_as_str(desc.exceptions.flags[it]));
    }
  }
  sp_carr_for(desc.exceptions.link_flags, it) {
    if (desc.exceptions.link_flags[it]) {
      sp_da_push(profile.exceptions.link_flags, sp_cstr_as_str(desc.exceptions.link_flags[it]));
    }
  }
  if (desc.sdk) {
    profile.sdk = spn_sdk_at(spn.mem, desc.target, test_arg_path(desc.sdk));
  }
  if (desc.bin) {
    sp_assert(profile.sdk.kind == SPN_SDK_MSVC);
    profile.sdk.msvc.bin = test_arg_path(desc.bin);
  }
  if (desc.tools) {
    sp_assert(profile.sdk.kind == SPN_SDK_MSVC);
    profile.sdk.msvc.tools.sdk = test_arg_path(desc.tools);
  }
  if (desc.atl) {
    sp_assert(profile.sdk.kind == SPN_SDK_MSVC);
    profile.sdk.msvc.lib.atlmfc = test_arg_path(desc.atl);
  }
  return profile;
}

spn_cc_t test_toolchain(spn_cc_driver_t driver) {
  return (spn_cc_t) {
    .name = spn_intern(sp_str_lit("test")),
    .driver = driver,
    .compiler = { .program = spn_arg_lit(sp_str_lit("cc")) },
    .cxx = { .program = spn_arg_lit(sp_str_lit("c++")) },
    .archiver = { .program = spn_arg_lit(sp_str_lit("ar")) },
    .archiver_driver = driver == SPN_CC_DRIVER_MSVC ? SPN_AR_DRIVER_MSVC : SPN_AR_DRIVER_GNU,
  };
}
