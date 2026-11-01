#include "toolchain/provision.h"

#include "ctx/types.h"
#include "error/error.h"
#include "hash/digest/digest.h"
#include "hash/sha256/sha256.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

static spn_err_t sha256_file(sp_mem_t mem, sp_path_t path, sp_str_t* hex) {
  sp_io_file_reader_t reader = sp_zero;
  if (sp_io_file_reader_from_path_at(&reader, path)) {
    return SPN_ERROR;
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
      return SPN_ERROR;
    }
    if (!bytes_read) {
      break;
    }
  }

  sp_io_file_reader_close(&reader);
  u8 digest [32] = sp_zero;
  spn_sha256_final(&ctx, digest);
  *hex = spn_digest_hex(mem, digest);
  return SPN_OK;
}

spn_err_t spn_fetch_curl(sp_mem_t mem, sp_str_t url, sp_str_t dest, sp_str_t* output) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_ps_output_t result = sp_ps_run(scratch.mem, (sp_ps_config_t) {
    .command = sp_str_lit("curl"),
    .args = {
      sp_str_lit("-fsSL"),
      sp_str_lit("-o"), dest,
      url,
    },
    .io = {
      .err = { .mode = SP_PS_IO_MODE_CREATE },
    },
  });
  *output = sp_str_copy(mem, sp_str_trim(result.err));
  sp_mem_end_scratch(scratch);
  return result.status.exit_code ? SPN_ERROR : SPN_OK;
}

sp_str_t spn_artifact_resolve_url(sp_mem_t mem, spn_artifact_t artifact, sp_str_t mirror) {
  if (sp_str_empty(mirror)) return artifact.url;

  s32 slash = sp_str_find_c8_reverse(artifact.url, '/');
  sp_str_t name = sp_str_suffix(artifact.url, (s32)artifact.url.len - slash - 1);
  if (sp_str_empty(name)) return artifact.url;

  while (mirror.len && mirror.data[mirror.len - 1] == '/') {
    mirror.len--;
  }

  return sp_fmt(mem, "{}/{}", sp_fmt_str(mirror), sp_fmt_str(name)).value;
}

static spn_err_t install(spn_toolchain_store_t* store, sp_mem_t scratch, spn_toolchain_claim_t* claim, sp_str_t name, sp_str_t url) {
  spn_artifact_t artifact = claim->artifact;
  spn_path_t dest = claim->root;
  spn_path_t tarball = claim->tarball;
  sp_path_t tarball_at = spn_path_at(store->roots, tarball);

  sp_str_t actual = sp_zero;
  if (sha256_file(store->mem, tarball_at, &actual)) {
    sp_fs_remove_file_at(tarball_at);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_READ,
      .artifact = {
        .name = name,
        .url = url,
      },
    });
  }
  if (!sp_str_equal(actual, artifact.sha256)) {
    sp_fs_remove_file_at(tarball_at);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_SHA,
      .artifact = {
        .name = name,
        .url = url,
        .expected = artifact.sha256,
        .actual = actual,
      },
    });
  }

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
  sp_ps_output_t extract = sp_ps_run(scratch, (sp_ps_config_t) {
    .command = sp_str_lit("tar"),
    .args = {
      sp_str_lit("xf"), spn_path_str(store->roots, scratch, tarball),
      sp_str_lit("--strip-components=1"),
      sp_str_lit("-C"), spn_path_str(store->roots, scratch, staged),
    },
    .io = {
      .err = { .mode = SP_PS_IO_MODE_CREATE },
    },
  });

  sp_fs_remove_file_at(tarball_at);

  sp_fs_it_t extracted = sp_fs_it_new_at(scratch, work, 0);
  bool empty = !sp_fs_it_next(&extracted);
  sp_fs_it_deinit(&extracted);
  if (extract.status.exit_code || empty) {
    sp_fs_remove_dir_at(work);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .artifact = {
        .name = name,
        .url = url,
        .output = sp_str_copy(spn.events->mem, sp_str_trim(extract.err)),
      },
    });
  }

  sp_path_t target = spn_path_at(store->roots, dest);
  if (sp_sys_rename_s(work.dir, work.sub, target.dir, target.sub)) {
    sp_fs_remove_dir_at(work);
    if (!sp_fs_is_dir_at(target)) {
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

spn_toolchain_claim_t spn_toolchain_claim(spn_toolchain_store_t* store, sp_mem_t mem, spn_artifact_t artifact) {
  spn_toolchain_claim_t claim = {
    .artifact = artifact,
    .root = spn_toolchain_artifact_root(mem, artifact),
  };
  claim.tarball = spn_path_staging(mem, claim.root, sp_str_lit("download"));
  bool locked = sp_fs_lock_acquire(&claim.lock, spn_path_at(store->roots, spn_path_concat(mem, claim.root, ".lock"))) == SP_OK;
  claim.present = locked && sp_fs_is_dir_at(spn_path_at(store->roots, claim.root));
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
