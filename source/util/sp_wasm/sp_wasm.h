#ifndef SP_WASM_H
#define SP_WASM_H

#include "sp.h"

#define SP_WASM_MAGIC                  0x6D736100u
#define SP_WASM_VERSION                1u
#define SP_WASM_METADATA_VERSION       2
#define SP_WASM_PAGE_SIZE              65536

#define SP_WASM_SEC_CUSTOM             0
#define SP_WASM_SEC_IMPORT             2
#define SP_WASM_SEC_DATA               11

#define SP_WASM_EXTERNAL_MEMORY        2
#define SP_WASM_LIMITS_FLAG_NONE       0
#define SP_WASM_DATA_SEGMENT_ACTIVE    0
#define SP_WASM_OPCODE_I32_CONST       0x41
#define SP_WASM_OPCODE_END             0x0B

#define SP_WASM_SEGMENT_INFO           5
#define SP_WASM_SYMBOL_TABLE           8
#define SP_WASM_SEGMENT_FLAG_NONE      0
#define SP_WASM_SYMBOL_TYPE_DATA       1
#define SP_WASM_SYMBOL_BINDING_GLOBAL  0

typedef struct {
  sp_str_t name;
  u32 align_log2;
  sp_io_dyn_mem_writer_t writer;
} sp_wasm_segment_t;

typedef struct {
  sp_str_t name;
  u32 segment;
  u64 offset;
  u64 size;
} sp_wasm_sym_entry_t;

typedef struct {
  sp_mem_t mem;
  sp_da(sp_wasm_segment_t) segments;
  sp_da(sp_wasm_sym_entry_t) symbols;
} sp_wasm_t;

sp_wasm_t*  sp_wasm_new(sp_mem_t mem);
u32         sp_wasm_add_segment(sp_wasm_t* wasm, sp_str_t name, u32 align_log2);
void        sp_wasm_add_symbol(sp_wasm_t* wasm, sp_str_t name, u32 segment, u64 offset, u64 size);
sp_err_t    sp_wasm_write(sp_wasm_t* wasm, sp_io_writer_t* io);
sp_err_t    sp_wasm_write_to_file(sp_wasm_t* wasm, sp_path_t path);

#endif


#if defined(SP_IMPLEMENTATION) && !defined(SP_WASM_IMPLEMENTATION)
  #define SP_WASM_IMPLEMENTATION
#endif

#if defined(SP_WASM_IMPLEMENTATION) && !defined(SP_WASM_IMPLEMENTATION_GUARD)
#define SP_WASM_IMPLEMENTATION_GUARD

static u32 sp_wasm_uleb(u8* buffer, u64 value) {
  u32 len = 0;
  do {
    u8 byte = value & 0x7F;
    value >>= 7;
    buffer[len++] = value ? (byte | 0x80) : byte;
  } while (value);
  return len;
}

static u32 sp_wasm_sleb(u8* buffer, s64 value) {
  u32 len = 0;
  bool more = true;
  while (more) {
    u8 byte = value & 0x7F;
    value >>= 7;
    more = !((value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40)));
    buffer[len++] = more ? (byte | 0x80) : byte;
  }
  return len;
}

static u32 sp_wasm_segment_header(u8* buffer, u64 address, u64 size) {
  u32 len = sp_wasm_uleb(buffer, SP_WASM_DATA_SEGMENT_ACTIVE);
  buffer[len++] = SP_WASM_OPCODE_I32_CONST;
  len += sp_wasm_sleb(buffer + len, (s64)address);
  buffer[len++] = SP_WASM_OPCODE_END;
  len += sp_wasm_uleb(buffer + len, size);
  return len;
}

static sp_err_t sp_wasm_write_u8(sp_io_writer_t* io, u8 value) {
  return sp_io_write(io, &value, sizeof(value), SP_NULLPTR);
}

static sp_err_t sp_wasm_write_u32(sp_io_writer_t* io, u32 value) {
  return sp_io_write(io, &value, sizeof(value), SP_NULLPTR);
}

static sp_err_t sp_wasm_write_uleb(sp_io_writer_t* io, u64 value) {
  u8 buffer [16] = sp_zero;
  return sp_io_write(io, buffer, sp_wasm_uleb(buffer, value), SP_NULLPTR);
}

static sp_err_t sp_wasm_write_name(sp_io_writer_t* io, sp_str_t name) {
  sp_try(sp_wasm_write_uleb(io, name.len));
  return sp_io_write(io, name.data, name.len, SP_NULLPTR);
}

static sp_err_t sp_wasm_write_section(sp_io_writer_t* io, u8 id, sp_io_dyn_mem_writer_t* payload) {
  sp_try(sp_wasm_write_u8(io, id));
  sp_try(sp_wasm_write_uleb(io, payload->storage.len));
  return sp_io_write(io, payload->storage.data, payload->storage.len, SP_NULLPTR);
}

sp_wasm_t* sp_wasm_new(sp_mem_t mem) {
  sp_wasm_t* wasm = sp_alloc_type(mem, sp_wasm_t);
  *wasm = (sp_wasm_t) {
    .mem = mem,
    .segments = sp_da_new(mem, sp_wasm_segment_t),
    .symbols = sp_da_new(mem, sp_wasm_sym_entry_t),
  };
  return wasm;
}

u32 sp_wasm_add_segment(sp_wasm_t* wasm, sp_str_t name, u32 align_log2) {
  sp_wasm_segment_t segment = {
    .name = sp_str_copy(wasm->mem, name),
    .align_log2 = align_log2,
  };
  sp_io_dyn_mem_writer_init(wasm->mem, &segment.writer);
  sp_da_push(wasm->segments, segment);
  return (u32)(sp_da_size(wasm->segments) - 1);
}

void sp_wasm_add_symbol(sp_wasm_t* wasm, sp_str_t name, u32 segment, u64 offset, u64 size) {
  sp_da_push(wasm->symbols, ((sp_wasm_sym_entry_t) {
    .name = sp_str_copy(wasm->mem, name),
    .segment = segment,
    .offset = offset,
    .size = size,
  }));
}

static sp_err_t sp_wasm_write_ex(sp_mem_t scratch, sp_wasm_t* wasm, sp_io_writer_t* io) {
  u64 num_segments = sp_da_size(wasm->segments);
  u64* addresses = sp_alloc_n(scratch, u64, num_segments);
  u64 end = 0;
  sp_da_for(wasm->segments, it) {
    end = sp_align_offset(end, 1ull << wasm->segments[it].align_log2);
    addresses[it] = end;
    end += wasm->segments[it].writer.storage.len;
  }

  sp_io_dyn_mem_writer_t import = sp_zero;
  sp_io_dyn_mem_writer_init(scratch, &import);
  sp_wasm_write_uleb(&import.base, 1);
  sp_wasm_write_name(&import.base, sp_str_lit("env"));
  sp_wasm_write_name(&import.base, sp_str_lit("__linear_memory"));
  sp_wasm_write_u8(&import.base, SP_WASM_EXTERNAL_MEMORY);
  sp_wasm_write_uleb(&import.base, SP_WASM_LIMITS_FLAG_NONE);
  sp_wasm_write_uleb(&import.base, (end + SP_WASM_PAGE_SIZE - 1) / SP_WASM_PAGE_SIZE);

  sp_io_dyn_mem_writer_t symtab = sp_zero;
  sp_io_dyn_mem_writer_init(scratch, &symtab);
  sp_wasm_write_uleb(&symtab.base, sp_da_size(wasm->symbols));
  sp_da_for(wasm->symbols, it) {
    sp_wasm_sym_entry_t* sym = &wasm->symbols[it];
    sp_wasm_write_u8(&symtab.base, SP_WASM_SYMBOL_TYPE_DATA);
    sp_wasm_write_uleb(&symtab.base, SP_WASM_SYMBOL_BINDING_GLOBAL);
    sp_wasm_write_name(&symtab.base, sym->name);
    sp_wasm_write_uleb(&symtab.base, sym->segment);
    sp_wasm_write_uleb(&symtab.base, sym->offset);
    sp_wasm_write_uleb(&symtab.base, sym->size);
  }

  sp_io_dyn_mem_writer_t seginfo = sp_zero;
  sp_io_dyn_mem_writer_init(scratch, &seginfo);
  sp_wasm_write_uleb(&seginfo.base, num_segments);
  sp_da_for(wasm->segments, it) {
    sp_wasm_write_name(&seginfo.base, wasm->segments[it].name);
    sp_wasm_write_uleb(&seginfo.base, wasm->segments[it].align_log2);
    sp_wasm_write_uleb(&seginfo.base, SP_WASM_SEGMENT_FLAG_NONE);
  }

  sp_io_dyn_mem_writer_t linking = sp_zero;
  sp_io_dyn_mem_writer_init(scratch, &linking);
  sp_wasm_write_name(&linking.base, sp_str_lit("linking"));
  sp_wasm_write_uleb(&linking.base, SP_WASM_METADATA_VERSION);
  sp_wasm_write_section(&linking.base, SP_WASM_SYMBOL_TABLE, &symtab);
  sp_wasm_write_section(&linking.base, SP_WASM_SEGMENT_INFO, &seginfo);

  u8 header [32] = sp_zero;
  u64 data_size = sp_wasm_uleb(header, num_segments);
  sp_da_for(wasm->segments, it) {
    u64 size = wasm->segments[it].writer.storage.len;
    data_size += sp_wasm_segment_header(header, addresses[it], size) + size;
  }

  sp_try(sp_wasm_write_u32(io, SP_WASM_MAGIC));
  sp_try(sp_wasm_write_u32(io, SP_WASM_VERSION));
  sp_try(sp_wasm_write_section(io, SP_WASM_SEC_IMPORT, &import));
  sp_try(sp_wasm_write_u8(io, SP_WASM_SEC_DATA));
  sp_try(sp_wasm_write_uleb(io, data_size));
  sp_try(sp_wasm_write_uleb(io, num_segments));
  sp_da_for(wasm->segments, it) {
    sp_mem_buffer_t data = wasm->segments[it].writer.storage;
    sp_try(sp_io_write(io, header, sp_wasm_segment_header(header, addresses[it], data.len), SP_NULLPTR));
    sp_try(sp_io_write(io, data.data, data.len, SP_NULLPTR));
  }
  return sp_wasm_write_section(io, SP_WASM_SEC_CUSTOM, &linking);
}

sp_err_t sp_wasm_write(sp_wasm_t* wasm, sp_io_writer_t* io) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(wasm->mem);
  sp_err_t err = sp_wasm_write_ex(scratch.mem, wasm, io);
  sp_mem_end_scratch(scratch);
  return err;
}

sp_err_t sp_wasm_write_to_file(sp_wasm_t* wasm, sp_path_t path) {
  sp_io_file_writer_t file = sp_zero;
  sp_try(sp_io_file_writer_from_path_at(&file, path));
  u8 buffer [65536] = sp_zero;
  sp_io_writer_set_buffer(&file.base, buffer, sizeof(buffer));
  sp_err_t err = sp_wasm_write(wasm, &file.base);
  sp_err_t flushed = sp_io_flush(&file.base);
  sp_io_file_writer_close(&file);
  return err ? err : flushed;
}

#endif
