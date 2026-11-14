#ifndef SPN_EXTERNAL_GZIP_H
#define SPN_EXTERNAL_GZIP_H

#include "sp.h"
#include "spn/core.h"

typedef struct {
  sp_io_reader_t base;
  sp_io_reader_t* inner;
  void* stream;
  u8* input;
  bool drained;
  bool member;
} spn_gzip_reader_t;

bool      spn_gzip_is_stream(sp_str_t head);
spn_err_t spn_gzip_reader_init(spn_gzip_reader_t* gzip, sp_mem_t mem, sp_io_reader_t* inner);
void      spn_gzip_reader_close(spn_gzip_reader_t* gzip);

#endif
