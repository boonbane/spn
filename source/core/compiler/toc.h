#ifndef SPN_COMPILER_TOC_H
#define SPN_COMPILER_TOC_H

#include "sp.h"
#include "spn/core.h"

#define SPN_TOC_SYMBOL_MAX 8192

typedef enum {
  SPN_TOC_FORMAT_NONE,
  SPN_TOC_FORMAT_GNU,
  SPN_TOC_FORMAT_BSD,
} spn_toc_format_t;

typedef struct {
  sp_io_limit_reader_t body;
  spn_toc_format_t format;
  spn_err_t err;
  u64 count;
  u64 remaining;
  u8 buf [SPN_TOC_SYMBOL_MAX];
} spn_toc_parser_t;

typedef struct {
  sp_io_reader_t* io;
  sp_mem_t mem;
  u8* data;
  u64 capacity;
  u64 pad;
  spn_err_t err;
} spn_ar_members_t;

spn_err_t spn_toc_init(spn_toc_parser_t* toc, sp_io_reader_t* io);
bool      spn_toc_next(spn_toc_parser_t* toc, sp_str_t* symbol);
spn_err_t spn_ar_members_init(spn_ar_members_t* members, sp_io_reader_t* io, sp_mem_t mem);
bool      spn_ar_members_next(spn_ar_members_t* members, sp_str_t* member);

#endif
