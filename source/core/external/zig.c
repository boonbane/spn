#include "external/zig.h"

static u64 packet_size(u32 count) {
  return 1 + ((u64)count * (SPN_ZIG_PROGRESS_NODE_SIZE + 1));
}

static void decode(spn_zig_progress_t* progress, const u8* packet) {
  u32 count = packet[0];
  sp_mem_copy(progress->snapshot, packet, packet_size(count));

  const u8* storage = progress->snapshot + 1;
  const u8* parents = storage + ((u64)count * SPN_ZIG_PROGRESS_NODE_SIZE);

  progress->count = count;
  sp_for(it, count) {
    const u8* base = storage + ((u64)it * SPN_ZIG_PROGRESS_NODE_SIZE);

    spn_zig_node_t* node = &progress->nodes[it];
    sp_mem_copy(&node->completed, base, sizeof(u32));
    sp_mem_copy(&node->total, base + sizeof(u32), sizeof(u32));
    node->parent = parents[it];

    const c8* name = (const c8*)(base + (2 * sizeof(u32)));
    node->name = sp_str(name, sp_cstr_len_n(name, SPN_ZIG_PROGRESS_NAME_CAP));
  }
}

static bool drain(spn_zig_progress_t* progress) {
  u64 offset = 0;
  u64 last = 0;
  bool complete = false;

  while (progress->fill - offset > 0) {
    u64 need = packet_size(progress->pending[offset]);
    if (progress->fill - offset < need) {
      break;
    }

    last = offset;
    complete = true;
    progress->packets++;
    offset += need;
  }

  if (complete) {
    decode(progress, progress->pending + last);
  }

  progress->fill -= offset;
  if (offset && progress->fill) {
    sp_mem_move(progress->pending, progress->pending + offset, progress->fill);
  }
  return complete;
}

void spn_zig_progress_init(spn_zig_progress_t* progress) {
  *progress = sp_zero_s(spn_zig_progress_t);
}

bool spn_zig_progress_feed(spn_zig_progress_t* progress, const u8* bytes, u64 len) {
  bool advanced = false;
  while (len) {
    u64 space = sizeof(progress->pending) - progress->fill;
    u64 take = sp_min(len, space);
    sp_mem_copy(progress->pending + progress->fill, bytes, take);
    progress->fill += take;
    bytes += take;
    len -= take;
    advanced |= drain(progress);
  }
  return advanced;
}
