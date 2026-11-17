#include "sp.h"
#include "cpu/cpu.h"
#include "xz.h"

#include <lzma.h>

#define spn_xz_input_size (64 * 1024)
#define spn_xz_max_threads 8

static sp_err_t decode(sp_io_reader_t* reader, void* ptr, u64 size, u64* bytes_read) {
  spn_xz_reader_t* xz = (spn_xz_reader_t*)reader;
  lzma_stream* stream = (lzma_stream*)xz->stream;
  *bytes_read = 0;

  stream->next_out = (u8*)ptr;
  stream->avail_out = size;
  while (stream->avail_out == size && !xz->done) {
    if (!stream->avail_in && !xz->drained) {
      u64 filled = 0;
      sp_err_t err = sp_io_read(xz->inner, xz->input, spn_xz_input_size, &filled);
      if (err == SP_ERR_IO_EOF) {
        xz->drained = true;
      }
      else if (err) {
        return err;
      }
      stream->next_in = xz->input;
      stream->avail_in = filled;
    }

    lzma_ret ret = lzma_code(stream, xz->drained ? LZMA_FINISH : LZMA_RUN);
    if (ret == LZMA_STREAM_END) {
      xz->done = true;
    }
    else if (ret != LZMA_OK) {
      return SP_ERR;
    }
  }

  *bytes_read = size - stream->avail_out;
  return *bytes_read ? SP_OK : SP_ERR_IO_EOF;
}

bool spn_xz_is_stream(sp_str_t head) {
  return sp_str_starts_with(head, sp_str_lit("\xfd" "7zXZ\0"));
}

spn_err_t spn_xz_reader_init(spn_xz_reader_t* xz, sp_mem_t mem, sp_io_reader_t* inner) {
  lzma_stream* stream = sp_alloc_type(mem, lzma_stream);
  lzma_mt options = {
    .flags = LZMA_CONCATENATED,
    .threads = sp_min(spn_cpu_count(), spn_xz_max_threads),
    .memlimit_threading = lzma_physmem() / 4,
    .memlimit_stop = SP_LIMIT_U64_MAX,
  };
  if (lzma_stream_decoder_mt(stream, &options) != LZMA_OK) {
    return SPN_ERROR;
  }

  *xz = (spn_xz_reader_t) {
    .base = { .read = decode },
    .inner = inner,
    .stream = stream,
    .input = (u8*)sp_alloc(mem, spn_xz_input_size),
  };
  return SPN_OK;
}

void spn_xz_reader_close(spn_xz_reader_t* xz) {
  lzma_end((lzma_stream*)xz->stream);
}
