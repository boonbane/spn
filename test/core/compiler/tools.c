#include "compiler.h"

typedef enum {
  RENDER_COMPILE,
  RENDER_LINK,
  RENDER_ARCHIVE,
} render_t;

typedef struct {
  const c8* path [SPN_DIR_LIST_MAX];
} expect_t;

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  render_t render;
  const c8* tools;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "msvc_compile", .driver = SPN_CC_DRIVER_MSVC, .render = RENDER_COMPILE, .tools = "/T", .expect = { .path = { "/T" } } },
  { .name = "msvc_link", .driver = SPN_CC_DRIVER_MSVC, .render = RENDER_LINK, .tools = "/T", .expect = { .path = { "/T" } } },
  { .name = "msvc_archive", .driver = SPN_CC_DRIVER_MSVC, .render = RENDER_ARCHIVE, .tools = "/T", .expect = { .path = { "/T" } } },
  { .name = "clang_link", .driver = SPN_CC_DRIVER_CLANG, .render = RENDER_LINK, .tools = "/T", .expect = { .path = { "/T" } } },
  { .name = "sdk_without_tools", .driver = SPN_CC_DRIVER_MSVC, .render = RENDER_LINK },
};

sp_test_each(render_tools, path, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  spn_profile_info_t profile = test_profile((test_profile_t) {
    .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    .linking.runtime = SPN_RUNTIME_STATIC,
    .sdk = "/X",
    .tools = it->tools,
  });

  spn_dir_list_t path = sp_zero;
  switch (it->render) {
    case RENDER_COMPILE: {
      spn_cc_compile_t compile = sp_zero;
      path = si_cc_render_compile(mem, &toolchain, &profile, SPN_LANG_C, &compile).base.path;
      break;
    }
    case RENDER_LINK: {
      spn_cc_link_t link = { .lang = SPN_LANG_C, .kind = SPN_CC_OUTPUT_EXE };
      path = si_cc_render_link(mem, &toolchain, &profile, &link).base.path;
      break;
    }
    case RENDER_ARCHIVE: {
      path = si_cc_render_archive(mem, &toolchain, &profile).base.path;
      break;
    }
  }

  u32 count = 0;
  sp_carr_detect_len(it->expect.path, count, it->expect.path[count]);
  sp_must_eq(t, count, path.count);
  sp_for(at, count) {
    sp_expect_str_eq_c(t, spn_path_str(&spn.roots, mem, path.items[at]), it->expect.path[at]);
  }
  return SP_OK;
}
