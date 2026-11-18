#if defined(__linux__) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include "cpu/cpu.h"

static u64 cpu_max(sp_mem_t mem, sp_path_t dir) {
  sp_str_t content = sp_zero;
  if (sp_fs_read_at(mem, sp_path_join(mem, dir, sp_str_lit("cpu.max")), &content)) {
    return SP_LIMIT_U64_MAX;
  }

  sp_str_pair_t fields = sp_str_cleave_c8(sp_str_trim(content), ' ');
  u64 limit = 0;
  u64 period = 0;
  if (!sp_parse_u64_ex(fields.first, &limit) || !sp_parse_u64_ex(fields.second, &period) || !period) {
    return SP_LIMIT_U64_MAX;
  }
  return limit / period;
}

u64 spn_cpu_cgroup_quota(sp_mem_t mem, sp_path_t proc, sp_path_t root) {
  sp_str_t content = sp_zero;
  if (sp_fs_read_at(mem, proc, &content)) {
    return SP_LIMIT_U64_MAX;
  }

  sp_str_t prefix = sp_str_lit("0::");
  sp_str_for_split(content, '\n', line) {
    if (!sp_str_starts_with(line.value, prefix)) {
      continue;
    }

    sp_path_t dir = root;
    u64 quota = cpu_max(mem, dir);
    sp_str_for_split(sp_str_skip(line.value, (s32)prefix.len), '/', name) {
      if (sp_str_empty(name.value)) {
        continue;
      }
      dir = sp_path_join(mem, dir, name.value);
      quota = sp_min(quota, cpu_max(mem, dir));
    }
    return quota;
  }
  return SP_LIMIT_U64_MAX;
}

#if defined(SP_LINUX)
  #include <sched.h>
  #include <unistd.h>

static u32 affinity_count() {
  cpu_set_t set = sp_zero;
  if (!sched_getaffinity(0, sizeof(set), &set)) {
    u32 count = (u32)CPU_COUNT(&set);
    if (count) {
      return count;
    }
  }
  long online = sysconf(_SC_NPROCESSORS_ONLN);
  return online > 0 ? (u32)online : 1;
}

u32 spn_cpu_count() {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  u64 quota = spn_cpu_cgroup_quota(scratch.mem, sp_path_from_str(sp_str_lit("/proc/self/cgroup")), sp_path_from_str(sp_str_lit("/sys/fs/cgroup")));
  sp_mem_end_scratch(scratch);
  return (u32)sp_max(sp_min((u64)affinity_count(), quota), 1);
}

#elif defined(SP_MACOS)
  #include <sys/sysctl.h>

u32 spn_cpu_count() {
  s32 count = 0;
  size_t len = sizeof(count);
  if (!sysctlbyname("hw.activecpu", &count, &len, SP_NULLPTR, 0) && count > 0) {
    return (u32)count;
  }
  return 1;
}

#elif defined(SP_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>

u32 spn_cpu_count() {
  DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return count ? (u32)count : 1;
}

#else
  #error "spn_cpu_count: unsupported platform"
#endif
