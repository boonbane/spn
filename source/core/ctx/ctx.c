#include "spn/host.h"

#include "ctx/ctx.h"
#include "dag/types.h"
#include "elf/elf.h"
#include "error/error.h"
#include "intern/intern.h"

spn_ctx_t spn;

sp_intern_t* spn_ctx_get_intern() {
  return spn.intern;
}

bool spn_ctx_progress(spn_ctx_t* ctx, spn_progress_t* progress) {
  spn_dag_progress_t* dag = (spn_dag_progress_t*)sp_atomic_ptr_load(&ctx->progress, SP_ATOMIC_SEQ_CST);
  if (!dag) {
    return false;
  }

  *progress = (spn_progress_t) {
    .total = (u32)sp_atomic_s32_load(&dag->total, SP_ATOMIC_SEQ_CST),
    .completed = (u32)sp_atomic_s32_load(&dag->completed, SP_ATOMIC_SEQ_CST),
    .hits = (u32)sp_atomic_s32_load(&dag->hits, SP_ATOMIC_SEQ_CST),
    .misses = (u32)sp_atomic_s32_load(&dag->misses, SP_ATOMIC_SEQ_CST),
    .warm = sp_atomic_u64_load(&dag->warm, SP_ATOMIC_SEQ_CST),
  };
  return true;
}

spn_err_t spn_ctx_require_project(spn_ctx_t* ctx) {
  if (!ctx->project) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_NO_MANIFEST,
      .no_manifest = { .path = ctx->roots.dirs[SPN_PATH_ROOT_PROJECT] },
    });
  }
  return SPN_OK;
}

spn_index_info_t* spn_find_index(spn_ctx_t* ctx, sp_str_t name) {
  if (sp_str_empty(name)) {
    name = sp_str_lit("core");
  }
  spn_index_id_t id = sp_intern(ctx->intern, name).id;
  sp_da_for(ctx->indexes, it) {
    if (ctx->indexes[it].id == id) {
      return &ctx->indexes[it];
    }
  }
  return SP_NULLPTR;
}

sp_intern_str_t spn_intern(sp_str_t str) {
  return sp_intern(spn_ctx_get_intern(), str);
}

sp_intern_str_t spn_intern_cstr(const c8* cstr) {
  return sp_intern_cstr(spn_ctx_get_intern(), cstr);
}

sp_str_t spn_intern_find(sp_intern_id_t id) {
  return sp_intern_find(spn_ctx_get_intern(), id);
}

spn_triple_t spn_host_detect() {
  spn_triple_t host = sp_zero;

#if defined(SP_AMD64)
  host.arch = SPN_ARCH_X64;
#elif defined(SP_ARM64)
  host.arch = SPN_ARCH_ARM64;
#endif

#if defined(SP_LINUX)
  host.os = SPN_OS_LINUX;
  #if defined(__GLIBC__)
    host.abi = SPN_ABI_GNU;
  #else
    host.abi = SPN_ABI_MUSL;
  #endif

  sp_io_file_reader_t file = sp_zero;
  if (!sp_io_file_reader_from_path_at(&file, sp_path_from_str(sp_str_lit("/bin/sh")))) {
    sp_io_seeking_reader_t elf = sp_zero;
    sp_io_seeking_reader_from_file_reader(&elf, &file);
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    sp_str_t interp = sp_zero;
    if (!spn_elf_interp(scratch.mem, &elf, &interp)) {
      if (sp_str_find(interp, sp_str_lit("ld-musl")) >= 0) {
        host.abi = SPN_ABI_MUSL;
      }
      else if (sp_str_find(interp, sp_str_lit("ld-linux")) >= 0) {
        host.abi = SPN_ABI_GNU;
      }
    }
    sp_mem_end_scratch(scratch);
    sp_io_file_reader_close(&file);
  }
#elif defined(SP_MACOS)
  host.os = SPN_OS_MACOS;
  host.abi = SPN_ABI_APPLE;
#elif defined(SP_WIN32)
  host.os = SPN_OS_WINDOWS;
#endif

  return host;
}
