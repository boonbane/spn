#include "sp.h"
#include "macro/macro.h"
#include "tom.h"

toml_table_t* spn_toml_parse(sp_path_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t content = sp_zero;
  toml_table_t* toml = SP_NULLPTR;
  if (!sp_fs_read_at(scratch.mem, path, &content)) {
    toml = spn_toml_parse_str_diag(scratch.mem, content, SP_NULLPTR);
  }
  sp_mem_end_scratch(scratch);
  return toml;
}

toml_table_t* spn_toml_parse_str_diag(sp_mem_t mem, sp_str_t content, sp_str_t* diag) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  c8 parse_err[1024] = {0};
  toml_table_t* toml = toml_parse(sp_str_to_cstr(scratch.mem, content), parse_err, sp_carr_len(parse_err));
  sp_mem_end_scratch(scratch);

  if (!toml && diag) {
    *diag = sp_str_copy(mem, sp_cstr_as_str(parse_err));
  }

  return toml;
}
