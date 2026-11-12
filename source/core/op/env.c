#include "spn/host.h"

#include "compiler/driver.h"
#include "ctx/types.h"
#include "model/sync.h"
#include "op/op.h"
#include "paths/paths.h"
#include "session/types.h"
#include "toolchain/sdk.h"
#include "unit/types.h"

static void push_path(spn_env_result_t* env, sp_str_t dir) {
  sp_assert(env->path.count < SPN_ENV_MAX_PATH);
  env->path.items[env->path.count++] = dir;
}

static void push_var(spn_env_result_t* env, const c8* name, sp_str_t value) {
  sp_assert(env->vars.count < SPN_ENV_MAX_VARS);
  env->vars.items[env->vars.count++] = (spn_env_var_t) { .name = sp_cstr_as_str(name), .value = value };
}

static bool sets(const spn_env_result_t* env, sp_str_t name) {
  sp_for(it, env->vars.count) {
    if (sp_str_equal(env->vars.items[it].name, name)) {
      return true;
    }
  }
  return false;
}

static void push_clear(spn_env_result_t* env, sp_str_t name) {
  sp_assert(env->clear.count < SPN_ENV_MAX_CLEAR);
  env->clear.items[env->clear.count++] = name;
}

static sp_str_t search_list(const spn_path_roots_t* roots, sp_mem_t mem, spn_dir_list_t dirs) {
  sp_str_t items [SPN_DIR_LIST_MAX] = sp_zero;
  sp_for(it, dirs.count) {
    items[it] = spn_path_str(roots, mem, dirs.items[it]);
  }
  return sp_str_join_n(mem, items, dirs.count, sp_str_lit(";"));
}

static sp_str_t native_dir(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t path, const c8* sub) {
  return sp_str_replace_c8(mem, sp_fmt(mem, "{}/{}", sp_fmt_str(spn_path_str(roots, mem, path)), sp_fmt_cstr(sub)).value, '/', '\\');
}

spn_err_t spn_op_env(spn_op_t* op) {
  spn_session_t* session = op->session;
  const spn_path_roots_t* roots = &session->ctx->roots;
  sp_mem_t mem = op->mem;
  spn_build_unit_t* build = session->builds.program;
  spn_try(spn_sync_toolchain(&session->ctx->caches.toolchains, build->toolchain));

  spn_env_result_t* env = &op->result.env;
  const spn_sdk_t* sdk = &build->profile.sdk;

  spn_arg_t compiler = spn_cc_launcher(mem, &build->toolchain->cc, &build->profile, SPN_LANG_C).program;
  push_path(env, sp_fs_parent_path(spn_arg_str(roots, mem, compiler)));
  spn_dir_list_t tools = spn_sdk_tool_dirs(sdk);
  sp_for(it, tools.count) {
    push_path(env, spn_path_str(roots, mem, tools.items[it]));
  }

  switch (sdk->kind) {
    case SPN_SDK_MSVC: {
      const spn_sdk_msvc_t* msvc = &sdk->msvc;
      push_var(env, "INCLUDE", search_list(roots, mem, spn_msvc_include_dirs(msvc)));
      push_var(env, "LIB", search_list(roots, mem, spn_msvc_lib_dirs(msvc)));
      if (!spn_path_empty(msvc->install.vs)) {
        push_var(env, "VSINSTALLDIR", native_dir(roots, mem, msvc->install.vs, ""));
        push_var(env, "VCINSTALLDIR", native_dir(roots, mem, msvc->install.vs, "VC/"));
        push_var(env, "VCToolsInstallDir", native_dir(roots, mem, msvc->install.toolset, ""));
        push_var(env, "VCToolsVersion", msvc->version);
        push_var(env, "WindowsSdkDir", native_dir(roots, mem, msvc->install.kit.root, ""));
        push_var(env, "WindowsSDKVersion", sp_fmt(mem, "{}\\", sp_fmt_str(msvc->install.kit.version)).value);
        push_var(env, "UniversalCRTSdkDir", native_dir(roots, mem, msvc->install.kit.root, ""));
        push_var(env, "UCRTVersion", msvc->install.kit.version);
        push_var(env, "VSCMD_ARG_HOST_ARCH", spn_msvc_arch_name(msvc->install.host));
        push_var(env, "VSCMD_ARG_TGT_ARCH", spn_msvc_arch_name(msvc->arch));
      }
      break;
    }
    case SPN_SDK_MACOS: {
      push_var(env, "SDKROOT", spn_path_str(roots, mem, sdk->macos.root));
      break;
    }
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT: {
      break;
    }
  }

  if (session->ctx->devshell) {
    const c8* const* shell = SP_NULLPTR;
    u32 count = spn_msvc_shell_vars(&shell);
    sp_for(it, count) {
      sp_str_t name = sp_cstr_as_str(shell[it]);
      if (!sets(env, name)) {
        push_clear(env, name);
      }
    }
  }
  return SPN_OK;
}

spn_op_t* spn_env(spn_session_t* session) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_ENV);
  spn_op_submit(op);
  return op;
}
