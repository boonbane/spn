#ifndef SPN_LOCK_LOCK_H
#define SPN_LOCK_LOCK_H

#include "spn/errors.h"
#include "lock/types.h"
#include "paths/types.h"

spn_err_t         spn_lock_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t path, spn_lock_t* lock, spn_err_union_t* err);
spn_err_t         spn_lock_save(const spn_path_roots_t* roots, spn_path_t path, const spn_lock_t* lock, spn_err_union_t* err);
spn_lock_t        spn_lock_new(sp_mem_t mem);
bool              spn_lock_insert(spn_lock_t* lock, spn_lock_entry_t entry);
spn_lock_index_t* spn_lock_find_index(const spn_lock_t* lock, sp_intern_id_t name);
void              spn_lock_pin_index(spn_lock_t* lock, spn_lock_index_t index);
spn_lock_diff_t   spn_lock_diff(const spn_lock_t* held, const spn_lock_t* next);
bool              spn_lock_diff_next(spn_lock_diff_t* diff);
spn_err_union_t   spn_lock_mismatch(const spn_lock_entry_t* was, const spn_lock_entry_t* now);
si_da(si_lock_move_t) si_lock_merge(sp_mem_t mem, spn_lock_t* lock, si_da(si_lock_pick_t) picks);
bool              si_lock_changed(const spn_lock_t* held, const spn_lock_t* next, sp_str_t* what);

#endif
