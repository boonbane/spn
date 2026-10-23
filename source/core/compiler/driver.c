#include "compiler/driver.h"
#include "compiler/exports.h"
#include "compiler/push.h"
#include "compiler/rsp.h"
#include "compiler/types.h"

#include "paths/paths.h"
#include "profile/types.h"
#include "toolchain/linker.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

void spn_cc_push(sp_mem_t mem, spn_invocation_t* invocation, spn_arg_t arg) {
  if (!invocation->args) sp_da_init(mem, invocation->args);
  if (!spn_arg_empty(arg)) {
    sp_da_push(invocation->args, arg);
  }
}

void spn_cc_push_c(sp_mem_t mem, spn_invocation_t* invocation, const c8* value) {
  spn_cc_push(mem, invocation, spn_arg_lit(sp_cstr_as_str(value)));
}

void spn_cc_push_str(sp_mem_t mem, spn_invocation_t* invocation, sp_str_t value) {
  spn_cc_push(mem, invocation, spn_arg_lit(value));
}

void spn_cc_push_fmt(sp_mem_t mem, spn_invocation_t* invocation, const c8* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  sp_str_r str = sp_fmt_mem_v(mem, sp_cstr_as_str(fmt), args);
  va_end(args);

  spn_cc_push(mem, invocation, spn_arg_lit(str.value));
}

void spn_cc_push_path(sp_mem_t mem, spn_invocation_t* invocation, spn_path_t path) {
  spn_cc_push(mem, invocation, spn_arg_path(path));
}

void spn_cc_push_glued(sp_mem_t mem, spn_invocation_t* invocation, const c8* prefix, spn_path_t path) {
  spn_cc_push(mem, invocation, spn_arg_glue(sp_cstr_as_str(prefix), path));
}

void spn_cc_push_strs(sp_mem_t mem, spn_invocation_t* invocation, sp_da(sp_str_t) values) {
  sp_da_for(values, it) {
    spn_cc_push(mem, invocation, spn_arg_lit(values[it]));
  }
}

void spn_cc_push_paths(sp_mem_t mem, spn_invocation_t* invocation, sp_da(spn_path_t) paths) {
  sp_da_for(paths, it) {
    spn_cc_push(mem, invocation, spn_arg_path(paths[it]));
  }
}

void spn_cc_push_args(sp_mem_t mem, spn_invocation_t* invocation, sp_da(spn_arg_t) args) {
  sp_da_for(args, it) {
    spn_cc_push(mem, invocation, args[it]);
  }
}

void spn_cc_push_env(sp_mem_t mem, spn_invocation_t* invocation, spn_env_key_t key, spn_arg_t value) {
  spn_invocation_env_t env = { .key = key, .values = sp_da_new(mem, spn_arg_t) };
  sp_da_push(env.values, value);
  if (!invocation->env) sp_da_init(mem, invocation->env);
  sp_da_push(invocation->env, env);
}

void spn_cc_push_env_paths(sp_mem_t mem, spn_invocation_t* invocation, spn_env_key_t key, const spn_path_t* paths, u32 count) {
  spn_invocation_env_t env = { .key = key, .values = sp_da_new(mem, spn_arg_t) };
  sp_for(it, count) {
    sp_da_push(env.values, spn_arg_path(paths[it]));
  }
  if (!invocation->env) sp_da_init(mem, invocation->env);
  sp_da_push(invocation->env, env);
}

bool spn_cc_has(const spn_cc_t* toolchain, spn_cc_cap_t cap) {
  return (spn_toolchain_driver_caps(toolchain->driver) & cap) == (spn_cc_cap_set_t)cap;
}

spn_err_t spn_cc_parse_depfile(sp_mem_t mem, spn_cc_driver_t driver, sp_str_t content, sp_da(sp_str_t)* prereqs) {
  sp_da_init(mem, *prereqs);
  switch (driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      return spn_gnu_parse_depfile(mem, content, prereqs);
    }
    case SPN_CC_DRIVER_MSVC: {
      return spn_msvc_parse_depfile(mem, content, prereqs);
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(SPN_ERROR);
}

static spn_cc_feature_t link_feature(spn_cc_output_kind_t kind) {
  switch (kind) {
    case SPN_CC_OUTPUT_EXE: return SPN_CC_FEATURE_LINK_EXE;
    case SPN_CC_OUTPUT_SHARED_LIB: return SPN_CC_FEATURE_LINK_SHARED;
    case SPN_CC_OUTPUT_REACTOR: return SPN_CC_FEATURE_LINK_REACTOR;
    case SPN_CC_OUTPUT_OBJECT:
    case SPN_CC_OUTPUT_STATIC_LIB: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(SPN_CC_FEATURE_LINK_EXE);
}

spn_cc_compile_plan_t si_cc_render_compile(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, spn_lang_t lang, const spn_cc_compile_t* compile) {
  switch (toolchain->driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      return spn_gnu_render_compile(mem, toolchain, profile, lang, compile);
    }
    case SPN_CC_DRIVER_MSVC: {
      return spn_msvc_render_compile(mem, toolchain, profile, lang, compile);
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(sp_zero_s(spn_cc_compile_plan_t));
}

static spn_invocation_t invocation_from_base(sp_mem_t mem, const spn_invocation_t* base) {
  sp_assert(!spn_arg_empty(base->program));

  spn_invocation_t invocation = {
    .program = base->program,
  };
  sp_da_init(mem, invocation.args);
  sp_da_reserve(invocation.args, sp_da_size(base->args) + 1);
  sp_da_for(base->args, it) {
    sp_da_push(invocation.args, base->args[it]);
  }
  sp_da_init(mem, invocation.env);
  sp_da_for(base->env, it) {
    sp_da_push(invocation.env, base->env[it]);
  }
  return invocation;
}

spn_invocation_t spn_cc_render_compile_source(sp_mem_t mem, const spn_invocation_t* base, spn_path_t source) {
  sp_assert(!spn_path_empty(source));
  spn_invocation_t invocation = invocation_from_base(mem, base);
  spn_cc_push_path(mem, &invocation, source);
  return invocation;
}

spn_invocation_t spn_cc_render_compile_command(sp_mem_t mem, const spn_cc_compile_plan_t* plan, const spn_cc_compile_files_t* files) {
  sp_assert(!spn_path_empty(files->source));
  sp_assert(!spn_path_empty(files->output));

  spn_invocation_t invocation = invocation_from_base(mem, &plan->base);
  invocation.cwd = files->cwd;
  switch (plan->driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      spn_gnu_render_compile_locations(mem, plan, files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_MSVC: {
      spn_msvc_render_compile_locations(mem, plan, files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  return invocation;
}

spn_cc_archive_plan_t si_cc_render_archive(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile) {
  switch (toolchain->archiver_driver) {
    case SPN_AR_DRIVER_GNU: {
      return spn_gnu_render_archive(mem, toolchain);
    }
    case SPN_AR_DRIVER_MSVC: {
      return spn_msvc_render_archive(mem, toolchain, profile);
    }
  }
  SP_UNREACHABLE_RETURN(sp_zero_s(spn_cc_archive_plan_t));
}

spn_invocation_t spn_cc_render_archive_command(sp_mem_t mem, const spn_cc_archive_plan_t* plan, const spn_cc_archive_files_t* files) {
  sp_assert(!spn_path_empty(files->output));

  spn_invocation_t invocation = invocation_from_base(mem, &plan->base);
  invocation.cwd = files->cwd;
  switch (plan->driver) {
    case SPN_AR_DRIVER_GNU: {
      spn_gnu_render_archive_locations(mem, plan, files, &invocation);
      break;
    }
    case SPN_AR_DRIVER_MSVC: {
      spn_msvc_render_archive_locations(mem, plan, files, &invocation);
      break;
    }
  }
  return invocation;
}

spn_cc_link_plan_t si_cc_render_link(sp_mem_t mem, const spn_cc_t* toolchain, const spn_profile_info_t* profile, const spn_cc_link_t* link) {
  switch (toolchain->driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      return spn_gnu_render_link(mem, toolchain, profile, link);
    }
    case SPN_CC_DRIVER_MSVC: {
      return spn_msvc_render_link(mem, toolchain, profile, link);
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(sp_zero_s(spn_cc_link_plan_t));
}

spn_invocation_t spn_cc_render_link_command(sp_mem_t mem, const spn_cc_link_plan_t* plan, const spn_cc_link_files_t* files) {
  sp_assert(!spn_path_empty(files->output));

  spn_invocation_t invocation = invocation_from_base(mem, &plan->base);
  invocation.cwd = files->cwd;
  switch (plan->driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      spn_gnu_render_link_locations(mem, plan, files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_MSVC: {
      spn_msvc_render_link_locations(mem, plan, files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  return invocation;
}

spn_cc_link_refusal_t spn_cc_validate_link(const spn_cc_t* toolchain, spn_triple_t host, const spn_profile_info_t* profile, const spn_cc_link_t* link) {
  spn_cc_feature_t feature = link_feature(link->kind);
  spn_triple_t target = spn_profile_triple(profile);

  if (link->kind == SPN_CC_OUTPUT_REACTOR && profile->os != SPN_OS_WASI) {
    return (spn_cc_link_refusal_t) { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = feature };
  }
  if (link->kind == SPN_CC_OUTPUT_SHARED_LIB && !spn_triple_dynamic(target)) {
    return (spn_cc_link_refusal_t) { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = feature };
  }
  if (profile->os == SPN_OS_MACOS && !si_da_empty(link->frameworks) && profile->sdk.kind == SPN_SDK_NONE) {
    return (spn_cc_link_refusal_t) { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_FRAMEWORKS };
  }
  if (!si_da_empty(link->scripts) && !spn_ld_scripts(profile->linker, spn_os_to_native_object_format(profile->os))) {
    return (spn_cc_link_refusal_t) { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_LINKER_SCRIPT };
  }
  if (profile->linker == SPN_LD_FAMILY_MSVC && host.os != SPN_OS_WINDOWS) {
    return (spn_cc_link_refusal_t) { .err = SPN_ERR_TOOLCHAIN_MSVC_LINKER_HOST };
  }
  return (spn_cc_link_refusal_t) { .err = SPN_OK };
}

spn_cc_exports_format_t spn_cc_exports_format(spn_cc_output_kind_t kind, spn_obj_format_t format) {
  switch (kind) {
    case SPN_CC_OUTPUT_REACTOR: {
      return SPN_CC_EXPORTS_WASM;
    }
    case SPN_CC_OUTPUT_SHARED_LIB: {
      switch (format) {
        case SPN_OBJ_MACHO: return SPN_CC_EXPORTS_SYMBOL_LIST;
        case SPN_OBJ_COFF: return SPN_CC_EXPORTS_DEF;
        case SPN_OBJ_ELF: return SPN_CC_EXPORTS_VERSION_SCRIPT;
        case SPN_OBJ_WASM: sp_unreachable_case();
      }
      SP_UNREACHABLE_RETURN(SPN_CC_EXPORTS_VERSION_SCRIPT);
    }
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_OBJECT:
    case SPN_CC_OUTPUT_STATIC_LIB: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(SPN_CC_EXPORTS_VERSION_SCRIPT);
}

sp_str_t spn_cc_exports_extension(spn_cc_exports_format_t format) {
  switch (format) {
    case SPN_CC_EXPORTS_VERSION_SCRIPT: return sp_str_lit("map");
    case SPN_CC_EXPORTS_SYMBOL_LIST:    return sp_str_lit("exp");
    case SPN_CC_EXPORTS_DEF:            return sp_str_lit("def");
    case SPN_CC_EXPORTS_WASM:           return sp_str_lit("rsp");
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

void spn_cc_render_exports(sp_io_writer_t* io, const spn_path_roots_t* roots, spn_cc_exports_format_t format, spn_rsp_style_t rsp, sp_str_t library, si_da(sp_str_t) symbols) {
  switch (format) {
    case SPN_CC_EXPORTS_VERSION_SCRIPT: {
      spn_exports_render_version_script(io, symbols);
      break;
    }
    case SPN_CC_EXPORTS_SYMBOL_LIST: {
      spn_exports_render_symbol_list(io, symbols);
      break;
    }
    case SPN_CC_EXPORTS_WASM: {
      sp_mem_arena_marker_t s = sp_mem_begin_scratch();
      spn_rsp_render(io, roots, rsp, spn_gnu_render_exports(s.mem, symbols));
      sp_mem_end_scratch(s);
      break;
    }
    case SPN_CC_EXPORTS_DEF: {
      spn_exports_render_def(io, library, symbols);
      break;
    }
  }
}

bool spn_cc_link_implib(const spn_profile_info_t* profile, spn_cc_output_kind_t kind) {
  return kind == SPN_CC_OUTPUT_SHARED_LIB && spn_ld_dialect(spn_profile_triple(profile)) == SPN_LD_DIALECT_LINK;
}
