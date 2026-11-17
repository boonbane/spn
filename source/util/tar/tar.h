#ifndef SPN_SP_TAR_H
#define SPN_SP_TAR_H

#include "sp.h"

typedef enum {
  SP_TAR_FILE,
  SP_TAR_DIR,
  SP_TAR_SYM_LINK,
  SP_TAR_HARD_LINK,
} sp_tar_kind_t;

typedef struct {
  sp_tar_kind_t kind;
  sp_str_t path;
  sp_str_t link;
  u64 size;
  u32 mode;
} sp_tar_entry_t;

typedef struct {
  sp_io_reader_t* reader;
  sp_io_limit_reader_t content;
  u64 padding;
  sp_err_t err;
} sp_tar_it_t;

sp_tar_it_t sp_tar_it(sp_io_reader_t* reader);
bool        sp_tar_it_next(sp_tar_it_t* it, sp_mem_slice_t buf, sp_tar_entry_t* entry);

#endif
