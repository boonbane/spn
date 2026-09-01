#include "spn_test.h"

#include "external/zig.h"

#define ZIG_TEST_MAX_NODES 4
#define ZIG_TEST_MAX_PACKETS 3
#define ZIG_TEST_STREAM_CAP (ZIG_TEST_MAX_PACKETS * (1 + (ZIG_TEST_MAX_NODES * (SPN_ZIG_PROGRESS_NODE_SIZE + 1))))

typedef struct {
  u32 completed;
  u32 total;
  u8 parent;
  const c8* name;
} node_t;

typedef struct {
  node_t nodes [ZIG_TEST_MAX_NODES];
} packet_t;

typedef struct {
  bool advanced;
  u64 packets;
  u64 ticks;
  node_t nodes [ZIG_TEST_MAX_NODES];
} expect_t;

typedef struct {
  const c8* name;
  packet_t packets [ZIG_TEST_MAX_PACKETS];
  u64 hold;
  expect_t expect;
} test_t;

static u64 encode(u8* out, const node_t* nodes, u32 count) {
  out[0] = (u8)count;
  u8* storage = out + 1;
  u8* parents = storage + ((u64)count * SPN_ZIG_PROGRESS_NODE_SIZE);
  sp_for(it, count) {
    u8* base = storage + ((u64)it * SPN_ZIG_PROGRESS_NODE_SIZE);
    sp_mem_copy(base, &nodes[it].completed, sizeof(u32));
    sp_mem_copy(base + sizeof(u32), &nodes[it].total, sizeof(u32));
    sp_mem_zero(base + (2 * sizeof(u32)), SPN_ZIG_PROGRESS_NAME_CAP);
    sp_str_t name = sp_cstr_as_str(nodes[it].name);
    sp_mem_copy(base + (2 * sizeof(u32)), name.data, sp_min(name.len, SPN_ZIG_PROGRESS_NAME_CAP));
    parents[it] = nodes[it].parent;
  }
  return 1 + ((u64)count * (SPN_ZIG_PROGRESS_NODE_SIZE + 1));
}

#define ZIG_TEST_NAME_OVERLONG \
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" \
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBB"
#define ZIG_TEST_NAME_CAPPED \
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" \
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

static const test_t frame_tests [] = {
  {
    .name = "single",
    .packets = {
      { .nodes = { { 3, 10, SPN_ZIG_PROGRESS_ROOT, "A" } } },
    },
    .expect = {
      .advanced = true,
      .packets = 1,
      .nodes = { { 3, 10, SPN_ZIG_PROGRESS_ROOT, "A" } },
    },
  },
  {
    .name = "tree",
    .packets = {
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 2, 5, 0, "B" },
        { 5, 9, 0, "C" },
        { 0, 0, SPN_ZIG_PROGRESS_UNUSED, "" },
      } },
    },
    .expect = {
      .advanced = true,
      .packets = 1,
      .ticks = 7,
      .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 2, 5, 0, "B" },
        { 5, 9, 0, "C" },
        { 0, 0, SPN_ZIG_PROGRESS_UNUSED, "" },
      },
    },
  },
  {
    .name = "ipc",
    .packets = {
      { .nodes = { { 7, SPN_ZIG_PROGRESS_IPC, SPN_ZIG_PROGRESS_ROOT, "A" } } },
    },
    .expect = {
      .advanced = true,
      .packets = 1,
      .nodes = { { 7, SPN_ZIG_PROGRESS_IPC, SPN_ZIG_PROGRESS_ROOT, "A" } },
    },
  },
  {
    .name = "ticks_skip_ipc_and_deep",
    .packets = {
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 2, 5, 0, "B" },
        { 7, SPN_ZIG_PROGRESS_IPC, 0, "I" },
        { 9, 9, 1, "D" },
      } },
    },
    .expect = {
      .advanced = true,
      .packets = 1,
      .ticks = 2,
      .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 2, 5, 0, "B" },
        { 7, SPN_ZIG_PROGRESS_IPC, 0, "I" },
        { 9, 9, 1, "D" },
      },
    },
  },
  {
    .name = "last_wins",
    .packets = {
      { .nodes = { { 1, 4, SPN_ZIG_PROGRESS_ROOT, "A" } } },
      { .nodes = { { 2, 4, SPN_ZIG_PROGRESS_ROOT, "B" } } },
    },
    .expect = {
      .advanced = true,
      .packets = 2,
      .nodes = { { 2, 4, SPN_ZIG_PROGRESS_ROOT, "B" } },
    },
  },
  {
    .name = "ticks_survive_ended_node",
    .packets = {
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 1000, 0, 0, "B" },
        { 1, 0, 0, "C" },
      } },
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 1, 0, 0, "C" },
      } },
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 3, 0, 0, "C" },
      } },
    },
    .expect = {
      .advanced = true,
      .packets = 3,
      .ticks = 1003,
      .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 3, 0, 0, "C" },
      },
    },
  },
  {
    .name = "ticks_count_every_packet",
    .packets = {
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 1000, 0, 0, "B" },
      } },
      { .nodes = { { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" } } },
      { .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 400, 0, 0, "D" },
      } },
    },
    .expect = {
      .advanced = true,
      .packets = 3,
      .ticks = 1400,
      .nodes = {
        { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
        { 400, 0, 0, "D" },
      },
    },
  },
  {
    .name = "partial_tail",
    .packets = {
      { .nodes = { { 1, 4, SPN_ZIG_PROGRESS_ROOT, "A" } } },
      { .nodes = { { 2, 4, SPN_ZIG_PROGRESS_ROOT, "B" } } },
    },
    .hold = 10,
    .expect = {
      .advanced = true,
      .packets = 1,
      .nodes = { { 1, 4, SPN_ZIG_PROGRESS_ROOT, "A" } },
    },
  },
  {
    .name = "no_packet",
    .packets = {
      { .nodes = { { 1, 4, SPN_ZIG_PROGRESS_ROOT, "A" } } },
    },
    .hold = 1,
  },
  {
    .name = "name_cap",
    .packets = {
      { .nodes = { { 1, 2, SPN_ZIG_PROGRESS_ROOT, ZIG_TEST_NAME_OVERLONG } } },
    },
    .expect = {
      .advanced = true,
      .packets = 1,
      .nodes = { { 1, 2, SPN_ZIG_PROGRESS_ROOT, ZIG_TEST_NAME_CAPPED } },
    },
  },
};

sp_test_each(zig_decode, frames, test_t, frame_tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_zig_progress_t* progress = sp_alloc_type(mem, spn_zig_progress_t);
  spn_zig_progress_init(progress);

  u8 stream [ZIG_TEST_STREAM_CAP];
  u64 len = 0;
  u32 num_packets = 0;
  sp_carr_detect_len(it->packets, num_packets, it->packets[num_packets].nodes[0].name);
  sp_for(p, num_packets) {
    const packet_t* packet = &it->packets[p];
    u32 count = 0;
    sp_carr_detect_len(packet->nodes, count, packet->nodes[count].name);
    len += encode(stream + len, packet->nodes, count);
  }

  bool advanced = spn_zig_progress_feed(progress, stream, len - it->hold);
  sp_expect_eq(t, advanced, it->expect.advanced);
  sp_expect_eq(t, progress->packets, it->expect.packets);
  sp_expect_eq(t, spn_zig_progress_ticks(progress), it->expect.ticks);

  u32 expected = 0;
  sp_carr_detect_len(it->expect.nodes, expected, it->expect.nodes[expected].name);
  sp_must_eq(t, progress->count, expected);
  sp_for(n, expected) {
    const node_t* node = &it->expect.nodes[n];
    sp_test_kv_c(t, "node", node->name);
    sp_expect_eq(t, progress->nodes[n].completed, node->completed);
    sp_expect_eq(t, progress->nodes[n].total, node->total);
    sp_expect_eq(t, progress->nodes[n].parent, node->parent);
    sp_expect_str_eq_c(t, progress->nodes[n].name, node->name);
  }

  return SP_OK;
}

sp_test(zig_decode, split) {
  sp_mem_t mem = sp_test_arena(t);

  node_t a [] = {
    { 1, 0, SPN_ZIG_PROGRESS_ROOT, "A" },
    { 2, 8, 0, "B" },
  };
  node_t b [] = {
    { 3, 8, SPN_ZIG_PROGRESS_ROOT, "C" },
  };

  u8 stream [ZIG_TEST_STREAM_CAP];
  u64 len = 0;
  len += encode(stream + len, a, sp_carr_len(a));
  len += encode(stream + len, SP_NULLPTR, 0);
  len += encode(stream + len, b, sp_carr_len(b));

  spn_zig_progress_t* progress = sp_alloc_type(mem, spn_zig_progress_t);
  sp_for(split, len) {
    spn_zig_progress_init(progress);

    sp_test_kv(t, "split", sp_fmt(mem, "{}", sp_fmt_uint(split)).value);
    spn_zig_progress_feed(progress, stream, split);
    bool advanced = spn_zig_progress_feed(progress, stream + split, len - split);
    sp_expect(t, advanced);
    sp_must_eq(t, progress->packets, 3);
    sp_must_eq(t, progress->count, 1);
    sp_expect_str_eq_c(t, progress->nodes[0].name, "C");
    sp_expect_eq(t, progress->nodes[0].completed, 3);
    sp_expect_eq(t, spn_zig_progress_ticks(progress), 2);
  }

  spn_zig_progress_init(progress);
  sp_for(byte, len) {
    spn_zig_progress_feed(progress, stream + byte, 1);
  }
  sp_must_eq(t, progress->packets, 3);
  sp_expect_str_eq_c(t, progress->nodes[0].name, "C");
  sp_expect_eq(t, spn_zig_progress_ticks(progress), 2);

  node_t full [SPN_ZIG_PROGRESS_MAX_NODES];
  full[0] = (node_t) { 0, 0, SPN_ZIG_PROGRESS_ROOT, "A" };
  for (u32 n = 1; n < SPN_ZIG_PROGRESS_MAX_NODES; n++) {
    full[n] = (node_t) { n, 403, 0, "B" };
  }

  u8* big = sp_alloc_n(mem, u8, 3 * SPN_ZIG_PROGRESS_PACKET_MAX);
  u64 big_len = 0;
  sp_for(p, 3) {
    full[0].completed = p;
    big_len += encode(big + big_len, full, SPN_ZIG_PROGRESS_MAX_NODES);
  }
  sp_expect(t, big_len > sizeof(progress->pending));

  spn_zig_progress_init(progress);
  sp_expect(t, spn_zig_progress_feed(progress, big, big_len));
  sp_must_eq(t, progress->packets, 3);
  sp_must_eq(t, progress->count, SPN_ZIG_PROGRESS_MAX_NODES);
  sp_expect_eq(t, progress->nodes[0].completed, 2);
  sp_expect_eq(t, progress->nodes[SPN_ZIG_PROGRESS_MAX_NODES - 1].completed, SPN_ZIG_PROGRESS_MAX_NODES - 1);
  sp_expect_str_eq_c(t, progress->nodes[SPN_ZIG_PROGRESS_MAX_NODES - 1].name, "B");

  return SP_OK;
}
