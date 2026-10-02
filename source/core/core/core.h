#ifndef SPN_CORE_CORE_H
#define SPN_CORE_CORE_H

#include "core/types.h"
#include "paths/types.h"

bool        spn_fs_owned(sp_da(spn_path_t) owned, spn_path_t dir, sp_str_t rel);
spn_fs_it_t spn_fs_it_new(sp_mem_t mem, const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t dir);
bool        spn_fs_it_next(spn_fs_it_t* it);
bool        spn_fs_it_walk(spn_fs_it_t* it);
void        spn_fs_it_deinit(spn_fs_it_t* it);

spn_err_t spn_fs_update_file(sp_path_t from, sp_path_t to);
spn_err_t spn_fs_update_tree(const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t from, sp_path_t to);
spn_err_t spn_fs_update_glob(const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t from, sp_path_t to);

void spn_wake_ring(spn_wake_t* wake);
void spn_wake_pulse(spn_wake_t* wake);
void spn_wake_rearm(spn_wake_t* wake);

#endif
