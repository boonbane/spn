#include "sp.h"
#include "zip.h"

#include <minizip/unzip.h>

#define spn_zip_host_unix 3
#define spn_zip_mode_kind 0170000
#define spn_zip_mode_link 0120000

static voidpf open_file(voidpf opaque, const void* path, int mode) {
  sp_unused(path);
  sp_unused(mode);
  return opaque;
}

static uLong read_file(voidpf opaque, voidpf stream, void* buf, uLong size) {
  sp_unused(stream);
  spn_zip_it_t* it = (spn_zip_it_t*)opaque;
  u64 total = 0;
  while (total < size) {
    u64 got = 0;
    if (sp_sys_pread(it->file, (u8*)buf + total, size - total, it->pos + total, &got) || !got) {
      break;
    }
    total += got;
  }
  it->pos += total;
  it->cut |= total < size;
  return (uLong)total;
}

static ZPOS64_T tell_file(voidpf opaque, voidpf stream) {
  sp_unused(stream);
  return ((spn_zip_it_t*)opaque)->pos;
}

static long seek_file(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
  sp_unused(stream);
  spn_zip_it_t* it = (spn_zip_it_t*)opaque;
  switch (origin) {
    case ZLIB_FILEFUNC_SEEK_SET: {
      it->pos = offset;
      break;
    }
    case ZLIB_FILEFUNC_SEEK_CUR: {
      it->pos += offset;
      break;
    }
    case ZLIB_FILEFUNC_SEEK_END: {
      it->pos = it->size + offset;
      break;
    }
  }
  return 0;
}

static int close_file(voidpf opaque, voidpf stream) {
  sp_unused(opaque);
  sp_unused(stream);
  return 0;
}

static int error_file(voidpf opaque, voidpf stream) {
  sp_unused(stream);
  return ((spn_zip_it_t*)opaque)->cut;
}

static sp_err_t read_content(sp_io_reader_t* reader, void* ptr, u64 size, u64* bytes_read) {
  spn_zip_it_t* it = (spn_zip_it_t*)reader;
  *bytes_read = 0;

  s32 got = unzReadCurrentFile(it->archive, ptr, (u32)sp_min(size, (u64)SP_LIMIT_S32_MAX));
  if (got < 0) {
    return SP_ERR;
  }
  if (!got) {
    it->open = false;
    return unzCloseCurrentFile(it->archive) ? SP_ERR : SP_ERR_IO_EOF;
  }

  *bytes_read = (u64)got;
  return SP_OK;
}

static bool stop(spn_zip_it_t* it, sp_err_t err) {
  it->err = err;
  return false;
}

bool spn_zip_is_archive(sp_str_t head) {
  return sp_str_starts_with(head, sp_str_lit("PK\x03\x04"));
}

spn_err_t spn_zip_it_init(spn_zip_it_t* it, sp_io_file_reader_t* file) {
  u64 size = 0;
  if (sp_io_file_reader_size(file, &size)) {
    return SPN_ERROR;
  }
  *it = (spn_zip_it_t) {
    .content = { .read = read_content },
    .file = file->file,
    .size = size,
  };

  zlib_filefunc64_def io = {
    .zopen64_file = open_file,
    .zread_file = read_file,
    .ztell64_file = tell_file,
    .zseek64_file = seek_file,
    .zclose_file = close_file,
    .zerror_file = error_file,
    .opaque = it,
  };
  it->archive = unzOpen2_64(it, &io);
  if (!it->archive) {
    return SPN_ERROR;
  }

  unz_global_info64 global = sp_zero;
  unzGetGlobalInfo64(it->archive, &global);
  it->entries = global.number_entry;
  return SPN_OK;
}

bool spn_zip_it_next(spn_zip_it_t* it, sp_mem_slice_t buf, spn_zip_entry_t* entry) {
  if (it->open) {
    unzCloseCurrentFile(it->archive);
    it->open = false;
  }
  if (it->err || it->index == it->entries) {
    return false;
  }
  if (it->index ? unzGoToNextFile(it->archive) : unzGoToFirstFile(it->archive)) {
    return stop(it, SP_ERR);
  }
  it->index++;

  unz_file_info64 info = sp_zero;
  if (unzGetCurrentFileInfo64(it->archive, &info, (c8*)buf.data, (uLong)buf.len, SP_NULLPTR, 0, SP_NULLPTR, 0)) {
    return stop(it, SP_ERR);
  }
  if (info.size_filename >= buf.len) {
    return stop(it, SP_ERR_IO_NO_SPACE);
  }
  if (info.compression_method && info.compression_method != Z_DEFLATED) {
    return stop(it, SP_ERR_SYS_UNSUPPORTED);
  }

  sp_str_t path = sp_str((const c8*)buf.data, (u32)info.size_filename);
  u32 mode = (info.version >> 8) == spn_zip_host_unix ? (u32)(info.external_fa >> 16) : 0;
  *entry = (spn_zip_entry_t) {
    .kind = SPN_ZIP_DIR,
    .path = path,
    .size = info.uncompressed_size,
    .mode = mode,
  };
  if (sp_str_ends_with(path, sp_str_lit("/"))) {
    return true;
  }

  if (unzOpenCurrentFile(it->archive)) {
    return stop(it, SP_ERR);
  }
  it->open = true;
  entry->kind = SPN_ZIP_FILE;
  if ((mode & spn_zip_mode_kind) != spn_zip_mode_link) {
    return true;
  }

  u8* target = (u8*)buf.data + path.len;
  u64 len = 0;
  if (sp_io_read_all(&it->content, target, buf.len - path.len, &len) != SP_ERR_IO_EOF) {
    return stop(it, SP_ERR);
  }
  entry->kind = SPN_ZIP_SYM_LINK;
  entry->link = sp_str((const c8*)target, (u32)len);
  return true;
}

void spn_zip_it_close(spn_zip_it_t* it) {
  unzClose(it->archive);
}
