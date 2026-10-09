#ifndef SPN_INTERN_H
#define SPN_INTERN_H

#include "intern/types.h"

sp_intern_t* sp_intern_new(sp_mem_t mem);
void sp_intern_init(sp_intern_t* intern, sp_mem_t mem);
void sp_intern_init_ex(sp_intern_t* intern, sp_mem_t mem, sp_intern_hash_fn_t hash);
sp_intern_str_t sp_intern(sp_intern_t* intern, sp_str_t str);
sp_intern_str_t sp_intern_cstr(sp_intern_t* intern, const c8* cstr);
sp_str_t sp_intern_find(sp_intern_t* intern, sp_intern_id_t id);
u64 sp_intern_get_len(sp_intern_t* intern);
u64 sp_intern_get_bytes_used(sp_intern_t* intern);
u64 sp_intern_get_bytes_allocated(sp_intern_t* intern);

sp_intern_str_t spn_intern(sp_str_t str);
sp_intern_str_t spn_intern_cstr(const c8* cstr);
sp_str_t        spn_intern_find(sp_intern_id_t id);

#define spn_intern_lit(lit) spn_intern(strl(lit))

#endif
