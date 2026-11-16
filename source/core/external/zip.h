#ifndef SPN_EXTERNAL_ZIP_H
#define SPN_EXTERNAL_ZIP_H

#include "sp.h"
#include "spn/core.h"

typedef enum {
  SPN_ZIP_FILE,
  SPN_ZIP_DIR,
  SPN_ZIP_SYM_LINK,
} spn_zip_kind_t;

typedef struct {
  spn_zip_kind_t kind;
  sp_str_t path;
  sp_str_t link;
  u64 size;
  bool executable;
} spn_zip_entry_t;

typedef struct {
  sp_io_reader_t content;
  sp_sys_fd_t file;
  u64 size;
  u64 pos;
  void* archive;
  u64 entries;
  u64 index;
  bool cut;
  sp_err_t err;
} spn_zip_it_t;

bool      spn_zip_is_archive(sp_str_t head);
spn_err_t spn_zip_it_init(spn_zip_it_t* it, sp_io_file_reader_t* file);
bool      spn_zip_it_next(spn_zip_it_t* it, sp_mem_slice_t buf, spn_zip_entry_t* entry);
void      spn_zip_it_close(spn_zip_it_t* it);

#endif
