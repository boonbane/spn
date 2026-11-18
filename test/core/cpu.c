#include "spn_test.h"

#include "cpu/cpu.h"

#define CPU_MAX_FILES 4

typedef struct {
  const c8* path;
  const c8* content;
} cpu_file_t;

typedef struct {
  const c8* name;
  const c8* cgroup;
  cpu_file_t files [CPU_MAX_FILES];
  u64 quota;
} cpu_quota_test_t;

static const cpu_quota_test_t cpu_quota_tests [] = {
  {
    .name = "no_proc",
    .quota = SP_LIMIT_U64_MAX,
  },
  {
    .name = "unlimited",
    .cgroup = "0::/a\n",
    .files = {
      { "a/cpu.max", "max 100000\n" },
    },
    .quota = SP_LIMIT_U64_MAX,
  },
  {
    .name = "no_cpu_max",
    .cgroup = "0::/a/b\n",
    .quota = SP_LIMIT_U64_MAX,
  },
  {
    .name = "leaf",
    .cgroup = "0::/a/b\n",
    .files = {
      { "a/b/cpu.max", "200000 100000\n" },
    },
    .quota = 2,
  },
  {
    .name = "ancestor",
    .cgroup = "0::/a/b\n",
    .files = {
      { "a/cpu.max", "300000 100000\n" },
      { "a/b/cpu.max", "max 100000\n" },
    },
    .quota = 3,
  },
  {
    .name = "tightest",
    .cgroup = "0::/a/b/c\n",
    .files = {
      { "a/cpu.max", "800000 100000\n" },
      { "a/b/cpu.max", "200000 100000\n" },
      { "a/b/c/cpu.max", "400000 100000\n" },
    },
    .quota = 2,
  },
  {
    .name = "namespace_root",
    .cgroup = "0::/\n",
    .files = {
      { "cpu.max", "400000 100000\n" },
    },
    .quota = 4,
  },
  {
    .name = "floor",
    .cgroup = "0::/a\n",
    .files = {
      { "a/cpu.max", "250000 100000\n" },
    },
    .quota = 2,
  },
  {
    .name = "below_one",
    .cgroup = "0::/a\n",
    .files = {
      { "a/cpu.max", "50000 100000\n" },
    },
    .quota = 0,
  },
  {
    .name = "hybrid",
    .cgroup = "12:cpu,cpuacct:/a\n1:name=systemd:/a\n0::/b\n",
    .files = {
      { "a/cpu.max", "100000 100000\n" },
      { "b/cpu.max", "300000 100000\n" },
    },
    .quota = 3,
  },
  {
    .name = "v1_only",
    .cgroup = "12:cpu,cpuacct:/a\n",
    .files = {
      { "a/cpu.max", "100000 100000\n" },
    },
    .quota = SP_LIMIT_U64_MAX,
  },
  {
    .name = "malformed",
    .cgroup = "0::/a\n",
    .files = {
      { "a/cpu.max", "100000\n" },
    },
    .quota = SP_LIMIT_U64_MAX,
  },
  {
    .name = "zero_period",
    .cgroup = "0::/a\n",
    .files = {
      { "a/cpu.max", "100000 0\n" },
    },
    .quota = SP_LIMIT_U64_MAX,
  },
};

sp_test_each(cpu, cgroup_quota, cpu_quota_test_t, cpu_quota_tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_path_t dir = sp_test_dir(t);
  sp_path_t proc = sp_path_join(mem, dir, sp_str_lit("cgroup"));
  sp_path_t root = sp_path_join(mem, dir, sp_str_lit("fs"));

  sp_must_ok(t, sp_fs_create_dir_at(root));
  if (it->cgroup) {
    sp_must_ok(t, sp_fs_write_cstr_at(proc, it->cgroup, SP_FS_WRITE_TRUNCATE));
  }

  sp_carr_for(it->files, i) {
    cpu_file_t file = it->files[i];
    if (!file.path) {
      break;
    }

    sp_path_t path = sp_path_join(mem, root, sp_cstr_as_str(file.path));
    sp_must_ok(t, sp_fs_create_dir_at(sp_path_parent(mem, path)));
    sp_must_ok(t, sp_fs_write_cstr_at(path, file.content, SP_FS_WRITE_TRUNCATE));
  }

  sp_expect_eq(t, spn_cpu_cgroup_quota(mem, proc, root), it->quota);

  return SP_OK;
}
