#include "compiler/coff.h"

#define SPN_COFF_MACHINE_AMD64 0x8664
#define SPN_COFF_MACHINE_ARM64 0xaa64
#define SPN_COFF_CLASS_EXTERNAL 2
#define SPN_COFF_TYPE_FUNCTION 0x20
#define SPN_COFF_SECTION_CODE 0x00000020
#define SPN_COFF_SECTION_EXECUTE 0x20000000
#define SPN_COFF_SECTION_SIZE 40

static const u8 coff_bigobj [16] = {
  0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8,
};

static u16 coff_u16(const u8* p) {
  return (u16)(p[0] | (p[1] << 8));
}

static u32 coff_u32(const u8* p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static spn_err_t coff_set(spn_coff_parser_t* coff, spn_err_t err) {
  coff->err = err;
  return err;
}

spn_err_t spn_coff_init(spn_coff_parser_t* coff, sp_str_t object) {
  *coff = (spn_coff_parser_t) { .object = object };

  const u8* p = (const u8*)object.data;
  u64 len = object.len;
  if (len < 20) {
    return coff_set(coff, SPN_ERR_TOC_OPAQUE);
  }

  u16 machine = coff_u16(p);
  u64 table = 0;
  if (!machine && coff_u16(p + 2) == 0xffff) {
    // Version 0 is an import library member, which defines nothing. The
    // only later header with a symbol table is /bigobj's
    u16 version = coff_u16(p + 4);
    if (!version) {
      return SPN_OK;
    }
    if (version < 2 || len < 56 || memcmp(p + 12, coff_bigobj, sizeof(coff_bigobj))) {
      return coff_set(coff, SPN_ERR_TOC_OPAQUE);
    }
    machine = coff_u16(p + 6);
    coff->sections = 56;
    coff->num_sections = coff_u32(p + 44);
    table = coff_u32(p + 48);
    coff->num_symbols = coff_u32(p + 52);
    coff->record = 20;
  }
  else {
    coff->sections = 20 + coff_u16(p + 16);
    coff->num_sections = coff_u16(p + 2);
    table = coff_u32(p + 8);
    coff->num_symbols = coff_u32(p + 12);
    coff->record = 18;
  }
  if (machine != SPN_COFF_MACHINE_AMD64 && machine != SPN_COFF_MACHINE_ARM64) {
    return coff_set(coff, SPN_ERR_TOC_OPAQUE);
  }

  u64 symbols_len = (u64)coff->num_symbols * coff->record;
  if (coff->sections > len || (u64)coff->num_sections * SPN_COFF_SECTION_SIZE > len - coff->sections) {
    return coff_set(coff, SPN_ERR_TOC_TRUNCATED);
  }
  if (table > len || symbols_len > len - table) {
    return coff_set(coff, SPN_ERR_TOC_TRUNCATED);
  }
  coff->symbols = table;
  coff->strings = table + symbols_len;
  if (len - coff->strings >= 4) {
    coff->strings_len = coff_u32(p + coff->strings);
    if (coff->strings_len > len - coff->strings) {
      return coff_set(coff, SPN_ERR_TOC_TRUNCATED);
    }
  }
  return SPN_OK;
}

bool spn_coff_next(spn_coff_parser_t* coff, spn_coff_symbol_t* symbol) {
  const u8* p = (const u8*)coff->object.data;
  while (!coff->err && coff->index < coff->num_symbols) {
    const u8* record = p + coff->symbols + (u64)coff->index * coff->record;
    const u8* tail = record + coff->record - 4;
    s32 section = coff->record == 18 ? (s16)coff_u16(record + 12) : (s32)coff_u32(record + 12);
    coff->index += 1 + tail[3];

    // Section 0 with a size is a common symbol; without one it is a reference
    if (tail[2] != SPN_COFF_CLASS_EXTERNAL || section < 0 || (!section && !coff_u32(record + 8))) {
      continue;
    }
    if ((u32)section > coff->num_sections) {
      coff->err = SPN_ERR_TOC_TRUNCATED;
      return false;
    }

    if (coff_u32(record)) {
      u32 len = 0;
      while (len < 8 && record[len]) {
        len++;
      }
      symbol->name = sp_str((const c8*)record, len);
    }
    else {
      u32 at = coff_u32(record + 4);
      u32 end = at;
      while (end < coff->strings_len && p[coff->strings + end]) {
        end++;
      }
      if (at < 4 || end >= coff->strings_len) {
        coff->err = SPN_ERR_TOC_TRUNCATED;
        return false;
      }
      symbol->name = sp_str((const c8*)p + coff->strings + at, end - at);
    }

    // A label in assembly has no type, so code is also whatever sits in an
    // executable section
    symbol->data = true;
    if (section) {
      u32 flags = coff_u32(p + coff->sections + (u64)(section - 1) * SPN_COFF_SECTION_SIZE + 36);
      symbol->data = (coff_u16(tail) & 0x30) != SPN_COFF_TYPE_FUNCTION && !(flags & (SPN_COFF_SECTION_CODE | SPN_COFF_SECTION_EXECUTE));
    }
    return true;
  }
  return false;
}
