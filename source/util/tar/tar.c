#include "tar/tar.h"

#define sp_tar_block_size 512

#define try(it, expr) \
  do { \
    (it)->err = (expr); \
    if ((it)->err) { \
      return false; \
    } \
  } while (0)

typedef struct {
  c8 name [100];
  c8 mode [8];
  c8 uid [8];
  c8 gid [8];
  c8 size [12];
  c8 mtime [12];
  c8 checksum [8];
  c8 kind;
  c8 link [100];
  c8 magic [6];
  c8 version [2];
  c8 uname [32];
  c8 gname [32];
  c8 dev_major [8];
  c8 dev_minor [8];
  c8 prefix [155];
  c8 pad [12];
} header_t;

static sp_str_t text(const c8* field, u32 capacity) {
  u32 len = 0;
  while (len < capacity && field[len]) {
    len++;
  }
  return sp_str(field, len);
}

static bool octal(const c8* field, u32 capacity, u64* value) {
  sp_str_t digits = sp_str_trim(text(field, capacity));
  u64 result = 0;
  sp_for(it, digits.len) {
    c8 c = digits.data[it];
    if (c < '0' || c > '7') {
      return false;
    }
    result = result * 8 + (u64)(c - '0');
  }
  *value = result;
  return true;
}

static u64 padding(u64 size) {
  return sp_align_offset(size, sp_tar_block_size) - size;
}

static sp_err_t meta(sp_io_reader_t* reader, sp_io_mem_writer_t* names, u64 size, sp_str_t* body) {
  if (size > names->len - names->pos) {
    return SP_ERR_IO_NO_SPACE;
  }
  *body = sp_str((const c8*)names->ptr + names->pos, (u32)size);
  sp_try(sp_io_read_all(reader, names->ptr + names->pos, size, SP_NULLPTR));
  names->pos += size;
  return sp_io_discard(reader, padding(size), SP_NULLPTR);
}

sp_tar_it_t sp_tar_it(sp_io_reader_t* reader) {
  return (sp_tar_it_t) { .reader = reader };
}

bool sp_tar_it_next(sp_tar_it_t* it, sp_mem_slice_t buf, sp_tar_entry_t* entry) {
  *entry = sp_zero_s(sp_tar_entry_t);
  try(it, sp_io_discard(it->reader, it->content.remaining + it->padding, SP_NULLPTR));

  sp_io_mem_writer_t names = sp_zero;
  sp_io_mem_writer_from_buffer(&names, buf.data, buf.len);
  sp_str_t path = sp_zero;
  sp_str_t link = sp_zero;

  while (true) {
    header_t header = sp_zero;
    try(it, sp_io_read_all(it->reader, &header, sizeof(header), SP_NULLPTR));

    const u8* bytes = (const u8*)&header;
    u64 sum = 0;
    sp_for(at, sizeof(header)) {
      sum += bytes[at];
    }
    if (!sum) {
      return false;
    }
    sp_for(at, sizeof(header.checksum)) {
      sum += ' ' - (u8)header.checksum[at];
    }

    u64 checksum = 0;
    u64 size = 0;
    u64 mode = 0;
    bool valid =
      octal(header.checksum, sizeof(header.checksum), &checksum) &&
      octal(header.size, sizeof(header.size), &size) &&
      octal(header.mode, sizeof(header.mode), &mode) &&
      checksum == sum;
    if (!valid) {
      it->err = SP_ERR;
      return false;
    }

    sp_tar_kind_t kind = SP_TAR_FILE;
    u64 content = 0;
    switch (header.kind) {
      case 'g': {
        try(it, sp_io_discard(it->reader, size + padding(size), SP_NULLPTR));
        continue;
      }
      case 'L': {
        sp_str_t body = sp_zero;
        try(it, meta(it->reader, &names, size, &body));
        path = text(body.data, body.len);
        continue;
      }
      case 'K': {
        sp_str_t body = sp_zero;
        try(it, meta(it->reader, &names, size, &body));
        link = text(body.data, body.len);
        continue;
      }
      case 'x': {
        sp_str_t body = sp_zero;
        try(it, meta(it->reader, &names, size, &body));
        while (!sp_str_empty(body)) {
          sp_str_t digits = sp_str_cleave_c8(body, ' ').first;
          u32 len = 0;
          if (!sp_parse_u32_ex(digits, &len) || len > body.len || len < digits.len + 2) {
            it->err = SP_ERR;
            return false;
          }
          sp_str_pair_t record = sp_str_cleave_c8(sp_str_sub(body, digits.len + 1, len - digits.len - 2), '=');
          body = sp_str_suffix(body, body.len - len);
          if (sp_str_equal_cstr(record.first, "path")) {
            path = record.second;
          }
          if (sp_str_equal_cstr(record.first, "linkpath")) {
            link = record.second;
          }
          if (sp_str_equal_cstr(record.first, "size")) {
            it->err = SP_ERR_SYS_UNSUPPORTED;
            return false;
          }
        }
        continue;
      }
      case '\0':
      case '0':
      case '7': {
        content = size;
        break;
      }
      case '1': {
        kind = SP_TAR_HARD_LINK;
        break;
      }
      case '2': {
        kind = SP_TAR_SYM_LINK;
        break;
      }
      case '5': {
        kind = SP_TAR_DIR;
        break;
      }
      default: {
        it->err = SP_ERR_SYS_UNSUPPORTED;
        return false;
      }
    }

    if (sp_str_empty(path)) {
      u64 start = names.pos;
      sp_str_t prefix = text(header.prefix, sizeof(header.prefix));
      if (sp_str_equal_cstr(text(header.magic, sizeof(header.magic)), "ustar") && !sp_str_empty(prefix)) {
        try(it, sp_io_write_str(&names.base, prefix, SP_NULLPTR));
        try(it, sp_io_write_c8(&names.base, '/'));
      }
      try(it, sp_io_write_str(&names.base, text(header.name, sizeof(header.name)), SP_NULLPTR));
      path = sp_str((const c8*)names.ptr + start, (u32)(names.pos - start));
    }
    if (sp_str_empty(link)) {
      u64 start = names.pos;
      try(it, sp_io_write_str(&names.base, text(header.link, sizeof(header.link)), SP_NULLPTR));
      link = sp_str((const c8*)names.ptr + start, (u32)(names.pos - start));
    }
    while (path.len && path.data[path.len - 1] == '/') {
      path.len--;
    }

    *entry = (sp_tar_entry_t) {
      .kind = kind,
      .path = path,
      .link = link,
      .size = content,
      .mode = (u32)(mode & 07777),
    };
    sp_io_limit_reader_init(&it->content, it->reader, content);
    it->padding = padding(content);
    return true;
  }
}
