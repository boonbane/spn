#include "unit.h"

#include "external/obj.h"

#define OBJ_WASM_MAX_SYMBOLS 3

typedef struct {
  const c8* name;
  u32 size;
} symbol_t;

typedef struct {
  const c8* name;
  symbol_t symbols [OBJ_WASM_MAX_SYMBOLS];
} wasm_t;

static const wasm_t wasm_tests [] = {
  { .name = "one",   .symbols = { { "A", 4 } } },
  { .name = "many",  .symbols = { { "A", 1 }, { "B", 2 }, { "C", 3 } } },
  { .name = "empty", .symbols = { { "A", 0 } } },
  { .name = "long",  .symbols = { { "A", 200 } } },
};

typedef struct {
  sp_str_t bytes;
  u64 pos;
} cursor_t;

typedef struct {
  sp_str_t data;
  u64 align_log2;
} segment_t;

typedef struct {
  sp_str_t name;
  u8 kind;
  u64 flags;
  u64 segment;
  u64 offset;
  u64 size;
} entry_t;

static sp_str_t read_bytes(cursor_t* c, u64 len) {
  if (c->pos > c->bytes.len || len > c->bytes.len - c->pos) {
    c->pos = c->bytes.len + 1;
    return sp_zero_s(sp_str_t);
  }
  sp_str_t bytes = sp_str(c->bytes.data + c->pos, (u32)len);
  c->pos += len;
  return bytes;
}

static u8 read_u8(cursor_t* c) {
  sp_str_t byte = read_bytes(c, 1);
  return byte.len ? (u8)byte.data[0] : 0;
}

static u64 read_leb(cursor_t* c) {
  u64 value = 0;
  u32 shift = 0;
  u8 byte = 0x80;
  while ((byte & 0x80) && shift < 64) {
    byte = read_u8(c);
    value |= (u64)(byte & 0x7F) << shift;
    shift += 7;
  }
  return value;
}

static sp_str_t read_vec(cursor_t* c) {
  return read_bytes(c, read_leb(c));
}

sp_test_each(obj, wasm, wasm_t, wasm_tests) {
  sp_mem_t mem = sp_test_arena(t);

  u32 num_symbols = 0;
  sp_carr_detect_len(it->symbols, num_symbols, it->symbols[num_symbols].name);

  spn_obj_builder_t obj = sp_zero;
  spn_obj_init(&obj, mem, SPN_OBJ_WASM, SPN_ARCH_WASM32);
  sp_str_t contents [OBJ_WASM_MAX_SYMBOLS] = sp_zero;
  sp_for(i, num_symbols) {
    c8* data = sp_alloc_n(mem, c8, it->symbols[i].size);
    sp_for(j, it->symbols[i].size) {
      data[j] = (c8)(it->symbols[i].name[0] + j);
    }
    contents[i] = sp_str(data, it->symbols[i].size);
    spn_obj_add_symbol(&obj, sp_cstr_as_str(it->symbols[i].name), data, it->symbols[i].size);
  }

  sp_io_dyn_mem_writer_t writer = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &writer);
  sp_must_ok(t, sp_wasm_write(obj.wasm, &writer.base));

  cursor_t object = { .bytes = sp_io_dyn_mem_writer_as_str(&writer) };
  u32 header [2] = sp_zero;
  sp_str_t preamble = read_bytes(&object, sizeof(header));
  sp_must_eq(t, preamble.len, sizeof(header));
  sp_mem_copy(header, preamble.data, sizeof(header));
  sp_expect_eq(t, header[0], SP_WASM_MAGIC);
  sp_expect_eq(t, header[1], SP_WASM_VERSION);

  segment_t segments [OBJ_WASM_MAX_SYMBOLS] = sp_zero;
  entry_t entries [OBJ_WASM_MAX_SYMBOLS] = sp_zero;
  u64 num_segments = 0;
  u64 num_entries = 0;
  while (object.pos < object.bytes.len) {
    u8 id = read_u8(&object);
    cursor_t section = { .bytes = read_vec(&object) };
    if (id == SP_WASM_SEC_DATA) {
      num_segments = read_leb(&section);
      sp_must(t, num_segments <= OBJ_WASM_MAX_SYMBOLS);
      sp_for(i, num_segments) {
        sp_expect_eq(t, read_leb(&section), SP_WASM_DATA_SEGMENT_ACTIVE);
        sp_expect_eq(t, read_u8(&section), SP_WASM_OPCODE_I32_CONST);
        read_leb(&section);
        sp_expect_eq(t, read_u8(&section), SP_WASM_OPCODE_END);
        segments[i].data = read_vec(&section);
      }
      sp_must_eq(t, section.pos, section.bytes.len);
    }
    else if (id == SP_WASM_SEC_CUSTOM && sp_str_equal_cstr(read_vec(&section), "linking")) {
      sp_expect_eq(t, read_leb(&section), SP_WASM_METADATA_VERSION);
      while (section.pos < section.bytes.len) {
        u8 kind = read_u8(&section);
        cursor_t sub = { .bytes = read_vec(&section) };
        if (kind == SP_WASM_SYMBOL_TABLE) {
          num_entries = read_leb(&sub);
          sp_must(t, num_entries <= OBJ_WASM_MAX_SYMBOLS);
          sp_for(i, num_entries) {
            entries[i].kind = read_u8(&sub);
            entries[i].flags = read_leb(&sub);
            entries[i].name = read_vec(&sub);
            entries[i].segment = read_leb(&sub);
            entries[i].offset = read_leb(&sub);
            entries[i].size = read_leb(&sub);
          }
        }
        else if (kind == SP_WASM_SEGMENT_INFO) {
          sp_must_eq(t, read_leb(&sub), num_segments);
          sp_for(i, num_segments) {
            read_vec(&sub);
            segments[i].align_log2 = read_leb(&sub);
            read_leb(&sub);
          }
        }
        sp_must_eq(t, sub.pos, sub.bytes.len);
      }
      sp_must_eq(t, section.pos, section.bytes.len);
    }
  }
  sp_must_eq(t, object.pos, object.bytes.len);

  sp_must_eq(t, num_entries, num_symbols);
  sp_for(i, num_symbols) {
    entry_t* entry = SP_NULLPTR;
    sp_for(e, num_entries) {
      if (sp_str_equal_cstr(entries[e].name, it->symbols[i].name)) {
        entry = &entries[e];
      }
    }
    sp_must(t, entry);
    sp_expect_eq(t, entry->kind, SP_WASM_SYMBOL_TYPE_DATA);
    sp_expect_eq(t, entry->flags, SP_WASM_SYMBOL_BINDING_GLOBAL);
    sp_must(t, entry->segment < num_segments);
    segment_t* segment = &segments[entry->segment];
    sp_expect_eq(t, segment->align_log2, 3);
    sp_must(t, entry->offset + entry->size <= segment->data.len);
    sp_expect_str_eq(t, sp_str(segment->data.data + entry->offset, (u32)entry->size), contents[i]);
  }
  return SP_OK;
}
