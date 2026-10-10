#include "sp.h"
#include "ctx/types.h"
#include "error/error.h"
#include "intern/intern.h"
#include "macro/macro.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

#define SPN_GENERATION_ATTEMPTS 100

typedef enum {
  GENERATION_READ,
  GENERATION_EMPTY,
  GENERATION_GONE,
  GENERATION_INVALID,
} generation_read_t;

typedef struct {
  spn_err_t kind;
  spn_path_t path;
} generation_err_t;

spn_path_t spn_toolchain_artifact_root(sp_mem_t mem, spn_artifact_t artifact) {
  return spn_path_s(mem, SPN_DIR_ID_TOOLCHAIN_STORE, artifact.sha256);
}

spn_toolchain_launcher_t spn_toolchain_launcher_with_root(sp_mem_t mem, spn_toolchain_launcher_t launcher, spn_path_t root) {
  sp_assert(spn_path_empty(launcher.program.path));
  sp_str_t name = launcher.program.prefix;
#if defined(SP_WIN32)
  name = sp_fmt(mem, "{}.exe", sp_fmt_str(name)).value;
#endif

  spn_toolchain_launcher_t result = launcher;
  result.program = spn_arg_path(spn_path_join(mem, root, name));
  return result;
}

static sp_hash_t nonce(void) {
  sp_tm_epoch_t now = sp_tm_now_epoch();
  return sp_hash_bytes(&now, sizeof(now), 0);
}

static generation_err_t create_generation(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t file, sp_hash_t* generation, bool* created) {
  sp_path_t at = spn_path_at(roots, file);
  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_err_t err = sp_sys_open_s(at.dir, at.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_EXCLUSIVE, &fd);
  if (err == SP_ERR_SYS_EXISTS) {
    *created = false;
    return sp_zero_struct(generation_err_t);
  }
  if (err) {
    return (generation_err_t) { SPN_ERR_FS_WRITE, file };
  }

  *created = true;
  *generation = nonce();
  sp_str_t text = sp_fmt(mem, "{}\n", sp_fmt_uint(*generation)).value;
  u64 written = 0;
  err = sp_sys_write(fd, text.data, text.len, &written);
  sp_sys_close(fd);
  if (err || written != text.len) {
    sp_fs_remove_file_at(at);
    return (generation_err_t) { SPN_ERR_FS_WRITE, file };
  }
  return sp_zero_struct(generation_err_t);
}

static generation_read_t read_generation(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t file, sp_hash_t* generation) {
  sp_str_t content = sp_zero;
  if (sp_io_read_file_at(mem, spn_path_at(roots, file), &content)) {
    return GENERATION_GONE;
  }
  content = sp_str_trim(content);
  if (sp_str_empty(content)) {
    return GENERATION_EMPTY;
  }
  return sp_parse_u64_ex(content, generation) ? GENERATION_READ : GENERATION_INVALID;
}

static generation_err_t claim_generation(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t dir, spn_path_t file, sp_hash_t* generation) {
  sp_for(attempt, SPN_GENERATION_ATTEMPTS) {
    if (sp_fs_create_dir_at(spn_path_at(roots, dir))) {
      return (generation_err_t) { SPN_ERR_FS_CREATE_DIR, dir };
    }
    bool created = false;
    generation_err_t err = create_generation(roots, mem, file, generation, &created);
    if (err.kind) {
      return err;
    }
    if (created) {
      return sp_zero_struct(generation_err_t);
    }
    switch (read_generation(roots, mem, file, generation)) {
      case GENERATION_READ: {
        return sp_zero_struct(generation_err_t);
      }
      case GENERATION_EMPTY: {
        sp_os_sleep_ms(1);
        break;
      }
      case GENERATION_GONE: {
        break;
      }
      case GENERATION_INVALID: {
        return (generation_err_t) { SPN_ERR_FS_READ, file };
      }
    }
  }
  return (generation_err_t) { SPN_ERR_FS_READ, file };
}

spn_err_t spn_toolchain_generation(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t cache, sp_hash_t* generation) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  spn_path_t dir = spn_path_join(scratch.mem, cache, sp_str_lit("spn"));
  spn_path_t file = spn_path_join(scratch.mem, dir, sp_str_lit("generation"));
  generation_err_t err = claim_generation(roots, scratch.mem, dir, file, generation);
  spn_err_t result = SPN_OK;
  if (err.kind) {
    result = spn_err_emit(&spn, (spn_err_union_t) { .kind = err.kind, .fs = { .path = spn_path_copy(mem, err.path) } });
  }
  sp_mem_end_scratch(scratch);
  return result;
}

static spn_path_check_t local_arg(spn_path_root_t base, sp_str_t str, spn_arg_t* arg) {
  if (sp_fs_is_absolute(str)) {
    *arg = spn_arg_path((spn_path_t) { .sub = str });
    return SPN_PATH_OK;
  }
  if (base == SPN_PATH_ROOT_NONE) {
    return SPN_PATH_UNROOTED;
  }
  *arg = spn_arg_path((spn_path_t) { .root = base, .sub = str });
  return SPN_PATH_OK;
}

spn_path_check_t spn_toolchain_sdk_path(spn_toolchain_source_t source, spn_path_root_t base, sp_str_t str, spn_arg_t* sdk) {
  if (!spn_path_normal(str)) {
    return SPN_PATH_MALFORMED;
  }
  switch (source) {
    case SPN_TOOLCHAIN_SOURCE_LOCAL: {
      return local_arg(base, str, sdk);
    }
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION:
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      *sdk = sp_fs_is_absolute(str) ? spn_arg_path((spn_path_t) { .sub = str }) : spn_arg_lit(str);
      return SPN_PATH_OK;
    }
  }
  SP_UNREACHABLE_RETURN(SPN_PATH_MALFORMED);
}

spn_path_check_t spn_toolchain_program(spn_toolchain_source_t source, spn_path_root_t base, sp_str_t program, spn_arg_t* arg) {
  if (!spn_path_normal(program)) {
    return SPN_PATH_MALFORMED;
  }
  switch (source) {
    case SPN_TOOLCHAIN_SOURCE_LOCAL: {
      if (sp_fs_get_name(program).len == program.len) {
        *arg = spn_arg_lit(program);
        return SPN_PATH_OK;
      }
      return local_arg(base, program, arg);
    }
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION:
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      if (sp_fs_is_absolute(program)) {
        return SPN_PATH_ABSOLUTE;
      }
      *arg = spn_arg_lit(program);
      return SPN_PATH_OK;
    }
  }
  SP_UNREACHABLE_RETURN(SPN_PATH_MALFORMED);
}

spn_wasi_spelling_t spn_toolchain_wasi_spelling(const spn_path_roots_t* roots, sp_mem_t mem, const spn_toolchain_info_t* toolchain) {
  sp_da_for(toolchain->rows, it) {
    if (toolchain->rows[it].triple.os == SPN_OS_WASI) {
      return spn_sdk_wasi_spelling(roots, mem, &toolchain->rows[it].sdk);
    }
  }
  return SPN_WASI_SPELLING_WASI;
}

bool spn_toolchain_has_cxx(spn_toolchain_info_t* toolchain) {
  return !spn_arg_empty(toolchain->cxx.program);
}

spn_toolchain_ref_kind_t spn_toolchain_ref_kind(sp_str_t str) {
  if (sp_str_empty(str)) {
    return SPN_TOOLCHAIN_REF_NONE;
  }
  if (sp_str_equal_cstr(str, "auto")) {
    return SPN_TOOLCHAIN_REF_AUTO;
  }
  return SPN_TOOLCHAIN_REF_NAMED;
}

spn_toolchain_ref_t spn_toolchain_ref_from_str(sp_str_t str) {
  spn_toolchain_ref_t ref = { .kind = spn_toolchain_ref_kind(str) };
  if (ref.kind == SPN_TOOLCHAIN_REF_NAMED) {
    ref.name = spn_intern(str);
  }
  return ref;
}

spn_cc_cap_set_t spn_toolchain_driver_caps(spn_cc_driver_t driver) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_BARE | SPN_CC_CAP_GNU_RUNTIME;
    case SPN_CC_DRIVER_CLANG: return SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_LLVM_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_GNU_RUNTIME;
    case SPN_CC_DRIVER_ZIG: return SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_CODEVIEW | SPN_CC_CAP_LIBC_FILE | SPN_CC_CAP_DEFAULT_UBSAN | SPN_CC_CAP_BARE;
    case SPN_CC_DRIVER_MSVC: return 0;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(0);
}

bool spn_toolchain_driver_retargets(spn_cc_driver_t driver) {
  return spn_toolchain_driver_caps(driver) & SPN_CC_CAP_TARGET_TRIPLE;
}

static spn_sanitizer_set_t gcc_stock_sanitizers(spn_triple_t host) {
  switch (host.os) {
    case SPN_OS_LINUX: return host.abi == SPN_ABI_MUSL ? 0 : SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK;
    case SPN_OS_MACOS: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED;
    case SPN_OS_WINDOWS:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: return 0;
  }
  SP_UNREACHABLE_RETURN(0);
}

static spn_sanitizer_set_t clang_stock_sanitizers(spn_triple_t host) {
  switch (host.os) {
    case SPN_OS_LINUX: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_MEMORY | SPN_SANITIZER_LEAK;
    case SPN_OS_MACOS: return SPN_SANITIZER_ADDRESS | SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED | SPN_SANITIZER_LEAK;
    case SPN_OS_WINDOWS:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: return 0;
  }
  SP_UNREACHABLE_RETURN(0);
}

spn_sanitizer_set_t spn_toolchain_stock_sanitizers(spn_cc_driver_t driver, spn_triple_t host) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return gcc_stock_sanitizers(host);
    case SPN_CC_DRIVER_CLANG: return clang_stock_sanitizers(host);
    case SPN_CC_DRIVER_ZIG:
    case SPN_CC_DRIVER_MSVC: return 0;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(0);
}

spn_abi_t spn_default_abi(spn_cc_driver_t driver, spn_os_t os) {
  switch (os) {
    case SPN_OS_LINUX:
    case SPN_OS_FREESTANDING: return SPN_ABI_NONE;
    case SPN_OS_MACOS: return SPN_ABI_APPLE;
    case SPN_OS_WASI: return SPN_ABI_MUSL;
    case SPN_OS_WINDOWS: {
      switch (driver) {
        case SPN_CC_DRIVER_GCC:
        case SPN_CC_DRIVER_ZIG: return SPN_ABI_GNU;
        case SPN_CC_DRIVER_MSVC: return SPN_ABI_MSVC;
        case SPN_CC_DRIVER_CLANG: return SPN_ABI_NONE;
        case SPN_CC_DRIVER_NONE: sp_unreachable_case();
      }
      sp_unreachable_return(SPN_ABI_NONE);
    }
    case SPN_OS_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(SPN_ABI_NONE);
}

bool spn_toolchain_driver_composes(spn_cc_driver_t driver, spn_ld_dialect_t dialect) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: return dialect == SPN_LD_DIALECT_GNU || dialect == SPN_LD_DIALECT_DARWIN;
    case SPN_CC_DRIVER_MSVC: return dialect == SPN_LD_DIALECT_LINK;
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: return true;
    case SPN_CC_DRIVER_NONE: sp_unreachable_case();
  }
  SP_UNREACHABLE_RETURN(false);
}
