#define SP_IMPLEMENTATION
#include "sp.h"

#include <stdio.h>

#define FAKEZIG_NODE_SIZE 128
#define FAKEZIG_NAME_CAP (FAKEZIG_NODE_SIZE - (2 * sizeof(u32)))
#define FAKEZIG_MAX_NODES 2
#define FAKEZIG_MAX_PACKETS 3
#define FAKEZIG_MAX_INPUTS 16

typedef struct {
  u32 completed;
  u32 total;
  u8 parent;
  const c8* name;
} node_t;

typedef struct {
  bool compile;
  bool fail;
  sp_str_t output;
  sp_str_t depfile;
  sp_str_t inputs [FAKEZIG_MAX_INPUTS];
  u32 num_inputs;
} args_t;

static u64 encode(u8* out, const node_t* nodes, u32 count) {
  out[0] = (u8)count;
  u8* storage = out + 1;
  u8* parents = storage + ((u64)count * FAKEZIG_NODE_SIZE);
  sp_for(it, count) {
    u8* base = storage + ((u64)it * FAKEZIG_NODE_SIZE);
    sp_mem_copy(base, &nodes[it].completed, sizeof(u32));
    sp_mem_copy(base + sizeof(u32), &nodes[it].total, sizeof(u32));
    sp_mem_zero(base + (2 * sizeof(u32)), FAKEZIG_NAME_CAP);
    sp_str_t name = sp_cstr_as_str(nodes[it].name);
    sp_mem_copy(base + (2 * sizeof(u32)), name.data, sp_min(name.len, FAKEZIG_NAME_CAP));
    parents[it] = nodes[it].parent;
  }
  return 1 + ((u64)count * (FAKEZIG_NODE_SIZE + 1));
}

static void report(sp_sys_fd_t fd) {
  node_t started [] = { { 0, 0, 255, "" }, { 1, 0, 0, "libc" } };
  node_t advanced [] = { { 0, 0, 255, "" }, { 3, 0, 0, "libc" } };
  node_t ended [] = { { 0, 0, 255, "" } };

  u8 buf [FAKEZIG_MAX_PACKETS * (1 + (FAKEZIG_MAX_NODES * (FAKEZIG_NODE_SIZE + 1)))];
  u64 len = 0;
  len += encode(buf + len, started, sp_carr_len(started));
  len += encode(buf + len, advanced, sp_carr_len(advanced));
  len += encode(buf + len, ended, sp_carr_len(ended));

  u64 written = 0;
  sp_sys_write(fd, buf, len, &written);
}

static args_t parse(int argc, char** argv) {
  args_t args = sp_zero;
  for (int it = 1; it < argc; it++) {
    const c8* arg = argv[it];
    if (sp_cstr_equal(arg, "-c")) {
      args.compile = true;
    }
    else if (sp_cstr_equal(arg, "--fail")) {
      args.fail = true;
    }
    else if (sp_cstr_equal(arg, "-o") && it + 1 < argc) {
      args.output = sp_cstr_as_str(argv[++it]);
    }
    else if (sp_cstr_equal(arg, "-MF") && it + 1 < argc) {
      args.depfile = sp_cstr_as_str(argv[++it]);
    }
    else if (arg[0] != '-' && args.num_inputs < FAKEZIG_MAX_INPUTS) {
      args.inputs[args.num_inputs++] = sp_cstr_as_str(arg);
    }
  }
  return args;
}

int main(int argc, char** argv) {
  sp_mem_t mem = sp_mem_heap_as_allocator(sp_mem_heap_new());
  args_t args = parse(argc, argv);

  if (!args.compile) {
    u64 fd = 0;
    if (sp_parse_u64_ex(sp_os_env_get(sp_str_lit("ZIG_PROGRESS")), &fd)) {
      report((sp_sys_fd_t)fd);
    }
    if (args.fail) {
      fputs("fakezig: refusing to link\n", stderr);
      return 3;
    }
  }

  if (!sp_str_empty(args.depfile)) {
    sp_str_t inputs = sp_str_join_n(mem, args.inputs, args.num_inputs, sp_str_lit(" "));
    sp_fs_create_file_str(args.depfile, sp_fmt(mem, "{}: {}\n", sp_fmt_str(args.output), sp_fmt_str(inputs)).value);
  }
  sp_fs_create_file_str(args.output, sp_str_lit(""));
  return 0;
}
