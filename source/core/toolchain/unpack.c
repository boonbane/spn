#include "toolchain/unpack.h"

#include "external/gzip.h"
#include "external/xz.h"
#include "tar/tar.h"

#define spn_unpack_chunk_size (64 * 1024)

typedef enum {
  PLACE_ABOVE,
  PLACE_INSIDE,
  PLACE_UNSAFE,
} place_t;

typedef struct {
  sp_mem_t mem;
  sp_sys_dir_t root;
  sp_str_ht(bool) links;
  u8* chunk;
  spn_unpack_failure_t* failure;
} unpack_t;

static spn_err_t fail(unpack_t* unpack, spn_unpack_reason_t reason, sp_str_t entry) {
  *unpack->failure = (spn_unpack_failure_t) {
    .reason = reason,
    .entry = sp_str_copy(unpack->mem, entry),
  };
  return SPN_ERROR;
}

static place_t place(sp_io_mem_writer_t* names, sp_str_t path, sp_str_t* rel) {
  u64 start = names->pos;
  bool top = true;
  sp_str_pair_t parts = { .second = path };
  while (!sp_str_empty(parts.second)) {
    parts = sp_str_cleave_c8(parts.second, '/');
    sp_str_t part = parts.first;
    if (sp_str_empty(part) || sp_str_equal_cstr(part, ".")) {
      continue;
    }
    if (sp_str_equal_cstr(part, "..")) {
      return PLACE_UNSAFE;
    }
    sp_for(it, part.len) {
      if (sp_fs_is_sep(part.data[it])) {
        return PLACE_UNSAFE;
      }
    }
    if (top) {
      top = false;
      continue;
    }
    if (names->pos > start) {
      sp_io_write_c8(&names->base, '/');
    }
    sp_io_write_str(&names->base, part, SP_NULLPTR);
  }

  *rel = sp_str((const c8*)names->ptr + start, (u32)(names->pos - start));
  return sp_str_empty(*rel) ? PLACE_ABOVE : PLACE_INSIDE;
}

static bool crosses_link(unpack_t* unpack, sp_str_t rel) {
  if (sp_str_ht_empty(unpack->links)) {
    return false;
  }
  sp_for(it, rel.len + 1) {
    if (it < rel.len && rel.data[it] != '/') {
      continue;
    }
    sp_str_t prefix = sp_str_prefix(rel, it);
    if (sp_str_ht_get(unpack->links, prefix)) {
      return true;
    }
  }
  return false;
}

static sp_err_t open_file(sp_path_t at, sp_io_file_writer_t* file) {
  sp_err_t err = sp_io_file_writer_from_path_at(file, at);
  if (err != SP_ERR_SYS_NOT_FOUND) {
    return err;
  }
  sp_try(sp_fs_create_parent_at(at));
  return sp_io_file_writer_from_path_at(file, at);
}

static sp_err_t mark_executable(sp_path_t at) {
  sp_sys_file_meta_t meta = sp_zero;
  sp_try(sp_sys_get_path_metadata_s(at.dir, at.sub, &meta));
  sp_sys_set_executable(&meta.perms, true);
  return sp_sys_set_file_perms_s(at.dir, at.sub, meta.perms);
}

static sp_fs_kind_t link_kind(unpack_t* unpack, sp_io_mem_writer_t* names, sp_str_t rel, sp_str_t link) {
  u64 start = names->pos;
  s32 slash = sp_str_find_c8_reverse(rel, '/');
  if (slash >= 0) {
    sp_io_write_str(&names->base, sp_str_prefix(rel, (u32)slash + 1), SP_NULLPTR);
  }
  sp_io_write_str(&names->base, link, SP_NULLPTR);
  sp_path_t target = { .dir = unpack->root, .sub = sp_str((const c8*)names->ptr + start, (u32)(names->pos - start)) };
  sp_sys_file_meta_t meta = sp_zero;
  return (!sp_fs_get_link_metadata_at(target, &meta) && meta.kind == SP_FS_KIND_DIR) ? SP_FS_KIND_DIR : SP_FS_KIND_FILE;
}

static spn_err_t untar(unpack_t* unpack, sp_io_reader_t* reader) {
  sp_tar_it_t tar = sp_tar_it(reader);
  sp_tar_entry_t entry = sp_zero;
  u8 names [2 * SP_PATH_MAX] = sp_zero;
  u8 placed [2 * SP_PATH_MAX] = sp_zero;

  while (sp_tar_it_next(&tar, sp_mem_slice(names, sizeof(names)), &entry)) {
    sp_io_mem_writer_t rels = sp_zero;
    sp_io_mem_writer_from_buffer(&rels, placed, sizeof(placed));

    sp_str_t rel = sp_zero;
    switch (place(&rels, entry.path, &rel)) {
      case PLACE_ABOVE: {
        continue;
      }
      case PLACE_UNSAFE: {
        return fail(unpack, SPN_UNPACK_UNSAFE, entry.path);
      }
      case PLACE_INSIDE: {
        break;
      }
    }
    if (crosses_link(unpack, rel)) {
      return fail(unpack, SPN_UNPACK_UNSAFE, entry.path);
    }

    sp_path_t at = { .dir = unpack->root, .sub = rel };
    switch (entry.kind) {
      case SP_TAR_DIR: {
        if (sp_fs_create_dir_at(at)) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }
        break;
      }
      case SP_TAR_FILE: {
        sp_io_file_writer_t file = sp_zero;
        if (open_file(at, &file)) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }

        sp_err_t read = SP_OK;
        sp_err_t written = SP_OK;
        while (!read && !written) {
          u64 size = 0;
          read = sp_io_read(&tar.content.base, unpack->chunk, spn_unpack_chunk_size, &size);
          written = sp_io_write_all(&file.base, unpack->chunk, size, SP_NULLPTR);
        }
        sp_err_t closed = sp_io_file_writer_close(&file);

        if (written || closed) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }
        if (read != SP_ERR_IO_EOF || tar.content.remaining) {
          return fail(unpack, SPN_UNPACK_CORRUPT, entry.path);
        }
        if ((entry.mode & 0111) && mark_executable(at)) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }
        break;
      }
      case SP_TAR_SYM_LINK: {
        sp_fs_kind_t kind = link_kind(unpack, &rels, rel, entry.link);
        if (sp_fs_create_parent_at(at) || sp_fs_create_sym_link_at(entry.link, at, kind)) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }
        sp_str_ht_insert(unpack->links, sp_str_copy(unpack->mem, rel), true);
        break;
      }
      case SP_TAR_HARD_LINK: {
        sp_str_t target = sp_zero;
        if (place(&rels, entry.link, &target) != PLACE_INSIDE || crosses_link(unpack, target)) {
          return fail(unpack, SPN_UNPACK_UNSAFE, entry.path);
        }
        sp_path_t existing = { .dir = unpack->root, .sub = target };
        if (sp_fs_create_parent_at(at) || sp_fs_create_hard_link_at(existing, at)) {
          return fail(unpack, SPN_UNPACK_UNWRITABLE, entry.path);
        }
        break;
      }
    }
  }

  if (tar.err == SP_ERR_SYS_UNSUPPORTED) {
    return fail(unpack, SPN_UNPACK_UNSUPPORTED, sp_str_lit(""));
  }
  if (tar.err) {
    return fail(unpack, SPN_UNPACK_CORRUPT, sp_str_lit(""));
  }
  return SPN_OK;
}

spn_err_t spn_artifact_unpack(sp_mem_t mem, sp_path_t archive, sp_path_t root, spn_unpack_failure_t* failure) {
  *failure = sp_zero_s(spn_unpack_failure_t);
  unpack_t unpack = {
    .mem = mem,
    .chunk = (u8*)sp_alloc(mem, spn_unpack_chunk_size),
    .failure = failure,
  };
  sp_str_ht_init(mem, unpack.links);

  sp_io_file_reader_t file = sp_zero;
  if (sp_io_file_reader_from_path_at(&file, archive)) {
    return fail(&unpack, SPN_UNPACK_UNREADABLE, sp_str_lit(""));
  }
  sp_io_reader_set_buffer(&file.base, (u8*)sp_alloc(mem, spn_unpack_chunk_size), spn_unpack_chunk_size);
  if (sp_fs_open_dir_at(root, &unpack.root)) {
    sp_io_file_reader_close(&file);
    return fail(&unpack, SPN_UNPACK_UNWRITABLE, sp_str_lit(""));
  }

  u8* decoded = (u8*)sp_alloc(mem, spn_unpack_chunk_size);
  sp_str_t head = sp_zero;
  sp_io_peek(&file.base, &head);

  spn_err_t err = SPN_OK;
  if (spn_xz_is_stream(head)) {
    spn_xz_reader_t xz = sp_zero;
    if (spn_xz_reader_init(&xz, mem, &file.base)) {
      err = fail(&unpack, SPN_UNPACK_UNREADABLE, sp_str_lit(""));
    }
    else {
      sp_io_reader_set_buffer(&xz.base, decoded, spn_unpack_chunk_size);
      err = untar(&unpack, &xz.base);
      spn_xz_reader_close(&xz);
    }
  }
  else if (spn_gzip_is_stream(head)) {
    spn_gzip_reader_t gzip = sp_zero;
    if (spn_gzip_reader_init(&gzip, mem, &file.base)) {
      err = fail(&unpack, SPN_UNPACK_UNREADABLE, sp_str_lit(""));
    }
    else {
      sp_io_reader_set_buffer(&gzip.base, decoded, spn_unpack_chunk_size);
      err = untar(&unpack, &gzip.base);
      spn_gzip_reader_close(&gzip);
    }
  }
  else {
    err = fail(&unpack, SPN_UNPACK_UNRECOGNIZED, sp_str_lit(""));
  }

  sp_fs_close_dir(unpack.root);
  sp_io_file_reader_close(&file);
  return err;
}
