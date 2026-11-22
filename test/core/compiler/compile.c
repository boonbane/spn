#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  const c8* compiler;
  test_profile_t profile;
  spn_lang_t lang;
  spn_cxx_options_t cxx;
  bool pic;
  const c8* arg;
  const c8* include;
  const c8* define;
  const c8* depfile;
  const c8* libc;
  const c8* embed_include;
  const c8* cwd;
  spn_os_version_t min_os;
  render_expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "gcc_linux",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .pic = true,
    .arg = "-fno-common",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-fPIC", "-fno-common", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_embed_include_is_a_location",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .include = "inc",
    .embed_include = "E",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-Iinc", "-Werror=return-type", "-IE", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_debug_maps_cwd_out_of_debug_info",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .mode = SPN_MODE_DEBUG,
      .opt = SPN_OPT_LEVEL_0,
    },
    .cwd = "/W",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-g", "-gno-record-gcc-switches", "-O0", "-c", "-Werror=return-type", "main.c", "-fdebug-prefix-map=/W=.", "-o", "main.o" },
    },
  },
  {
    .name = "clang_debug_needs_no_prefix_map",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .mode = SPN_MODE_DEBUG,
    },
    .cwd = "/W",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-gnu", "-std=c99", "-g", "-ffile-compilation-dir=.", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_release_defines_ndebug",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .mode = SPN_MODE_RELEASE,
      .opt = SPN_OPT_LEVEL_2,
    },
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-O2", "-DNDEBUG", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_depfile",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .depfile = "main.o.d",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-Werror=return-type", "main.c", "-MD", "-MF", "main.o.d", "-o", "main.o" },
    },
  },
  {
    .name = "clang_wasi",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
    },
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasi", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_wasi_p1",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
      .spelling = "wasm32-wasip1",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasip1", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_windows_msvc_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-mno-incremental-linker-compatible", "-c", "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o" },
    },
  },
  {
    .name = "zig_freestanding_target",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_FREESTANDING, SPN_ABI_BARE },
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-freestanding-none", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-fno-sanitize=undefined", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_freestanding_wasm_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_FREESTANDING, SPN_ABI_BARE },
    },
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-unknown-unknown", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_freestanding_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_FREESTANDING, SPN_ABI_BARE },
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-none-elf", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_freestanding_elf_sysroot",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_FREESTANDING, SPN_ABI_ELF },
      .sdk = "/S",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-none-elf", "-std=c99", "-c", "--sysroot=/S", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_windows_deterministic_codeview",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
      .standard = SPN_C11,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-gnu", "-std=c11", "-c", "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o" },
    },
  },
  {
    .name = "clang_windows_gnu_writes_dwarf",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
      .standard = SPN_C11,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-gnu", "-std=c11", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "msvc_windows",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
    },
    .include = "inc",
    .define = "SPUM=1",
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/c", "/Iinc", "/DSPUM=1", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_embed_include_is_a_location",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
    },
    .include = "inc",
    .embed_include = "E",
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/c", "/Iinc", "/we4715", "/IE", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_depfile",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .depfile = "main.o.d",
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/we4715", "/sourceDependencies", "main.o.d", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_asm_uses_masm_beside_cl",
    .driver = SPN_CC_DRIVER_MSVC,
    .compiler = "vc/bin/cl.exe",
    .lang = SPN_LANG_ASM,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
      .mode = SPN_MODE_DEBUG,
    },
    .include = "inc",
    .define = "SPUM=1",
    .expect = {
      .command = "vc/bin/ml64.exe",
      .args = { "/nologo", "/c", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_asm_arm64_uses_sdk_bin",
    .driver = SPN_CC_DRIVER_MSVC,
    .compiler = "vc/bin/cl.exe",
    .lang = SPN_LANG_ASM,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
      .mode = SPN_MODE_DEBUG,
      .sdk = "/X",
      .msvc.bin = "/X/bin/arm64",
    },
    .expect = {
      .command = "/X/bin/arm64/armasm64.exe",
      .args = { "/nologo", "/c", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_sdk_bin_selects_cl",
    .driver = SPN_CC_DRIVER_MSVC,
    .compiler = "vc/bin/cl.exe",
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .sdk = "/X",
      .msvc.bin = "/X/bin/arm64",
    },
    .expect = {
      .command = "/X/bin/arm64/cl.exe",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/I/X/crt/include", "/I/X/sdk/include/ucrt", "/I/X/sdk/include/um", "/I/X/sdk/include/shared", "/I/X/sdk/include/winrt", "/I/X/sdk/include/cppwinrt", "/we4715", "/Fomain.o", "main.c" },
      .env = { "INCLUDE=/X/crt/include;/X/sdk/include/ucrt;/X/sdk/include/um;/X/sdk/include/shared;/X/sdk/include/winrt;/X/sdk/include/cppwinrt" },
    },
  },
  {
    .name = "msvc_c99_has_no_switch",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_debug",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
      .mode = SPN_MODE_DEBUG,
      .opt = SPN_OPT_LEVEL_0,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/Z7", "/Od", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_release_defines_ndebug",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
      .mode = SPN_MODE_RELEASE,
      .opt = SPN_OPT_LEVEL_2,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/O2", "/DNDEBUG", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_cxx_defaults",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .exceptions = true },
    .expect = {
      .command = "c++",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c++17", "/MT", "/c", "/EHsc", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_cxx_options",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .standard = SPN_CXX20, .no_rtti = true },
    .expect = {
      .command = "c++",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c++20", "/MT", "/c", "/GR-", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "clang_macos_sdk",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_APPLE },
      .sdk = "/sdk",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-isysroot", "/sdk", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_macos_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_APPLE },
      .sdk = "/sdk",
    },
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "clang_msvc_sdk",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .sdk = "/X",
    },
    .expect = {
      .command = "cc",
      .args = {
        "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-mno-incremental-linker-compatible", "-c",
        "-nostdlibinc",
        "-isystem/X/crt/include",
        "-isystem/X/sdk/include/ucrt",
        "-isystem/X/sdk/include/um",
        "-isystem/X/sdk/include/shared",
        "-isystem/X/sdk/include/winrt",
        "-isystem/X/sdk/include/cppwinrt",
        "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o"
      },
    },
  },
  {
    .name = "zig_msvc_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .sdk = "/X",
    },
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-mno-incremental-linker-compatible", "-c", "-isystem/X/sdk/include/winrt", "-isystem/X/sdk/include/cppwinrt", "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "cxx_defaults",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .no_rtti = true },
    .expect = {
      .command = "c++",
      .args = { "-std=c++17", "-c", "-fno-exceptions", "-fno-rtti", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "cxx_exceptions_take_the_row_flags",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
      .exceptions.flags = { "-fA", "-fB" },
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .exceptions = true },
    .expect = {
      .command = "c++",
      .args = { "--target=wasm32-wasi", "-std=c++17", "-c", "-fA", "-fB", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "includes_and_defines",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .include = "inc",
    .define = "SPUM=1",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-Iinc", "-DSPUM=1", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "sanitizers_on_compile_line",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .sanitizers = SPN_SANITIZER_ADDRESS,
    },
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-fsanitize=address", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "msvc_sanitizers_on_compile_line",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .standard = SPN_C11,
      .sanitizers = SPN_SANITIZER_ADDRESS,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/fsanitize=address", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "macos_min_os_minor",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_APPLE },
    },
    .min_os = { 13, 1 },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-mmacosx-version-min=13.1", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_macos_min_os",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_APPLE },
    },
    .min_os = { 13, 1 },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos.13.1", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "foreign_platform_config_never_renders",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .min_os = { 13 },
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_linux_sysroot",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
      .sdk = "/S",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "--sysroot=/S", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
};

sp_test_each(render_compile, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  if (it->compiler) {
    toolchain.compiler.program = spn_arg_path(test_arg_path(it->compiler));
  }
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, it->driver, it->profile, &profile));

  spn_cc_compile_t compile = {
    .cxx = it->cxx,
    .pic = it->pic,
    .min_os = it->min_os,
  };
  test_push_str(mem, &compile.args, it->arg);
  test_push_path(mem, &compile.include, it->include);
  test_push_str(mem, &compile.define, it->define);

  spn_cc_compile_files_t files = {
    .cwd = test_arg_path(it->cwd),
    .source = test_arg_path("main.c"),
    .output = test_arg_path("main.o"),
    .depfile = test_arg_path(it->depfile),
    .libc = test_arg_path(it->libc),
    .include = test_arg_path(it->embed_include),
  };
  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, it->lang, &compile);
  spn_invocation_t invocation = spn_cc_render_compile_command(mem, &plan, &files);
  return expect_invocation(t, &invocation, it->expect);
}

sp_test(render_compile, base_shared_across_commands) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(SPN_CC_DRIVER_ZIG);
  spn_cc_compile_t compile = sp_zero;
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, SPN_CC_DRIVER_ZIG, (test_profile_t) {
    .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
  }, &profile));
  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, SPN_LANG_C, &compile);

  spn_cc_compile_files_t first = {
    .source = test_arg_path("main.c"),
    .output = test_arg_path("a.o"),
    .libc = test_arg_path("/L"),
  };
  spn_cc_compile_files_t second = {
    .source = test_arg_path("main.c"),
    .output = test_arg_path("b.o"),
    .libc = test_arg_path("/L"),
  };
  spn_invocation_t a = spn_cc_render_compile_command(mem, &plan, &first);
  spn_invocation_t b = spn_cc_render_compile_command(mem, &plan, &second);

  if (expect_invocation(t, &a, (render_expect_t) {
    .command = "cc",
    .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "a.o" },
    .env = { "ZIG_LIBC=/L" },
  })) {
    return SP_ERR;
  }
  return expect_invocation(t, &b, (render_expect_t) {
    .command = "cc",
    .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "b.o" },
    .env = { "ZIG_LIBC=/L" },
  });
}
