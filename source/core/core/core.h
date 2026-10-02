#ifndef SPN_CORE_CORE_H
#define SPN_CORE_CORE_H

#include "core/types.h"

spn_err_t spn_fs_update_file(sp_path_t from, sp_path_t to);
spn_err_t spn_fs_update_glob(sp_path_t from, sp_path_t to);

void spn_wake_ring(spn_wake_t* wake);
void spn_wake_pulse(spn_wake_t* wake);
void spn_wake_rearm(spn_wake_t* wake);

#endif
