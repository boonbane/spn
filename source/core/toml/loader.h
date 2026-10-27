#ifndef SPN_TOML_LOADER_H
#define SPN_TOML_LOADER_H

#include "sp.h"
#include "spn/core.h"
#include "spn/errors.h"
#include "spn/host.h"
#include "toml/types.h"
#include "toml.h"

spn_toml_loader_t spn_toml_loader(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots);
toml_table_t*     spn_toml_loader_parse(spn_toml_loader_t* t, spn_path_t path);
toml_table_t*     spn_toml_loader_parse_str(spn_toml_loader_t* t, sp_str_t content);
spn_err_union_t   spn_toml_loader_err(const spn_toml_loader_t* t, spn_path_t path);

void              spn_toml_loader_push_key(spn_toml_loader_t* t, const c8* key);
void              spn_toml_loader_push_index(spn_toml_loader_t* t, u32 index);
void              spn_toml_loader_pop(spn_toml_loader_t* t);
void              spn_toml_loader_push_scope(spn_toml_loader_t* t, sp_str_t name);
void              spn_toml_loader_pop_scope(spn_toml_loader_t* t);
bool              spn_toml_loader_issue(spn_toml_loader_t* t, spn_err_t code, const c8* key);
bool              spn_toml_loader_issue_at(spn_toml_loader_t* t, spn_err_t code, sp_str_t detail);
bool              spn_toml_loader_issue_with(spn_toml_loader_t* t, const c8* key, spn_toml_issue_t issue);
sp_str_t          spn_toml_loader_intern(spn_toml_loader_t* t, sp_str_t value);
sp_str_t          spn_toml_loader_intern_value(spn_toml_loader_t* t, toml_value_t value);
bool              spn_toml_loader_field_present(toml_table_t* table, const c8* key);
bool              spn_toml_loader_str_present(toml_table_t* table, const c8* key);
sp_str_t          spn_toml_loader_str_required(spn_toml_loader_t* t, toml_table_t* table, const c8* k);
bool              spn_toml_loader_str_optional(spn_toml_loader_t* t, toml_table_t* table, const c8* k, sp_str_t* v);
bool              spn_toml_loader_read_bool(spn_toml_loader_t* t, toml_table_t* table, const c8* k, bool* v);
bool              spn_toml_loader_read_u64(spn_toml_loader_t* t, toml_table_t* table, const c8* k, u64* v);
void              spn_toml_loader_check_keys(spn_toml_loader_t* t, toml_table_t* table, const c8** keys, u32 count);
sp_da(sp_str_t)   spn_toml_loader_read_str_array(spn_toml_loader_t* t, toml_table_t* table, const c8* key);

#endif
