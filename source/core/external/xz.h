#ifndef SPN_EXTERNAL_XZ_H
#define SPN_EXTERNAL_XZ_H

#include "sp.h"
#include "spn/core.h"

typedef struct {
  sp_io_reader_t base;
  sp_io_reader_t* inner;
  void* stream;
  u8* input;
  bool drained;
  bool done;
} spn_xz_reader_t;

bool      spn_xz_is_stream(sp_str_t head);
spn_err_t spn_xz_reader_init(spn_xz_reader_t* xz, sp_mem_t mem, sp_io_reader_t* inner);
void      spn_xz_reader_close(spn_xz_reader_t* xz);

#endif
