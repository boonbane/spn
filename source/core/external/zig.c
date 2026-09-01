#include "external/zig.h"

#include "compiler/driver.h"
#include "enum/enum.h"
#include "hash/digest/digest.h"

static u64 packet_size(u32 count) {
  return 1 + ((u64)count * (SPN_ZIG_PROGRESS_NODE_SIZE + 1));
}

static u64 live_ticks(const spn_zig_progress_t* progress) {
  u64 ticks = 0;
  sp_for(it, progress->count) {
    const spn_zig_node_t* node = &progress->nodes[it];
    if (node->total == SPN_ZIG_PROGRESS_IPC) { continue; }
    if (node->parent >= progress->count) { continue; }
    if (progress->nodes[node->parent].parent != SPN_ZIG_PROGRESS_ROOT) { continue; }
    ticks += node->completed;
  }
  return ticks;
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

  u64 live = live_ticks(progress);
  if (live > progress->live) {
    progress->ticks += live - progress->live;
  }
  progress->live = live;
  progress->packets++;
}

static bool drain(spn_zig_progress_t* progress) {
  u64 offset = 0;
  while (progress->fill - offset > 0) {
    u64 need = packet_size(progress->pending[offset]);
    if (progress->fill - offset < need) {
      break;
    }
    decode(progress, progress->pending + offset);
    offset += need;
  }

  progress->fill -= offset;
  if (offset && progress->fill) {
    sp_mem_move(progress->pending, progress->pending + offset, progress->fill);
  }
  return offset > 0;
}

void spn_zig_progress_init(spn_zig_progress_t* progress) {
  *progress = sp_zero_s(spn_zig_progress_t);
}

u64 spn_zig_progress_ticks(const spn_zig_progress_t* progress) {
  return progress->ticks;
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

static sp_da(sp_str_t) canonical_libs(sp_mem_t mem, sp_da(sp_str_t) libs) {
  sp_da(sp_str_t) sorted = sp_da_new(mem, sp_str_t);
  sp_da_reserve(sorted, sp_da_size(libs));
  sp_da_for(libs, it) {
    sp_da_push(sorted, libs[it]);
  }
  sp_da_sort(sorted, sp_str_sort_kernel_alphabetical);

  sp_da(sp_str_t) unique = sp_da_new(mem, sp_str_t);
  sp_da_for(sorted, it) {
    if (it && sp_str_equal(sorted[it], sorted[it - 1])) { continue; }
    sp_da_push(unique, sorted[it]);
  }
  return unique;
}

spn_zig_stub_t spn_zig_stub_canonical(sp_mem_t mem, spn_os_t os, spn_zig_stub_t link) {
  sp_assert(link.kind != SPN_CC_OUTPUT_OBJECT);
  sp_assert(link.kind != SPN_CC_OUTPUT_STATIC_LIB);
  sp_assert(link.lang != SPN_LANG_ASM);

  spn_zig_stub_t stub = {
    .kind = link.kind,
    .lang = link.lang,
  };
  if (link.kind == SPN_CC_OUTPUT_EXE && link.linkage == SPN_LIB_KIND_STATIC && os != SPN_OS_MACOS) {
    stub.linkage = link.linkage;
  }
  if (os == SPN_OS_WINDOWS) {
    stub.system_libs = canonical_libs(mem, link.system_libs);
  }
  return stub;
}

static sp_str_t stub_kind_label(spn_cc_output_kind_t kind) {
  switch (kind) {
    case SPN_CC_OUTPUT_EXE: return sp_str_lit("exe");
    case SPN_CC_OUTPUT_SHARED_LIB: return sp_str_lit("shared");
    case SPN_CC_OUTPUT_REACTOR: return sp_str_lit("reactor");
    case SPN_CC_OUTPUT_OBJECT:
    case SPN_CC_OUTPUT_STATIC_LIB: {
      sp_unreachable_case();
    }
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

sp_str_t spn_zig_stub_name(sp_mem_t mem, sp_str_t triple, spn_sanitizer_set_t sanitizers, const spn_zig_stub_t* stub) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_da(sp_str_t) parts = sp_da_new(s.mem, sp_str_t);
  sp_da_push(parts, triple);
  sp_da_push(parts, stub_kind_label(stub->kind));
  sp_da_push(parts, stub->lang == SPN_LANG_CXX ? sp_str_lit("cxx") : sp_str_lit("c"));
  if (stub->linkage == SPN_LIB_KIND_STATIC) {
    sp_da_push(parts, sp_str_lit("static"));
  }
  if (sanitizers) {
    sp_da_push(parts, spn_sanitizer_set_to_str(s.mem, sanitizers));
  }
  sp_da_for(stub->system_libs, it) {
    sp_da_push(parts, stub->system_libs[it]);
  }
  sp_str_t name = sp_str_join_n(mem, parts, sp_da_size(parts), sp_str_lit("."));
  sp_mem_end_scratch(s);
  return name;
}
