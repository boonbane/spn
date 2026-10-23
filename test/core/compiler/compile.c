#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  const c8* compiler;
  spn_wasi_spelling_t wasi;
  test_profile_t profile;
  spn_lang_t lang;
  spn_cxx_options_t cxx;
  bool pic;
  const c8* arg;
  const c8* link_arg;
  const c8* include;
  const c8* define;
  const c8* depfile;
  const c8* libc;
  spn_os_version_t min_os;
  render_expect_t expect;
} compile_test_t;

static const compile_test_t tests [] = {
  {
    .name = "gcc_linux",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
    },
    .pic = true,
    .arg = "-fno-common",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-fPIC", "-fno-common", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "toolchain_link_args_never_compile",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
    },
    .link_arg = "-fuse-ld=lld",
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_gnu_dialect",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_GNU11,
    },
    .expect = {
      .command = "cc",
      .args = { "-std=gnu11", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_depfile",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
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
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_WASI,
      .standard = SPN_C99,
      .opt = SPN_OPT_LEVEL_2,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasi", "-std=c99", "-O2", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_wasi_p1",
    .driver = SPN_CC_DRIVER_CLANG,
    .wasi = SPN_WASI_SPELLING_WASIP1,
    .profile = {
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_WASI,
      .standard = SPN_C99,
      .opt = SPN_OPT_LEVEL_2,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasip1", "-std=c99", "-O2", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_linux_target",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_MUSL,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_windows_msvc_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-c", "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o" },
    },
  },
  {
    .name = "zig_freestanding_target",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_FREESTANDING,
      .abi = SPN_ABI_BARE,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-freestanding-none", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-fno-sanitize=undefined", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_freestanding_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_FREESTANDING,
      .abi = SPN_ABI_BARE,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-none-elf", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_linux_none_target",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_BARE,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-none", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-fno-sanitize=undefined", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_linux_none_target",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_BARE,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-none", "-std=c99", "-ffreestanding", "-fno-stack-protector", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "clang_freestanding_elf_sysroot",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_FREESTANDING,
      .abi = SPN_ABI_ELF,
      .standard = SPN_C99,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_GNU,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_GNU,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
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
    .name = "msvc_asm_uses_masm_beside_cl",
    .driver = SPN_CC_DRIVER_MSVC,
    .compiler = "vc/bin/cl.exe",
    .lang = SPN_LANG_ASM,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
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
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_C11,
      .mode = SPN_MODE_DEBUG,
      .sdk = "/X",
      .bin = "/X/bin/arm64",
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
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .sdk = "/X",
      .bin = "/X/bin/arm64",
    },
    .expect = {
      .command = "/X/bin/arm64/cl.exe",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/I/X/crt/include", "/I/X/sdk/include/ucrt", "/I/X/sdk/include/um", "/I/X/sdk/include/shared", "/we4715", "/Fomain.o", "main.c" },
      .env = { "INCLUDE=/X/crt/include;/X/sdk/include/ucrt;/X/sdk/include/um;/X/sdk/include/shared" },
    },
  },
  {
    .name = "msvc_gnu11_takes_c11_switch",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_GNU11,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MT", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_c99_has_no_switch",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_C99,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_shared_runtime_uses_md",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_SHARED,
      .standard = SPN_C11,
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c11", "/MD", "/c", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_debug",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
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
    .name = "msvc_cxx_defaults",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
    },
    .lang = SPN_LANG_CXX,
    .expect = {
      .command = "c++",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c++17", "/MT", "/c", "/EHsc", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "msvc_cxx_options",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .standard = SPN_CXX20, .no_exceptions = true, .no_rtti = true },
    .expect = {
      .command = "c++",
      .args = { "/nologo", "/utf-8", "/Brepro", "/std:c++20", "/MT", "/c", "/GR-", "/we4715", "/Fomain.o", "main.c" },
    },
  },
  {
    .name = "clang_macos_sdk",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_MACOS,
      .abi = SPN_ABI_APPLE,
      .standard = SPN_C99,
      .sdk = "/sdk",
    },
    .min_os = { 13 },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-isysroot", "/sdk", "-mmacosx-version-min=13.0", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_macos_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_MACOS,
      .abi = SPN_ABI_APPLE,
      .standard = SPN_C99,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_C99,
      .sdk = "/X",
    },
    .expect = {
      .command = "cc",
      .args = {
        "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-c",
        "-nostdlibinc",
        "-isystem", "/X/crt/include",
        "-isystem", "/X/sdk/include/ucrt",
        "-isystem", "/X/sdk/include/um",
        "-isystem", "/X/sdk/include/shared",
        "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o"
      },
    },
  },
  {
    .name = "zig_msvc_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .standard = SPN_C99,
      .sdk = "/X",
    },
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-std=c99", "-fms-runtime-lib=static", "-c", "-gno-codeview-command-line", "-Werror=return-type", "main.c", "-Xclang", "-object-file-name=main.o", "-o", "main.o" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "zig_libc_sdk_without_libc_file",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_MACOS,
      .abi = SPN_ABI_APPLE,
      .standard = SPN_C99,
      .sdk = "/sdk",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "zig_libc_file_without_sdk",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_MUSL,
      .standard = SPN_C99,
    },
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "msvc_sdk",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .linking.runtime = SPN_RUNTIME_STATIC,
      .sdk = "/X",
    },
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/utf-8", "/Brepro", "/MT", "/c", "/I/X/crt/include", "/I/X/sdk/include/ucrt", "/I/X/sdk/include/um", "/I/X/sdk/include/shared", "/we4715", "/Fomain.o", "main.c" },
      .env = { "INCLUDE=/X/crt/include;/X/sdk/include/ucrt;/X/sdk/include/um;/X/sdk/include/shared" },
    },
  },
  {
    .name = "cxx_defaults",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .no_exceptions = true, .no_rtti = true },
    .expect = {
      .command = "c++",
      .args = { "-std=c++17", "-c", "-fno-exceptions", "-fno-rtti", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "cxx_standard",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
    },
    .lang = SPN_LANG_CXX,
    .cxx = { .standard = SPN_CXX20 },
    .expect = {
      .command = "c++",
      .args = { "-std=c++20", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "includes_and_defines",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
      .sanitizers = SPN_SANITIZER_ADDRESS,
    },
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-fsanitize=address", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-c", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "macos_min_os_minor",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .arch = SPN_ARCH_ARM64,
      .os = SPN_OS_MACOS,
      .abi = SPN_ABI_APPLE,
      .standard = SPN_C99,
    },
    .min_os = { 13, 1 },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-std=c99", "-c", "-mmacosx-version-min=13.1", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "foreign_platform_config_never_renders",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
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
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_MUSL,
      .standard = SPN_C99,
      .sdk = "/S",
    },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-musl", "-std=c99", "-c", "--sysroot=/S", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
  {
    .name = "gcc_linux_sysroot",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .arch = SPN_ARCH_X64,
      .os = SPN_OS_LINUX,
      .abi = SPN_ABI_GNU,
      .standard = SPN_C99,
      .sdk = "/S",
    },
    .expect = {
      .command = "cc",
      .args = { "-std=c99", "-c", "--sysroot=/S", "-Werror=return-type", "main.c", "-o", "main.o" },
    },
  },
};

sp_test_each(render_compile, render, compile_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  toolchain.wasi = it->wasi;
  if (it->compiler) {
    toolchain.compiler.program = spn_arg_path(test_arg_path(it->compiler));
  }
  spn_cc_compile_t compile = {
    .cxx = it->cxx,
    .pic = it->pic,
    .min_os = it->min_os,
  };
  sp_da_init(mem, toolchain.link_args);
  if (it->arg) {
    si_da_push(mem, compile.args, sp_str_from_cstr(mem, it->arg));
  }
  if (it->link_arg) {
    sp_da_push(toolchain.link_args, sp_str_from_cstr(mem, it->link_arg));
  }
  if (it->include) {
    si_da_push(mem, compile.include, test_arg_path(it->include));
  }
  if (it->define) {
    si_da_push(mem, compile.define, sp_str_from_cstr(mem, it->define));
  }

  spn_profile_info_t profile = test_profile(it->profile);
  profile.sdk = spn_sdk_for_driver(spn_toolchain_driver_caps(it->driver), profile.sdk);
  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, it->lang, &compile);

  spn_cc_compile_files_t files = {
    .source = test_arg_path("main.c"),
    .output = test_arg_path("main.o"),
    .depfile = it->depfile ? test_arg_path(it->depfile) : sp_zero_s(spn_path_t),
    .libc = it->libc ? test_arg_path(it->libc) : sp_zero_s(spn_path_t),
  };
  spn_invocation_t invocation = spn_cc_render_compile_command(mem, &plan, &files);
  return expect_args(t, &invocation, it->expect);
}

sp_test(render_compile, base_shared_across_commands, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(SPN_CC_DRIVER_ZIG);
  toolchain.cache = test_arg_path("/C");
  spn_cc_compile_t compile = sp_zero;
  spn_profile_info_t profile = test_profile((test_profile_t) {
    .arch = SPN_ARCH_ARM64,
    .os = SPN_OS_MACOS,
    .abi = SPN_ABI_APPLE,
    .standard = SPN_C99,
    .sdk = "/sdk",
  });
  profile.sdk = spn_sdk_for_driver(spn_toolchain_driver_caps(SPN_CC_DRIVER_ZIG), profile.sdk);
  sp_must_eq(t, (u32)SPN_SDK_LIBC, (u32)profile.sdk.kind);

  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, SPN_LANG_C, &compile);
  u64 args = sp_da_size(plan.base.args);
  u64 env = sp_da_size(plan.base.env);

  spn_cc_compile_files_t first = {
    .cwd = test_arg_path("/W"),
    .source = test_arg_path("main.c"),
    .output = test_arg_path("a.o"),
    .libc = test_arg_path("/L"),
  };
  spn_cc_compile_files_t second = {
    .source = test_arg_path("main.c"),
    .output = test_arg_path("b.o"),
    .depfile = test_arg_path("b.o.d"),
    .libc = test_arg_path("/L"),
  };
  spn_invocation_t a = spn_cc_render_compile_command(mem, &plan, &first);
  spn_invocation_t b = spn_cc_render_compile_command(mem, &plan, &second);

  sp_expect_eq(t, sp_da_size(plan.base.args), args);
  sp_expect_eq(t, sp_da_size(plan.base.env), env);
  sp_expect_str_eq_c(t, a.cwd.sub, "/W");
  sp_expect(t, spn_path_empty(b.cwd));
  sp_expect(t, spn_path_empty(plan.base.cwd));
  if (expect_args(t, &a, (render_expect_t) {
    .command = "cc",
    .args = { "--target=aarch64-macos", "-std=c99", "-c", "-Werror=return-type", "main.c", "-o", "a.o" },
    .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C", "ZIG_LIBC=/L" },
  })) {
    return SP_ERR;
  }
  return expect_args(t, &b, (render_expect_t) {
    .command = "cc",
    .args = { "--target=aarch64-macos", "-std=c99", "-c", "-Werror=return-type", "main.c", "-MD", "-MF", "b.o.d", "-o", "b.o" },
    .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C", "ZIG_LIBC=/L" },
  });
}

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_os_t os;
  spn_abi_t abi;
  bool expect;
} codeview_test_t;

static const codeview_test_t codeview_tests [] = {
  { .name = "zig_windows_gnu", .driver = SPN_CC_DRIVER_ZIG, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_GNU, .expect = true },
  { .name = "zig_windows_msvc", .driver = SPN_CC_DRIVER_ZIG, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .expect = true },
  { .name = "clang_windows_msvc", .driver = SPN_CC_DRIVER_CLANG, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .expect = true },
  { .name = "clang_windows_gnu", .driver = SPN_CC_DRIVER_CLANG, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_GNU, .expect = false },
  { .name = "gcc_windows_gnu", .driver = SPN_CC_DRIVER_GCC, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_GNU, .expect = false },
  { .name = "zig_linux_musl", .driver = SPN_CC_DRIVER_ZIG, .os = SPN_OS_LINUX, .abi = SPN_ABI_MUSL, .expect = false },
};

sp_test_each(render_compile, codeview, codeview_test_t, codeview_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  spn_profile_info_t profile = test_profile((test_profile_t) {
    .arch = SPN_ARCH_X64,
    .os = it->os,
    .abi = it->abi,
    .linking.runtime = SPN_RUNTIME_STATIC,
    .standard = SPN_C99,
  });
  spn_cc_compile_t compile = sp_zero;
  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, SPN_LANG_C, &compile);
  sp_expect_eq(t, (u32)plan.codeview, (u32)it->expect);
  return SP_OK;
}
