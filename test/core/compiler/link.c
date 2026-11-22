#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  const c8* link_args [2];
  test_profile_t profile;
  spn_cc_output_kind_t kind;
  bool exceptions;
  bool import_undefined;
  const c8* exports;
  const c8* implib;
  const c8* archive;
  const c8* shared;
  const c8* whole_archive;
  const c8* system_lib;
  const c8* framework;
  const c8* arg;
  const c8* script;
  const c8* libc;
  spn_os_version_t min_os;
  spn_win_subsystem_t subsystem;
  render_expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "clang_wasi_reactor",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
    },
    .kind = SPN_CC_OUTPUT_REACTOR,
    .exports = "S.rsp",
    .expect = {
      .command = "cc",
      .args = {
        "--target=wasm32-wasi",
        "-mexec-model=reactor",
        "-Wl,--no-entry", "@S.rsp",
        "main.o", "-o", "main"
      },
    },
  },
  {
    .name = "clang_reactor_imports_undefined",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
    },
    .kind = SPN_CC_OUTPUT_REACTOR,
    .import_undefined = true,
    .exports = "S.rsp",
    .expect = {
      .command = "cc",
      .args = {
        "--target=wasm32-wasi",
        "-mexec-model=reactor",
        "-Wl,--no-entry", "-Wl,--import-undefined", "@S.rsp",
        "main.o", "-o", "main"
      },
    },
  },
  {
    .name = "freestanding_wasm_reactor_has_no_exec_model",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_FREESTANDING, SPN_ABI_BARE },
    },
    .kind = SPN_CC_OUTPUT_REACTOR,
    .exports = "S.rsp",
    .expect = {
      .command = "cc",
      .args = {
        "--target=wasm32-freestanding-none",
        "-nostdlib",
        "-Wl,--no-entry", "@S.rsp",
        "main.o", "-o", "main"
      },
    },
  },
  {
    .name = "zig_reactor_imports_symbols",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
    },
    .kind = SPN_CC_OUTPUT_REACTOR,
    .import_undefined = true,
    .exports = "S.rsp",
    .expect = {
      .command = "cc",
      .args = {
        "--target=wasm32-wasi",
        "-mexec-model=reactor",
        "-Wl,--no-entry", "-Wl,--import-symbols", "@S.rsp",
        "main.o", "-o", "main"
      },
    },
  },
  {
    .name = "clang_wasi_static_runtime_has_no_gnu_runtime",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasi", "main.o", "-o", "main" },
    },
  },
  {
    .name = "exceptions_take_the_row_link_flags",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
      .exceptions.link_flags = { "-fA", "-lB" },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .exceptions = true,
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasi", "-fA", "-lB", "main.o", "-o", "main" },
    },
  },
  {
    .name = "row_link_flags_stay_off_without_exceptions",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_WASM32, SPN_OS_WASI },
      .exceptions.link_flags = { "-fA", "-lB" },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=wasm32-wasi", "main.o", "-o", "main" },
    },
  },
  {
    .name = "msvc_shared_lib",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.def",
    .implib = "main.lib",
    .archive = "spum.lib",
    .system_lib = "ws2_32",
    .expect = {
      .command = "cc",
      .args = {
        "/nologo",
        "/LD",
        "main.o",
        "spum.lib",
        "/Femain",
        "ws2_32.lib",
        "/link", "/DEF:S.def", "/IMPLIB:main.lib"
      },
    },
  },
  {
    .name = "msvc_debug_pdb",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .mode = SPN_MODE_DEBUG,
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "/nologo", "main.o", "/Femain", "/link", "/DEBUG", "/NOIMPLIB", "/NOEXP" },
    },
  },
  {
    .name = "msvc_subsystem",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .subsystem = SPN_WIN_SUBSYSTEM_WINDOWS,
    .expect = {
      .command = "cc",
      .args = { "/nologo", "main.o", "/Femain", "/link", "/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", "/NOIMPLIB", "/NOEXP" },
    },
  },
  {
    .name = "clang_macos_frameworks",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS },
      .sdk = "/sdk",
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .framework = "Cocoa",
    .min_os = { 13 },
    .expect = {
      .command = "cc",
      .args = {
        "--target=aarch64-macos",
        "main.o",
        "-isysroot", "/sdk",
        "-mmacosx-version-min=13.0",
        "-framework", "Cocoa",
        "-Wl,-rpath,@loader_path", "-o", "main"
      },
    },
  },
  {
    .name = "zig_macos_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS },
      .sdk = "/sdk",
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "main.o", "-L/sdk/usr/lib", "-F", "/sdk/System/Library/Frameworks", "-Wl,-rpath,@loader_path", "-o", "main" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "zig_macos_min_os",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .min_os = { 13 },
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos.13.0", "main.o", "-Wl,-rpath,@loader_path", "-o", "main" },
    },
  },
  {
    .name = "windows_gnu_subsystem",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .subsystem = SPN_WIN_SUBSYSTEM_WINDOWS,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-gnu", "-Wl,--subsystem,windows", "main.o", "-o", "main" },
    },
  },
  {
    .name = "gcc_linux_static_runtime_keeps_shared_libc",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .request.runtime = SPN_RUNTIME_STATIC,
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-static-libstdc++", "-static-libgcc", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "gcc_linux_musl_static_libc",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-static", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "gcc_linux_musl_shared_lib_static_libc",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
    },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.map",
    .expect = {
      .command = "cc",
      .args = { "-shared", "-static-libstdc++", "-static-libgcc", "-Wl,--version-script,S.map", "-Wl,-soname,main", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "gcc_windows_gnu_static_runtime",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-static-libstdc++", "-static-libgcc", "-Wl,-Bstatic,--whole-archive", "-lwinpthread", "-Wl,--no-whole-archive,-Bdynamic", "main.o", "-o", "main" },
    },
  },
  {
    .name = "zig_windows_gnu_static_runtime_is_bare",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-gnu", "main.o", "-o", "main" },
    },
  },
  {
    .name = "clang_freestanding_elf_exe",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_FREESTANDING, SPN_ABI_ELF },
      .sdk = "/S",
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-none-elf", "-static", "main.o", "--sysroot=/S", "-o", "main" },
    },
  },
  {
    .name = "libs_precede_system_libs",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .archive = "libA.a",
    .shared = "libS.so",
    .system_lib = "m",
    .expect = {
      .command = "cc",
      .args = { "main.o", "libA.a", "libS.so", "-lm", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "sanitizers_on_link_line",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .sanitizers = SPN_SANITIZER_ADDRESS,
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-fsanitize=address", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "msvc_sanitizers_on_link_line",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC },
      .sanitizers = SPN_SANITIZER_ADDRESS,
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "/nologo", "/fsanitize=address", "main.o", "/Femain", "/link", "/NOIMPLIB", "/NOEXP" },
    },
  },
  {
    .name = "linux_shared_exports",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.map",
    .whole_archive = "libD.a",
    .archive = "libP.a",
    .expect = {
      .command = "cc",
      .args = {
        "-shared", "-Wl,--version-script,S.map", "-Wl,-soname,main",
        "main.o",
        "-Wl,--whole-archive", "libD.a", "-Wl,--no-whole-archive",
        "libP.a",
        "-Wl,-rpath,$ORIGIN", "-o", "main"
      },
    },
  },
  {
    .name = "macos_shared_exports",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_MACOS },
    },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.exp",
    .whole_archive = "libD.a",
    .archive = "libP.a",
    .expect = {
      .command = "cc",
      .args = {
        "--target=aarch64-macos",
        "-shared", "-Wl,-exported_symbols_list,S.exp", "-Wl,-install_name,@rpath/main",
        "main.o",
        "-Wl,-force_load,libD.a",
        "libP.a",
        "-Wl,-rpath,@loader_path", "-o", "main"
      },
    },
  },
  {
    .name = "windows_gnu_shared_def",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.def",
    .whole_archive = "libD.a",
    .archive = "libP.a",
    .expect = {
      .command = "cc",
      .args = {
        "--target=x86_64-windows-gnu",
        "-shared", "S.def",
        "main.o",
        "-Wl,--whole-archive", "libD.a", "-Wl,--no-whole-archive",
        "libP.a",
        "-o", "main"
      },
    },
  },
  {
    .name = "foreign_platform_config_never_renders",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .framework = "Cocoa",
    .min_os = { 13 },
    .subsystem = SPN_WIN_SUBSYSTEM_WINDOWS,
    .expect = {
      .command = "cc",
      .args = { "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "zig_linux_sysroot",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU },
      .sdk = "/S",
    },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-gnu", "main.o", "--sysroot=/S", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "toolchain_link_args_precede_target_args",
    .driver = SPN_CC_DRIVER_GCC,
    .link_args = { "-fuse-ld=lld", "-B" },
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU } },
    .kind = SPN_CC_OUTPUT_EXE,
    .arg = "-A",
    .script = "A.ld",
    .expect = {
      .command = "cc",
      .args = { "-fuse-ld=lld", "-B", "-A", "-Wl,-T,A.ld", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "msvc_toolchain_link_args_after_link",
    .driver = SPN_CC_DRIVER_MSVC,
    .link_args = { "/B" },
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .kind = SPN_CC_OUTPUT_EXE,
    .arg = "/A",
    .expect = {
      .command = "cc",
      .args = { "/nologo", "main.o", "/Femain", "/link", "/B", "/A", "/NOIMPLIB", "/NOEXP" },
    },
  },
  {
    .name = "clang_msvc_link",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "main.o", "-o", "main" },
    },
  },
  {
    .name = "clang_msvc_shared_def",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .exports = "S.def",
    .implib = "main.lib",
    .whole_archive = "libD.a",
    .archive = "P.lib",
    .expect = {
      .command = "cc",
      .args = {
        "--target=x86_64-windows-msvc",
        "-fms-runtime-lib=static",
        "-shared", "-Wl,/DEF:S.def", "-Wl,/IMPLIB:main.lib",
        "main.o",
        "-Wl,/WHOLEARCHIVE:libD.a",
        "P.lib",
        "-o", "main"
      },
    },
  },
  {
    .name = "clang_msvc_subsystem",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .kind = SPN_CC_OUTPUT_EXE,
    .subsystem = SPN_WIN_SUBSYSTEM_WINDOWS,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:mainCRTStartup", "main.o", "-o", "main" },
    },
  },
  {
    .name = "clang_freestanding_exe",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_FREESTANDING, SPN_ABI_BARE } },
    .kind = SPN_CC_OUTPUT_EXE,
    .script = "A.ld",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-none-elf", "-nostartfiles", "-nolibc", "-static", "-Wl,-T,A.ld", "main.o", "-o", "main" },
    },
  },
  {
    .name = "wasi_whole_archive",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_WASM32, SPN_OS_WASI } },
    .kind = SPN_CC_OUTPUT_REACTOR,
    .exports = "S.rsp",
    .whole_archive = "libD.a",
    .expect = {
      .command = "cc",
      .args = {
        "--target=wasm32-wasi",
        "-mexec-model=reactor",
        "-Wl,--no-entry", "@S.rsp",
        "main.o",
        "-Wl,--whole-archive", "libD.a", "-Wl,--no-whole-archive",
        "-o", "main"
      },
    },
  },
  {
    .name = "clang_msvc_sdk_libs",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X" },
    .kind = SPN_CC_OUTPUT_EXE,
    .system_lib = "user32",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "main.o", "-luser32", "-o", "main" },
      .env = { "LIB=/X/crt/lib/x86_64;/X/sdk/lib/ucrt/x86_64;/X/sdk/lib/um/x86_64" },
    },
  },
  {
    .name = "zig_msvc_libc_links_off_windows",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X" },
    .kind = SPN_CC_OUTPUT_EXE,
    .system_lib = "user32",
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "main.o", "-luser32", "-o", "main" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "zig_msvc_libc_searches_atl",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X", .msvc.atl = "/A" },
    .kind = SPN_CC_OUTPUT_EXE,
    .libc = "/L",
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "main.o", "-L/A", "-o", "main" },
      .env = { "ZIG_LIBC=/L" },
    },
  },
  {
    .name = "msvc_sdk_libs",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X" },
    .kind = SPN_CC_OUTPUT_EXE,
    .system_lib = "user32",
    .expect = {
      .command = "cc",
      .args = { "/nologo", "main.o", "/Femain", "user32.lib", "/link", "/NOIMPLIB", "/NOEXP" },
      .env = { "LIB=/X/crt/lib/x86_64;/X/sdk/lib/ucrt/x86_64;/X/sdk/lib/um/x86_64" },
    },
  },
  {
    .name = "strip_none_renders_nothing",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU }, .strip = SPN_STRIP_NONE },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "zig_strip_debuginfo",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU }, .strip = SPN_STRIP_DEBUGINFO },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-gnu", "-s", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "gnu_strip_debuginfo",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU }, .strip = SPN_STRIP_DEBUGINFO },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-Wl,-S", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "gnu_strip_symbols",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU }, .strip = SPN_STRIP_SYMBOLS },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "-s", "main.o", "-Wl,-rpath,$ORIGIN", "-o", "main" },
    },
  },
  {
    .name = "darwin_strip_debuginfo",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_ARM64, SPN_OS_MACOS }, .strip = SPN_STRIP_DEBUGINFO },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-Wl,-S", "main.o", "-Wl,-rpath,@loader_path", "-o", "main" },
    },
  },
  {
    .name = "darwin_strip_symbols",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_ARM64, SPN_OS_MACOS }, .strip = SPN_STRIP_SYMBOLS },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=aarch64-macos", "-Wl,-S,-x", "main.o", "-Wl,-rpath,@loader_path", "-o", "main" },
    },
  },
  {
    .name = "clang_msvc_strip_debuginfo_renders_nothing",
    .driver = SPN_CC_DRIVER_CLANG,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .strip = SPN_STRIP_DEBUGINFO },
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-windows-msvc", "-fms-runtime-lib=static", "main.o", "-o", "main" },
    },
  },
};

sp_test_each(render_link, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  toolchain.link_args = test_strs(mem, it->link_args, sp_carr_len(it->link_args));
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, it->driver, it->profile, &profile));

  spn_cc_link_t link = {
    .lang = SPN_LANG_C,
    .kind = it->kind,
    .exceptions = it->exceptions,
    .import_undefined = it->import_undefined,
    .min_os = it->min_os,
    .subsystem = it->subsystem,
  };
  test_push_str(mem, &link.system_libs, it->system_lib);
  test_push_str(mem, &link.frameworks, it->framework);
  test_push_str(mem, &link.args, it->arg);

  spn_cc_link_files_t files = {
    .output = test_arg_path("main"),
    .exports = test_arg_path(it->exports),
    .implib = test_arg_path(it->implib),
    .libc = test_arg_path(it->libc),
  };
  test_push_path(mem, &files.objects, "main.o");
  test_push_path(mem, &files.scripts, it->script);
  test_push_path(mem, &files.archives, it->archive);
  test_push_path(mem, &files.shared, it->shared);
  test_push_path(mem, &files.whole_archives, it->whole_archive);

  spn_cc_link_plan_t plan = si_cc_render_link(mem, &toolchain, &profile, &link);
  spn_invocation_t invocation = spn_cc_render_link_command(mem, &plan, &files);
  return expect_invocation(t, &invocation, it->expect);
}
