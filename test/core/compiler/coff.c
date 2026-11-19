#include "compiler.h"

#include "compiler/coff.h"

#define coff_syms_max 4

typedef enum {
  COFF_REFERENCE,
  COFF_TEXT,
  COFF_DATA,
  COFF_RDATA,
  COFF_COMMON,
  COFF_ABSOLUTE,
} coff_section_t;

typedef enum {
  COFF_OBJECT,
  COFF_BIGOBJ,
  COFF_IMPORT,
  COFF_ANONYMOUS,
  COFF_FOREIGN,
  COFF_TRUNCATED,
} coff_kind_t;

typedef struct {
  const c8* name;
  coff_section_t section;
  bool function;
  bool local;
  u8 aux;
} coff_symbol_t;

typedef struct {
  spn_err_t err;
  const c8* code [coff_syms_max];
  const c8* data [coff_syms_max];
} coff_expect_t;

typedef struct {
  const c8* name;
  coff_kind_t kind;
  coff_symbol_t symbols [coff_syms_max];
  coff_expect_t expect;
} coff_test_t;

typedef sp_io_dyn_mem_writer_t coff_buf_t;

static const u32 section_flags [] = { 0x60000020, 0xc0000040, 0x40000040 };

static void coff_bytes(coff_buf_t* b, const void* ptr, u32 len) {
  sp_io_write(&b->base, ptr, len, SP_NULLPTR);
}

static void coff_zeros(coff_buf_t* b, u32 len) {
  sp_for(it, len) {
    coff_bytes(b, "\0", 1);
  }
}

static void coff_u16(coff_buf_t* b, u16 v) {
  u8 p [2] = { (u8)v, (u8)(v >> 8) };
  coff_bytes(b, p, 2);
}

static void coff_u32(coff_buf_t* b, u32 v) {
  u8 p [4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
  coff_bytes(b, p, 4);
}

static sp_str_t coff_emit(sp_mem_t mem, const coff_test_t* it) {
  coff_buf_t b = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &b);

  switch (it->kind) {
    case COFF_IMPORT: {
      coff_u16(&b, 0);
      coff_u16(&b, 0xffff);
      coff_u16(&b, 0);
      coff_u16(&b, 0x8664);
      coff_zeros(&b, 12);
      return sp_io_dyn_mem_writer_as_str(&b);
    }
    case COFF_ANONYMOUS: {
      coff_u16(&b, 0);
      coff_u16(&b, 0xffff);
      coff_u16(&b, 1);
      coff_u16(&b, 0x8664);
      coff_zeros(&b, 48);
      return sp_io_dyn_mem_writer_as_str(&b);
    }
    case COFF_FOREIGN: {
      coff_bytes(&b, "BC\xc0\xde", 4);
      coff_zeros(&b, 16);
      return sp_io_dyn_mem_writer_as_str(&b);
    }
    case COFF_OBJECT:
    case COFF_BIGOBJ:
    case COFF_TRUNCATED: {
      break;
    }
  }

  u32 n = 0;
  sp_carr_detect_len(it->symbols, n, it->symbols[n].name);
  u32 records = 0;
  sp_for(s, n) {
    records += 1 + it->symbols[s].aux;
  }
  bool big = it->kind == COFF_BIGOBJ;
  u32 sections = sp_carr_len(section_flags);
  u32 table = (big ? 56 : 20) + sections * 40;

  if (big) {
    static const u8 class_id [16] = {
      0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8,
    };
    coff_u16(&b, 0);
    coff_u16(&b, 0xffff);
    coff_u16(&b, 2);
    coff_u16(&b, 0x8664);
    coff_u32(&b, 0);
    coff_bytes(&b, class_id, 16);
    coff_zeros(&b, 16);
    coff_u32(&b, sections);
    coff_u32(&b, table);
    coff_u32(&b, records);
  }
  else {
    coff_u16(&b, 0x8664);
    coff_u16(&b, (u16)sections);
    coff_u32(&b, 0);
    coff_u32(&b, table);
    coff_u32(&b, records);
    coff_u16(&b, 0);
    coff_u16(&b, 0);
  }
  sp_for(s, sections) {
    coff_zeros(&b, 36);
    coff_u32(&b, section_flags[s]);
  }
  if (it->kind == COFF_TRUNCATED) {
    return sp_io_dyn_mem_writer_as_str(&b);
  }

  u32 strings = 4;
  sp_for(s, n) {
    const coff_symbol_t* symbol = &it->symbols[s];
    u32 len = sp_cstr_len(symbol->name);
    if (len > 8) {
      coff_u32(&b, 0);
      coff_u32(&b, strings);
      strings += len + 1;
    }
    else {
      coff_bytes(&b, symbol->name, len);
      coff_zeros(&b, 8 - len);
    }

    s32 section = 0;
    u32 value = 0;
    switch (symbol->section) {
      case COFF_REFERENCE: break;
      case COFF_TEXT:      section = 1; break;
      case COFF_DATA:      section = 2; break;
      case COFF_RDATA:     section = 3; break;
      case COFF_COMMON:    value = 4; break;
      case COFF_ABSOLUTE:  section = -1; break;
    }
    coff_u32(&b, value);
    if (big) {
      coff_u32(&b, (u32)section);
    }
    else {
      coff_u16(&b, (u16)section);
    }
    coff_u16(&b, symbol->function ? 0x20 : 0);
    coff_bytes(&b, symbol->local ? "\x03" : "\x02", 1);
    coff_bytes(&b, &symbol->aux, 1);
    coff_zeros(&b, symbol->aux * (big ? 20 : 18));
  }

  coff_u32(&b, strings);
  sp_for(s, n) {
    u32 len = sp_cstr_len(it->symbols[s].name);
    if (len > 8) {
      coff_bytes(&b, it->symbols[s].name, len + 1);
    }
  }
  return sp_io_dyn_mem_writer_as_str(&b);
}

static const coff_test_t tests [] = {
  {
    .name = "function",
    .symbols = { { "A", COFF_TEXT, .function = true } },
    .expect.code = { "A" },
  },
  {
    .name = "untyped_label_in_code",
    .symbols = { { "A", COFF_TEXT } },
    .expect.code = { "A" },
  },
  {
    .name = "initialized_global",
    .symbols = { { "A", COFF_DATA } },
    .expect.data = { "A" },
  },
  {
    .name = "constant",
    .symbols = { { "A", COFF_RDATA } },
    .expect.data = { "A" },
  },
  {
    .name = "common",
    .symbols = { { "A", COFF_COMMON } },
    .expect.data = { "A" },
  },
  {
    .name = "reference_is_not_a_definition",
    .symbols = { { "A", COFF_REFERENCE }, { "B", COFF_TEXT, .function = true } },
    .expect.code = { "B" },
  },
  {
    .name = "static_is_not_exported",
    .symbols = { { "A", COFF_TEXT, .function = true, .local = true }, { "B", COFF_DATA } },
    .expect.data = { "B" },
  },
  {
    .name = "absolute_is_not_exported",
    .symbols = { { "A", COFF_ABSOLUTE }, { "B", COFF_DATA } },
    .expect.data = { "B" },
  },
  {
    .name = "long_name_comes_from_the_string_table",
    .symbols = { { "AAAAAAAAA", COFF_TEXT, .function = true }, { "BBBBBBBBB", COFF_DATA } },
    .expect = { .code = { "AAAAAAAAA" }, .data = { "BBBBBBBBB" } },
  },
  {
    .name = "eight_bytes_fit_inline",
    .symbols = { { "AAAAAAAA", COFF_TEXT, .function = true } },
    .expect.code = { "AAAAAAAA" },
  },
  {
    .name = "aux_records_are_stepped_over",
    .symbols = { { "A", COFF_TEXT, .function = true, .aux = 2 }, { "B", COFF_DATA } },
    .expect = { .code = { "A" }, .data = { "B" } },
  },
  {
    .name = "bigobj",
    .kind = COFF_BIGOBJ,
    .symbols = { { "A", COFF_TEXT, .function = true, .aux = 1 }, { "BBBBBBBBB", COFF_RDATA } },
    .expect = { .code = { "A" }, .data = { "BBBBBBBBB" } },
  },
  {
    .name = "import_member_defines_nothing",
    .kind = COFF_IMPORT,
  },
  {
    .name = "anonymous_object_is_opaque",
    .kind = COFF_ANONYMOUS,
    .expect.err = SPN_ERR_TOC_OPAQUE,
  },
  {
    .name = "foreign_object_is_opaque",
    .kind = COFF_FOREIGN,
    .expect.err = SPN_ERR_TOC_OPAQUE,
  },
  {
    .name = "missing_symbol_table",
    .kind = COFF_TRUNCATED,
    .symbols = { { "A", COFF_TEXT, .function = true } },
    .expect.err = SPN_ERR_TOC_TRUNCATED,
  },
};

sp_test_each(coff, symbols, coff_test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  spn_coff_parser_t coff;
  spn_err_t err = spn_coff_init(&coff, coff_emit(mem, it));

  sp_da(sp_str_t) code = sp_da_new(mem, sp_str_t);
  sp_da(sp_str_t) data = sp_da_new(mem, sp_str_t);
  spn_coff_symbol_t symbol = sp_zero;
  while (spn_coff_next(&coff, &symbol)) {
    if (symbol.data) {
      sp_da_push(data, sp_str_copy(mem, symbol.name));
    }
    else {
      sp_da_push(code, sp_str_copy(mem, symbol.name));
    }
  }
  if (!err) {
    err = coff.err;
  }
  sp_expect_eq(t, err, it->expect.err);

  if (!it->expect.err) {
    sp_must_strs_eq(t, code, sp_da_size(code), it->expect.code);
    sp_must_strs_eq(t, data, sp_da_size(data), it->expect.data);
  }

  return SP_OK;
}
