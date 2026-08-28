#include "compiler.h"

typedef enum {
  RENDER_COMPILE,
  RENDER_COMPILE_COMMAND,
  RENDER_LINK,
  RENDER_ARCHIVE,
} render_t;

typedef struct {
  const c8* name;
  render_t render;
  const c8* cache;
  const c8* env [render_env_max];
} env_test_t;

static const env_test_t tests [] = {
  { .name = "compile", .render = RENDER_COMPILE, .cache = "/C", .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" } },
  { .name = "compile_command", .render = RENDER_COMPILE_COMMAND, .cache = "/C", .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" } },
  { .name = "link", .render = RENDER_LINK, .cache = "/C", .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" } },
  { .name = "archive", .render = RENDER_ARCHIVE, .cache = "/C", .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" } },
  { .name = "empty", .render = RENDER_COMPILE },
};

sp_test_each(render_env, render, env_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_toolchain_t toolchain = test_toolchain(SPN_CC_DRIVER_ZIG);
  if (it->cache) {
    toolchain.cache = test_arg_path(it->cache);
  }

  spn_profile_info_t profile = test_profile((test_profile_t) {
    .arch = SPN_ARCH_X64,
    .os = SPN_OS_LINUX,
    .abi = SPN_ABI_MUSL,
    .standard = SPN_C99,
  });

  spn_invocation_t invocation = sp_zero;
  switch (it->render) {
    case RENDER_COMPILE: {
      spn_cc_compile_t compile = { .lang = SPN_LANG_C };
      spn_cc_render_compile(mem, &toolchain, &profile, &compile, &invocation);
      break;
    }
    case RENDER_COMPILE_COMMAND: {
      spn_cc_compile_t compile = { .lang = SPN_LANG_C };
      spn_invocation_t base = sp_zero;
      spn_cc_render_compile(mem, &toolchain, &profile, &compile, &base);
      spn_cc_compile_files_t files = {
        .source = test_arg_path("main.c"),
        .output = test_arg_path("main.o"),
      };
      invocation = spn_cc_render_compile_command(mem, &toolchain, &profile, &base, &files);
      break;
    }
    case RENDER_LINK: {
      spn_cc_link_t link = { .lang = SPN_LANG_C, .kind = SPN_CC_OUTPUT_EXE };
      spn_cc_link_files_t files = { .output = test_arg_path("main") };
      sp_da_init(mem, files.objects);
      sp_da_push(files.objects, test_arg_path("main.o"));
      sp_must_eq(t, spn_cc_render_link(mem, &toolchain, spn_triple_host(), &profile, &link, &files, &invocation), SPN_OK);
      break;
    }
    case RENDER_ARCHIVE: {
      spn_cc_archive_files_t files = { .output = test_arg_path("libmain.a") };
      sp_da_init(mem, files.objects);
      sp_da_push(files.objects, test_arg_path("main.o"));
      sp_must_eq(t, spn_cc_render_archive(mem, &toolchain, &profile, &files, &invocation), SPN_OK);
      break;
    }
  }

  const spn_path_roots_t* roots = &spn.roots;
  sp_da(sp_str_t) env = sp_da_new(mem, sp_str_t);
  sp_da_for(invocation.env, v) {
    sp_env_var_t var = spn_invocation_env_var(roots, mem, invocation.env[v]);
    sp_da_push(env, sp_fmt(mem, "{}={}", sp_fmt_str(var.key), sp_fmt_str(var.value)).value);
  }
  sp_must_strs_eq(t, env, sp_da_size(env), it->env);
  return SP_OK;
}
