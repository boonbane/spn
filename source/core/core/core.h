#ifndef SPN_CORE_CORE_H
#define SPN_CORE_CORE_H

#include "core/types.h"

spn_err_t spn_fs_update_file(sp_path_t from, sp_path_t to);

void spn_wake_ring(spn_wake_t* wake);
void spn_wake_pulse(spn_wake_t* wake);
void spn_wake_rearm(spn_wake_t* wake);

void           spn_children_add(spn_children_t* children, spn_child_t* child);
void           spn_children_remove(spn_children_t* children, spn_child_t* child);
void           spn_children_kill(spn_children_t* children);
sp_ps_output_t spn_children_run(spn_children_t* children, sp_mem_t mem, sp_ps_config_t config);

#endif
