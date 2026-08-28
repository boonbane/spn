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

static bool stub_equal(const spn_zig_stub_t* a, const spn_zig_stub_t* b) {
  if (a->kind != b->kind || a->lang != b->lang) return false;
  if (sp_da_size(a->system_libs) != sp_da_size(b->system_libs)) return false;
  sp_da_for(a->system_libs, it) {
    if (!sp_str_equal(a->system_libs[it], b->system_libs[it])) return false;
  }
  return true;
}

static sp_da(sp_str_t) canonical_libs(sp_mem_t mem, sp_da(sp_str_t) libs) {
  sp_da(sp_str_t) sorted = sp_da_new(mem, sp_str_t);
  sp_da_reserve(sorted, sp_da_size(libs));
  sp_da_for(libs, it) {
    sp_da_push(sorted, libs[it]);
  }
  sp_da_sort(sorted, sp_str_sort_kernel_alphabetical);

  sp_da(sp_str_t) unique = sp_da_new(mem, sp_str_t);
  sp_da_for(sorted, it) {
    if (it && sp_str_equal(sorted[it], sorted[it - 1])) continue;
    sp_da_push(unique, sorted[it]);
  }
  return unique;
}

sp_da(spn_zig_stub_t) spn_zig_stubs(sp_mem_t mem, spn_os_t os, sp_da(spn_zig_stub_t) links) {
  sp_da(spn_zig_stub_t) stubs = sp_da_new(mem, spn_zig_stub_t);
  sp_da_for(links, it) {
    sp_assert(links[it].kind != SPN_CC_OUTPUT_OBJECT);
    sp_assert(links[it].kind != SPN_CC_OUTPUT_STATIC_LIB);
    sp_assert(links[it].lang != SPN_LANG_ASM);

    spn_zig_stub_t stub = {
      .kind = links[it].kind,
      .lang = links[it].lang,
    };
    if (os == SPN_OS_WINDOWS) {
      stub.system_libs = canonical_libs(mem, links[it].system_libs);
    }

    bool seen = false;
    sp_da_for(stubs, jt) {
      if (stub_equal(&stubs[jt], &stub)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      sp_da_push(stubs, stub);
    }
  }
  return stubs;
}
