#pragma once

#include "spn_test.h"

#include "git/cache.h"
#include "git/key.h"
#include "git/patch.h"
#include "git/url.h"
#include "sp_template/sp_template.h"

typedef struct {
  const c8* file;
  const c8* from;
  const c8* to;
} git_edit_t;

static inline sp_err_t git_edit_write(sp_test_t* t, git_edit_t edit, sp_str_t path) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t source = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_str_lit("test/core/git/edit.patch")), &source));

  sp_template_scope_t* scope = sp_template_scope_create(mem);
  sp_template_set(scope, sp_str_lit("file"), sp_cstr_as_str(edit.file));
  sp_template_set(scope, sp_str_lit("from"), sp_cstr_as_str(edit.from));
  sp_template_set(scope, sp_str_lit("to"), sp_cstr_as_str(edit.to));

  sp_io_dyn_mem_writer_t patch = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &patch);
  sp_must_eq(t, (u32)sp_template_render(&patch.base, source, scope, SP_NULLPTR), (u32)SP_TEMPLATE_OK);
  sp_must_ok(t, sp_fs_write(path, sp_io_dyn_mem_writer_take_str(&patch), SP_FS_WRITE_TRUNCATE));
  return SP_OK;
}
