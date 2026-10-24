#include "toml/loader.h"

#include "external/tom.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "sp.h"

spn_toml_loader_t spn_toml_loader(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots) {
  return (spn_toml_loader_t) {
    .mem = mem,
    .intern = intern,
    .roots = roots,
    .issues = sp_da_new(mem, spn_toml_issue_t),
  };
}

void spn_toml_loader_push_key(spn_toml_loader_t* ctx, const c8* key) {
  if (ctx->path.depth >= SPN_TOML_PATH_MAX) {
    return;
  }
  ctx->path.segs[ctx->path.depth] = (spn_toml_seg_t) { .kind = SPN_TOML_SEG_KEY, .key = key };
  ctx->path.depth += 1;
}

void spn_toml_loader_push_index(spn_toml_loader_t* ctx, u32 index) {
  if (ctx->path.depth >= SPN_TOML_PATH_MAX) {
    return;
  }
  ctx->path.segs[ctx->path.depth] = (spn_toml_seg_t) { .kind = SPN_TOML_SEG_INDEX, .index = index };
  ctx->path.depth += 1;
}

void spn_toml_loader_pop(spn_toml_loader_t* ctx) {
  if (ctx->path.depth) {
    ctx->path.depth -= 1;
  }
}

void spn_toml_loader_push_scope(spn_toml_loader_t* ctx, sp_str_t name) {
  ctx->scope = (spn_toml_scope_t) { .name = name, .depth = ctx->path.depth };
}

void spn_toml_loader_pop_scope(spn_toml_loader_t* ctx) {
  ctx->scope = sp_zero_s(spn_toml_scope_t);
}

sp_str_t spn_toml_path_str(sp_mem_t mem, const spn_toml_path_t* path) {
  sp_io_dyn_mem_writer_t writer;
  sp_io_dyn_mem_writer_init(mem, &writer);
  for (u32 it = path->scope; it < path->depth; it++) {
    const spn_toml_seg_t* seg = &path->segs[it];
    if (seg->kind == SPN_TOML_SEG_KEY) {
      sp_fmt_io(&writer.base, it > path->scope ? ".{}" : "{}", sp_fmt_cstr(seg->key));
    } else {
      sp_fmt_io(&writer.base, "[{}]", sp_fmt_uint(seg->index));
    }
  }
  return sp_io_dyn_mem_writer_as_str(&writer);
}

static void record(spn_toml_loader_t* ctx, spn_toml_issue_t issue) {
  issue.at = ctx->path;
  issue.at.scope = ctx->scope.depth;
  issue.detail = sp_str_copy(ctx->mem, issue.detail);
  issue.value = sp_str_copy(ctx->mem, issue.value);
  issue.scope = sp_str_copy(ctx->mem, ctx->scope.name);
  sp_da_push(ctx->issues, issue);
}

bool spn_toml_loader_issue_with(spn_toml_loader_t* ctx, const c8* key, spn_toml_issue_t issue) {
  spn_toml_loader_push_key(ctx, key);
  record(ctx, issue);
  spn_toml_loader_pop(ctx);
  return true;
}

bool spn_toml_loader_issue(spn_toml_loader_t* ctx, spn_err_t code, const c8* key) {
  return spn_toml_loader_issue_with(ctx, key, (spn_toml_issue_t) { .code = code, .detail = sp_cstr_as_str(key) });
}

bool spn_toml_loader_issue_at(spn_toml_loader_t* ctx, spn_err_t code, sp_str_t detail) {
  record(ctx, (spn_toml_issue_t) { .code = code, .detail = detail });
  return true;
}

sp_str_t spn_toml_loader_intern(spn_toml_loader_t* ctx, sp_str_t value) {
  return sp_intern(ctx->intern, value).str;
}

sp_str_t spn_toml_loader_intern_value(spn_toml_loader_t* ctx, toml_value_t value) {
  sp_str_t str = sp_str(value.u.s, (u32)value.u.sl);
  sp_str_t interned = sp_intern(ctx->intern, str).str;
  free(value.u.s);
  return interned;
}

bool spn_toml_loader_str_present(toml_table_t* table, const c8* key) {
  toml_value_t value = toml_table_string(table, key);
  if (value.ok) {
    free(value.u.s);
  }
  return value.ok;
}

static bool read_str(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key, sp_str_t* value) {
  toml_value_t found = toml_table_string(table, key);
  if (!found.ok) {
    return false;
  }
  *value = spn_toml_loader_intern_value(ctx, found);
  return true;
}

bool spn_toml_loader_field_present(toml_table_t* table, const c8* key) {
  return toml_table_array(table, key) || toml_table_table(table, key) || toml_table_unparsed(table, key);
}

sp_str_t spn_toml_loader_str_required(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key) {
  sp_str_t value = sp_zero;
  if (!read_str(ctx, table, key, &value)) {
    spn_toml_loader_issue(ctx, spn_toml_loader_field_present(table, key) ? SPN_ERR_CODEGEN_EXPECTED_STR : SPN_ERR_CODEGEN_MISSING_KEY, key);
  }
  return value;
}

bool spn_toml_loader_str_optional(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key, sp_str_t* value) {
  return read_str(ctx, table, key, value);
}

bool spn_toml_loader_read_bool(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key, bool* value) {
  toml_value_t found = toml_table_bool(table, key);
  if (!found.ok) {
    return false;
  }
  *value = found.u.b;
  return true;
}

bool spn_toml_loader_read_u64(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key, u64* value) {
  toml_value_t found = toml_table_int(table, key);
  if (found.ok) {
    if (found.u.i < 0) {
      spn_toml_loader_issue(ctx, SPN_ERR_CODEGEN_EXPECTED_INT, key);
      return false;
    }
    *value = (u64)found.u.i;
    return true;
  }

  if (spn_toml_loader_field_present(table, key)) {
    spn_toml_loader_issue(ctx, SPN_ERR_CODEGEN_EXPECTED_INT, key);
  }
  return false;
}

void spn_toml_loader_check_keys(spn_toml_loader_t* ctx, toml_table_t* table, const c8** keys, u32 count) {
  sp_for(it, (u32)toml_table_len(table)) {
    s32 len = 0;
    const c8* name = toml_table_key(table, (s32)it, &len);
    if (!name) {
      break;
    }
    bool known = false;
    sp_for(kt, count) {
      if (sp_str_equal(sp_str(name, (u32)len), sp_cstr_as_str(keys[kt]))) {
        known = true;
        break;
      }
    }
    if (!known) {
      spn_toml_loader_issue_at(ctx, SPN_ERR_CODEGEN_UNKNOWN_KEY, sp_str(name, (u32)len));
    }
  }
}

sp_da(sp_str_t) spn_toml_loader_read_str_array(spn_toml_loader_t* ctx, toml_table_t* table, const c8* key) {
  toml_array_t* array = toml_table_array(table, key);
  if (!array) {
    return SP_NULLPTR;
  }

  sp_da(sp_str_t) values = sp_da_new(ctx->mem, sp_str_t);
  spn_toml_loader_push_key(ctx, key);
  sp_for(it, (u32)toml_array_len(array)) {
    spn_toml_loader_push_index(ctx, it);
    toml_value_t element = toml_array_string(array, (s32)it);
    if (element.ok) {
      sp_da_push(values, spn_toml_loader_intern_value(ctx, element));
    } else {
      record(ctx, (spn_toml_issue_t) { .code = SPN_ERR_CODEGEN_EXPECTED_STR });
    }
    spn_toml_loader_pop(ctx);
  }
  spn_toml_loader_pop(ctx);
  return values;
}

toml_table_t* spn_toml_loader_parse(spn_toml_loader_t* ctx, spn_path_t path) {
  sp_str_t diag = sp_zero;
  toml_table_t* table = spn_toml_parse_diag(ctx->mem, spn_path_at(ctx->roots, path), &diag);
  if (!table) {
    spn_toml_loader_issue_at(ctx, SPN_ERR_CODEGEN_PARSE, diag);
  }
  return table;
}

spn_err_union_t spn_toml_loader_err(const spn_toml_loader_t* ctx, spn_path_t path) {
  return (spn_err_union_t) {
    .kind = SPN_ERR_MANIFEST_ISSUES,
    .manifest = { .path = spn_path_str(ctx->roots, ctx->mem, path), .issues = ctx->issues },
  };
}
