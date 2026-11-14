#include "sp.h"
#include "gzip.h"

#include <zlib.h>

#define spn_gzip_input_size (64 * 1024)

static sp_err_t decode(sp_io_reader_t* reader, void* ptr, u64 size, u64* bytes_read) {
  spn_gzip_reader_t* gzip = (spn_gzip_reader_t*)reader;
  z_stream* stream = (z_stream*)gzip->stream;
  u32 room = (u32)sp_min(size, (u64)SP_LIMIT_U32_MAX);
  *bytes_read = 0;

  stream->next_out = (u8*)ptr;
  stream->avail_out = room;
  while (stream->avail_out == room) {
    if (!stream->avail_in && !gzip->drained) {
      u64 filled = 0;
      sp_err_t err = sp_io_read(gzip->inner, gzip->input, spn_gzip_input_size, &filled);
      if (err == SP_ERR_IO_EOF) {
        gzip->drained = true;
      }
      else if (err) {
        return err;
      }
      stream->next_in = gzip->input;
      stream->avail_in = (u32)filled;
    }
    if (gzip->drained && !stream->avail_in) {
      return gzip->member ? SP_ERR : SP_ERR_IO_EOF;
    }

    s32 ret = inflate(stream, Z_NO_FLUSH);
    gzip->member = ret == Z_OK;
    if (ret == Z_STREAM_END) {
      inflateReset(stream);
    }
    else if (ret != Z_OK) {
      return SP_ERR;
    }
  }

  *bytes_read = room - stream->avail_out;
  return SP_OK;
}

bool spn_gzip_is_stream(sp_str_t head) {
  return sp_str_starts_with(head, sp_str_lit("\x1f\x8b"));
}

spn_err_t spn_gzip_reader_init(spn_gzip_reader_t* gzip, sp_mem_t mem, sp_io_reader_t* inner) {
  z_stream* stream = sp_alloc_type(mem, z_stream);
  if (inflateInit2(stream, 16 + MAX_WBITS) != Z_OK) {
    return SPN_ERROR;
  }

  *gzip = (spn_gzip_reader_t) {
    .base = { .read = decode },
    .inner = inner,
    .stream = stream,
    .input = (u8*)sp_alloc(mem, spn_gzip_input_size),
  };
  return SPN_OK;
}

void spn_gzip_reader_close(spn_gzip_reader_t* gzip) {
  inflateEnd((z_stream*)gzip->stream);
}
