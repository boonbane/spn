#include "toolchain/provision.h"

#include "ctx/types.h"
#include "error/error.h"
#include "event/event.h"
#include "hash/digest/digest.h"
#include "hash/sha256/sha256.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"
#include "toolchain/unpack.h"

spn_err_t spn_toolchain_verify(spn_toolchain_store_t* store, const spn_toolchain_claim_t* claim, sp_mem_t mem, sp_str_t* actual) {
  sp_io_file_reader_t reader = sp_zero;
  if (sp_io_file_reader_from_path_at(&reader, spn_path_at(store->roots, claim->tarball))) {
    return SPN_ERR_TOOLCHAIN_READ;
  }

  spn_sha256_ctx_t ctx = sp_zero;
  spn_sha256_init(&ctx);

  u8 chunk [65536] = sp_zero;
  while (true) {
    u64 bytes_read = 0;
    sp_err_t err = sp_io_read(&reader.base, chunk, sizeof(chunk), &bytes_read);
    if (bytes_read) {
      spn_sha256_update(&ctx, chunk, bytes_read);
    }
    if (err == SP_ERR_IO_EOF) {
      break;
    }
    if (err) {
      sp_io_file_reader_close(&reader);
      return SPN_ERR_TOOLCHAIN_READ;
    }
    if (!bytes_read) {
      break;
    }
  }

  sp_io_file_reader_close(&reader);
  u8 digest [32] = sp_zero;
  spn_sha256_final(&ctx, digest);
  *actual = spn_digest_hex(mem, digest);
  return sp_str_equal(*actual, claim->artifact.sha256) ? SPN_OK : SPN_ERR_TOOLCHAIN_SHA;
}

sp_str_t spn_artifact_resolve_url(sp_mem_t mem, spn_artifact_t artifact, sp_str_t mirror) {
  s32 slash = sp_str_find_c8_reverse(artifact.url, '/');
  sp_str_t name = sp_str_suffix(artifact.url, (s32)artifact.url.len - slash - 1);

  while (mirror.len && mirror.data[mirror.len - 1] == '/') {
    mirror.len--;
  }

  return sp_fmt(mem, "{}/{}", sp_fmt_str(mirror), sp_fmt_str(name)).value;
}

sp_da(sp_str_t) spn_artifact_mirror_urls(sp_mem_t mem, spn_artifact_t artifact, sp_str_t listing, u64 seed) {
  sp_da(sp_str_t) urls = sp_da_new(mem, sp_str_t);
  sp_str_pair_t lines = { .second = listing };
  while (!sp_str_empty(lines.second)) {
    lines = sp_str_cleave_c8(lines.second, '\n');
    sp_str_t mirror = sp_str_trim(lines.first);
    if (!sp_str_empty(mirror)) {
      sp_da_push(urls, sp_str_concat(mem, spn_artifact_resolve_url(mem, artifact, mirror), sp_str_lit("?source=spn")));
    }
  }

  sp_da_for(urls, it) {
    u64 pick = it + sp_hash_bytes(&it, sizeof(it), seed) % (sp_da_size(urls) - it);
    sp_swap(sp_str_t, urls[it], urls[pick]);
  }
  return urls;
}

static bool empty(sp_path_t root) {
  sp_sys_dir_t dir = sp_zero;
  if (sp_fs_open_dir_at(root, &dir)) {
    return true;
  }
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF];
  sp_fs_dir_it_t it = sp_fs_dir_it(dir);
  sp_fs_dir_entry_t entry = sp_zero;
  bool found = sp_fs_dir_it_next(&it, sp_mem_slice(buf, sizeof(buf)), &entry);
  sp_fs_dir_it_close(&it);
  sp_fs_close_dir(dir);
  return !found;
}

static spn_err_t install(spn_toolchain_store_t* store, sp_mem_t scratch, spn_toolchain_claim_t* claim, sp_str_t name, sp_str_t url) {
  spn_path_t dest = claim->root;
  sp_path_t tarball_at = spn_path_at(store->roots, claim->tarball);

  spn_path_t staged = sp_zero;
  if (spn_path_stage_dir(scratch, store->roots, dest, sp_str_lit("tmp"), &staged)) {
    sp_fs_remove_file_at(tarball_at);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .artifact = {
        .name = name,
        .url = url,
      },
    });
  }

  sp_path_t work = spn_path_at(store->roots, staged);
  spn_unpack_failure_t failure = sp_zero;
  spn_err_t err = spn_artifact_unpack(scratch, tarball_at, work, &failure);
  if (!err && empty(work)) {
    failure.reason = SPN_UNPACK_EMPTY;
    err = SPN_ERROR;
  }

  sp_fs_remove_file_at(tarball_at);

  if (err) {
    sp_fs_remove_dir_at(work);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .artifact = {
        .name = name,
        .url = url,
        .unpack = failure.reason,
        .entry = sp_str_copy(spn.events->mem, failure.entry),
      },
    });
  }

  sp_path_t target = spn_path_at(store->roots, dest);
  if (sp_sys_rename_s(work.dir, work.sub, target.dir, target.sub)) {
    sp_fs_remove_dir_at(work);
    sp_sys_file_meta_t meta = sp_zero;
    if (sp_fs_get_link_metadata_at(target, &meta) || meta.kind != SP_FS_KIND_DIR) {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
        .artifact = {
          .name = name,
          .url = url,
        },
      });
    }
  }

  return SPN_OK;
}

spn_toolchain_claim_t spn_toolchain_claim(spn_toolchain_store_t* store, sp_mem_t mem, sp_str_t name, spn_artifact_t artifact) {
  spn_toolchain_claim_t claim = {
    .artifact = artifact,
    .root = spn_toolchain_artifact_root(mem, artifact),
  };
  claim.tarball = spn_path_staging(mem, claim.root, sp_str_lit("download"));
  sp_path_t root = spn_path_at(store->roots, claim.root);
  sp_sys_file_meta_t meta = sp_zero;
  claim.present = !sp_fs_get_link_metadata_at(root, &meta) && meta.kind == SP_FS_KIND_DIR;
  if (claim.present) {
    return claim;
  }

  sp_path_t lock_at = spn_path_at(store->roots, spn_path_lock(mem, claim.root));
  sp_fs_create_parent_at(lock_at);
  bool acquired = false;
  sp_err_t locked = sp_fs_lock_try_acquire(&claim.lock, lock_at, &acquired);
  if (!locked && !acquired) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LOCK_WAIT,
      .lock_wait = { .lock = SPN_FILE_LOCK_TOOLCHAIN, .name = name },
    });
    locked = sp_fs_lock_acquire(&claim.lock, lock_at);
  }
  claim.present = !locked && !sp_fs_get_link_metadata_at(root, &meta) && meta.kind == SP_FS_KIND_DIR;
  if (claim.present) {
    sp_fs_lock_release(&claim.lock);
  }
  return claim;
}

void spn_toolchain_release(spn_toolchain_store_t* store, spn_toolchain_claim_t* claim) {
  sp_fs_remove_file_at(spn_path_at(store->roots, claim->tarball));
  sp_fs_lock_release(&claim->lock);
}

spn_err_t spn_toolchain_install(spn_toolchain_store_t* store, spn_toolchain_claim_t* claim, sp_str_t name, sp_str_t url) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t result = install(store, scratch.mem, claim, name, url);
  sp_fs_lock_release(&claim->lock);
  sp_mem_end_scratch(scratch);
  return result;
}
