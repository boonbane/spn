#ifndef SPN_ZIG_H
#define SPN_ZIG_H

#include "sp.h"
#include "spn/core.h"
#include "compiler/types.h"

#define SPN_ZIG_PROGRESS_ROOT 255
#define SPN_ZIG_PROGRESS_UNUSED 254
#define SPN_ZIG_PROGRESS_IPC SP_LIMIT_U32_MAX
#define SPN_ZIG_PROGRESS_NODE_SIZE 128
#define SPN_ZIG_PROGRESS_NAME_CAP (SPN_ZIG_PROGRESS_NODE_SIZE - (2 * sizeof(u32)))
#define SPN_ZIG_PROGRESS_MAX_NODES 255
#define SPN_ZIG_PROGRESS_PACKET_MAX (1 + (SPN_ZIG_PROGRESS_MAX_NODES * (SPN_ZIG_PROGRESS_NODE_SIZE + 1)))

typedef struct {
  u32 completed;
  u32 total;
  u8 parent;
  sp_str_t name;
} spn_zig_node_t;

typedef struct {
  u8 pending [2 * SPN_ZIG_PROGRESS_PACKET_MAX];
  u64 fill;
  u8 snapshot [SPN_ZIG_PROGRESS_PACKET_MAX];
  spn_zig_node_t nodes [SPN_ZIG_PROGRESS_MAX_NODES];
  u32 count;
  u64 packets;
} spn_zig_progress_t;

void spn_zig_progress_init(spn_zig_progress_t* progress);
bool spn_zig_progress_feed(spn_zig_progress_t* progress, const u8* bytes, u64 len);

typedef struct {
  spn_cc_output_kind_t kind;
  spn_lang_t lang;
  sp_da(sp_str_t) system_libs;
} spn_zig_stub_t;

sp_da(spn_zig_stub_t) spn_zig_stubs(sp_mem_t mem, spn_os_t os, sp_da(spn_zig_stub_t) links);

#endif
