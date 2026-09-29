#include "toolchain/provision.h"

#include "ctx/types.h"
#include "error/error.h"
#include "hash/digest/digest.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "toolchain/toolchain.h"

spn_err_t spn_fetch_curl(spn_toolchain_store_t* store, sp_str_t url, sp_str_t dest, sp_str_t* output) {
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
  *output = sp_str_copy(store->mem, sp_str_trim(result.err));
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

static spn_err_t fetch(spn_toolchain_store_t* store, spn_artifact_t artifact, sp_str_t dest, sp_str_t* url, sp_str_t* output) {
  if (!sp_str_empty(store->mirror)) {
    *url = spn_artifact_resolve_url(store->mem, artifact, store->mirror);
    if (!sp_str_equal(*url, artifact.url)) {
      if (store->fetch(store, *url, dest, output) == SPN_OK) return SPN_OK;
    }
  }

  *url = artifact.url;
  return store->fetch(store, *url, dest, output);
}

static spn_err_t fill(spn_toolchain_store_t* store, sp_str_t name, spn_artifact_t artifact, spn_path_t dest) {
  sp_mem_t mem = store->mem;
  spn_path_t tarball = { .root = dest.root, .sub = sp_fs_staging_path(mem, dest.sub, sp_str_lit("download")) };
  sp_path_t tarball_at = spn_path_at(store->roots, tarball);

  sp_str_t url = sp_zero;
  sp_str_t output = sp_zero;
  if (fetch(store, artifact, spn_path_str(store->roots, mem, tarball), &url, &output)) {
    sp_fs_remove_file_at(tarball_at);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_FETCH,
      .artifact = {
        .name = name,
        .url = url,
        .output = output,
      },
    });
  }

  sp_str_t actual = sp_zero;
  if (spn_digest_file_hex(SPN_DIGEST_SHA256, mem, tarball_at, &actual)) {
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

  sp_path_t target = spn_path_at(store->roots, dest);
  sp_path_t work = sp_zero;
  if (sp_fs_staging_dir(mem, target, sp_str_lit("tmp"), &work)) {
    sp_fs_remove_file_at(tarball_at);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .artifact = {
        .name = name,
        .url = url,
      },
    });
  }

  spn_path_t staged = { .root = dest.root, .sub = work.sub };
  sp_ps_output_t extract = sp_ps_run(mem, (sp_ps_config_t) {
    .command = sp_str_lit("tar"),
    .args = {
      sp_str_lit("xf"), spn_path_str(store->roots, mem, tarball),
      sp_str_lit("--strip-components=1"),
      sp_str_lit("-C"), spn_path_str(store->roots, mem, staged),
    },
    .io = {
      .err = { .mode = SP_PS_IO_MODE_CREATE },
    },
  });

  sp_fs_remove_file_at(tarball_at);

  sp_fs_it_t extracted = sp_fs_it_new_at(mem, work, 0);
  bool empty = !sp_fs_it_next(&extracted);
  sp_fs_it_deinit(&extracted);
  if (extract.status.exit_code || empty) {
    sp_fs_remove_dir_at(work);
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .artifact = {
        .name = name,
        .url = url,
        .output = sp_str_trim(extract.err),
      },
    });
  }

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

spn_err_t spn_toolchain_provision(spn_toolchain_store_t* store, sp_str_t name, spn_artifact_t artifact) {
  spn_path_t dest = spn_toolchain_artifact_root(artifact);

  sp_fs_lock_t lock = sp_zero;
  bool locked = sp_fs_lock_acquire(&lock, spn_path_at(store->roots, spn_path_suffix(store->mem, dest, sp_str_lit(".lock")))) == SP_OK;

  if (locked && sp_fs_is_dir_at(spn_path_at(store->roots, dest))) {
    sp_fs_lock_release(&lock);
    return SPN_OK;
  }

  spn_err_t result = fill(store, name, artifact, dest);
  sp_fs_lock_release(&lock);
  return result;
}
