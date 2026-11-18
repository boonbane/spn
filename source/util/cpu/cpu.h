#ifndef SPN_CPU_CPU_H
#define SPN_CPU_CPU_H

#include "sp.h"

u32 spn_cpu_count();
u64 spn_cpu_cgroup_quota(sp_mem_t mem, sp_path_t proc, sp_path_t root);

#endif
