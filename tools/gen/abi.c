#include "gen.h"

typedef struct {
  const c8* type;
  const c8* fail;
} abi_ret_row_t;

static const abi_ret_row_t ABI_RETS[] = {
  [ABI_VAL_VOID]    = { .type = "void", .fail = "return;" },
  [ABI_VAL_S32]     = { .type = "s32",  .fail = "return SPN_ERROR;" },
  [ABI_VAL_STR]     = { .type = "u32",  .fail = "return 0;" },
  [ABI_VAL_STR_OPT] = { .type = "u32",  .fail = "return 0;" },
  [ABI_VAL_HANDLE]  = { .type = "u32",  .fail = "return 0;" },
};

static void* yj_malloc(void* ctx, size_t size) {
  return sp_alloc(*(sp_mem_t*)ctx, size);
}

static void* yj_realloc(void* ctx, void* ptr, size_t old_size, size_t size) {
  return sp_realloc(*(sp_mem_t*)ctx, ptr, old_size, size);
}

static void yj_free(void* ctx, void* ptr) {
}

yyjson_alc gen_yyjson_alc(sp_mem_t* mem) {
  return (yyjson_alc) {
    .malloc = yj_malloc,
    .realloc = yj_realloc,
    .free = yj_free,
    .ctx = mem,
  };
}

static bool abi_fail(abi_t* abi, sp_str_t where, const c8* what) {
  abi->err = sp_fmt(abi->mem, "abi: {.cyan}: {}", sp_fmt_str(where), sp_fmt_cstr(what)).value;
  return false;
}

static sp_str_t abi_str(abi_t* abi, yyjson_val* val) {
  return sp_str_from_cstr_n(abi->mem, yyjson_get_str(val), (u32)yyjson_get_len(val));
}

static bool is_handle(abi_t* abi, sp_str_t name) {
  return sp_ht_getp(abi->handles, name) != SP_NULLPTR;
}

static bool parse_val(abi_t* abi, sp_str_t where, yyjson_val* val, abi_val_t* parsed) {
  if (!yyjson_is_str(val)) {
    return abi_fail(abi, where, "expected a string");
  }
  sp_str_t name = abi_str(abi, val);
  if (sp_str_equal_cstr(name, "s32")) {
    *parsed = (abi_val_t) { .kind = ABI_VAL_S32 };
  }
  else if (sp_str_equal_cstr(name, "str")) {
    *parsed = (abi_val_t) { .kind = ABI_VAL_STR };
  }
  else if (sp_str_equal_cstr(name, "str_opt")) {
    *parsed = (abi_val_t) { .kind = ABI_VAL_STR_OPT };
  }
  else if (is_handle(abi, name)) {
    *parsed = (abi_val_t) { .kind = ABI_VAL_HANDLE, .name = name };
  }
  else {
    abi->err = sp_fmt(abi->mem, "abi: {.cyan}: unknown type {.red}", sp_fmt_str(where), sp_fmt_str(name)).value;
    return false;
  }
  return true;
}

static bool parse_export(abi_t* abi, yyjson_val* val) {
  if (!yyjson_is_obj(val)) {
    return abi_fail(abi, sp_str_lit("exports"), "export must be an object");
  }

  abi_export_t export = {
    .args = sp_da_new(abi->mem, abi_val_t),
  };
  yyjson_val* ret = SP_NULLPTR;
  yyjson_val* args = SP_NULLPTR;

  size_t idx, max;
  yyjson_val *k, *v;
  yyjson_obj_foreach(val, idx, max, k, v) {
    sp_str_t key = abi_str(abi, k);
    if (sp_str_equal_cstr(key, "name")) {
      if (!yyjson_is_str(v)) {
        return abi_fail(abi, sp_str_lit("exports"), "export name must be a string");
      }
      export.name = abi_str(abi, v);
    }
    else if (sp_str_equal_cstr(key, "host")) {
      if (!yyjson_is_str(v)) {
        return abi_fail(abi, export.name, "host must be a string");
      }
      export.host = abi_str(abi, v);
    }
    else if (sp_str_equal_cstr(key, "wasm_ctx")) {
      if (!yyjson_is_bool(v)) {
        return abi_fail(abi, export.name, "wasm_ctx must be a boolean");
      }
      export.wasm_ctx = yyjson_get_bool(v);
    }
    else if (sp_str_equal_cstr(key, "ret")) {
      ret = v;
    }
    else if (sp_str_equal_cstr(key, "args")) {
      if (!yyjson_is_arr(v)) {
        return abi_fail(abi, export.name, "args must be an array");
      }
      args = v;
    }
    else {
      abi->err = sp_fmt(abi->mem, "abi: unknown export key {.red}", sp_fmt_str(key)).value;
      return false;
    }
  }

  if (sp_str_empty(export.name)) {
    return abi_fail(abi, sp_str_lit("exports"), "export needs a name");
  }
  if (!sp_str_starts_with(export.name, sp_str_lit("spn_"))) {
    return abi_fail(abi, export.name, "export name must start with spn_");
  }
  sp_da_for(abi->exports, it) {
    if (sp_str_equal(abi->exports[it].name, export.name)) {
      return abi_fail(abi, export.name, "duplicate export name");
    }
  }
  if (ret && !parse_val(abi, export.name, ret, &export.ret)) {
    return false;
  }
  if (export.ret.kind == ABI_VAL_STR_OPT) {
    return abi_fail(abi, export.name, "export cannot return str_opt");
  }
  if (args) {
    yyjson_val* arg;
    yyjson_arr_foreach(args, idx, max, arg) {
      abi_val_t parsed = sp_zero;
      if (!parse_val(abi, export.name, arg, &parsed)) {
        return false;
      }
      sp_da_push(export.args, parsed);
    }
  }

  sp_da_push(abi->exports, export);
  return true;
}

abi_t* abi_parse(sp_mem_t mem, sp_str_t path) {
  abi_t* abi = sp_alloc_type(mem, abi_t);
  abi->mem = mem;
  sp_str_ht_init(mem, abi->handles);
  abi->exports = sp_da_new(mem, abi_export_t);

  sp_str_t json = sp_zero;
  if (sp_io_read_file(mem, path, &json)) {
    abi_fail(abi, path, "failed to read");
    return abi;
  }

  yyjson_alc alc = gen_yyjson_alc(&abi->mem);
  yyjson_doc* doc = yyjson_read_opts((c8*)json.data, json.len, 0, &alc, SP_NULLPTR);
  if (!doc) {
    abi_fail(abi, path, "invalid json");
    return abi;
  }

  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* handles = yyjson_obj_get(root, "handles");
  yyjson_val* exports = yyjson_obj_get(root, "exports");
  if (!yyjson_is_arr(handles) || !yyjson_is_arr(exports) || yyjson_obj_size(root) != 2) {
    abi_fail(abi, path, "abi needs exactly handles and exports arrays");
    return abi;
  }

  size_t idx, max;
  yyjson_val* val;
  yyjson_arr_foreach(handles, idx, max, val) {
    if (!yyjson_is_str(val)) {
      abi_fail(abi, sp_str_lit("handles"), "handle must be a string");
      break;
    }
    sp_str_t handle = abi_str(abi, val);
    if (is_handle(abi, handle)) {
      abi_fail(abi, handle, "duplicate handle");
      break;
    }
    sp_ht_insert(abi->handles, handle, true);
  }
  if (sp_str_empty(abi->err)) {
    yyjson_arr_foreach(exports, idx, max, val) {
      if (!parse_export(abi, val)) {
        break;
      }
    }
  }

  return abi;
}

static sp_str_t abi_val_kind(abi_t* abi, abi_val_t val) {
  return sp_fmt(abi->mem, "SPN_ABI_KIND_{}", sp_fmt_str(sp_str_to_upper(abi->mem, val.name))).value;
}

static sp_str_t export_sig(abi_t* abi, abi_export_t* export) {
  sp_io_dyn_mem_writer_t w = sp_zero;
  sp_io_dyn_mem_writer_init(abi->mem, &w);

  sp_fmt_io(&w.base, "(");
  sp_da_for(export->args, it) {
    sp_fmt_io(&w.base, "{}", sp_fmt_cstr(export->args[it].kind == ABI_VAL_STR ? "$" : "i"));
  }
  sp_fmt_io(&w.base, ")");
  if (export->ret.kind != ABI_VAL_VOID) {
    sp_fmt_io(&w.base, "i");
  }
  return sp_io_dyn_mem_writer_take_str(&w);
}

static const c8* arg_prefix(abi_val_t val) {
  switch (val.kind) {
    case ABI_VAL_HANDLE: {
      return "h";
    }
    case ABI_VAL_STR_OPT: {
      return "s";
    }
    case ABI_VAL_VOID:
    case ABI_VAL_S32:
    case ABI_VAL_STR: {
      return "a";
    }
  }
  SP_UNREACHABLE_RETURN("a");
}

static sp_str_t export_call(abi_t* abi, abi_export_t* export) {
  sp_io_dyn_mem_writer_t w = sp_zero;
  sp_io_dyn_mem_writer_init(abi->mem, &w);
  if (export->wasm_ctx) {
    sp_fmt_io(&w.base, "&abi");
  }
  sp_da_for(export->args, it) {
    sp_fmt_io(&w.base, "{}{}{}",
      sp_fmt_cstr((it || export->wasm_ctx) ? ", " : ""),
      sp_fmt_cstr(arg_prefix(export->args[it])),
      sp_fmt_uint(it));
  }
  return sp_io_dyn_mem_writer_take_str(&w);
}

static const c8* arg_ctype(abi_val_t val) {
  switch (val.kind) {
    case ABI_VAL_STR: {
      return "const c8*";
    }
    case ABI_VAL_S32: {
      return "s32";
    }
    case ABI_VAL_VOID:
    case ABI_VAL_STR_OPT:
    case ABI_VAL_HANDLE: {
      return "u32";
    }
  }
  SP_UNREACHABLE_RETURN("u32");
}

static void bind_export(abi_t* abi, sp_template_scope_t* scope, abi_export_t* export) {
  const abi_ret_row_t* ret = &ABI_RETS[export->ret.kind];

  sp_template_set(scope, sp_str_lit("name"), export->name);
  sp_template_set(scope, sp_str_lit("short"), sp_str_strip_left(export->name, sp_str_lit("spn_")));
  sp_template_set(scope, sp_str_lit("sig"), export_sig(abi, export));
  sp_template_set(scope, sp_str_lit("host"), sp_str_empty(export->host) ? export->name : export->host);
  sp_template_set(scope, sp_str_lit("call"), export_call(abi, export));
  sp_template_set(scope, sp_str_lit("ret_type"), sp_cstr_as_str(ret->type));
  sp_template_set(scope, sp_str_lit("fail"), sp_cstr_as_str(ret->fail));

  switch (export->ret.kind) {
    case ABI_VAL_VOID: {
      sp_template_set(scope, sp_str_lit("r_void"), sp_str_lit("1"));
      break;
    }
    case ABI_VAL_S32: {
      sp_template_set(scope, sp_str_lit("r_s32"), sp_str_lit("1"));
      break;
    }
    case ABI_VAL_STR: {
      sp_template_set(scope, sp_str_lit("r_str"), sp_str_lit("1"));
      break;
    }
    case ABI_VAL_STR_OPT:
    case ABI_VAL_HANDLE: {
      sp_template_set(scope, sp_str_lit("r_handle"), sp_str_lit("1"));
      sp_template_set(scope, sp_str_lit("ret_kind"), abi_val_kind(abi, export->ret));
      break;
    }
  }

  sp_template_list(scope, sp_str_lit("args"));
  sp_da_for(export->args, it) {
    abi_val_t* arg = &export->args[it];
    sp_template_scope_t* child = sp_template_push(scope, sp_str_lit("args"));
    sp_template_set(child, sp_str_lit("i"), sp_fmt(abi->mem, "{}", sp_fmt_uint(it)).value);
    sp_template_set(child, sp_str_lit("ctype"), sp_cstr_as_str(arg_ctype(*arg)));
    if (arg->kind == ABI_VAL_STR_OPT) {
      sp_template_set(child, sp_str_lit("stropt"), sp_str_lit("1"));
    }
    if (arg->kind == ABI_VAL_HANDLE) {
      sp_template_set(child, sp_str_lit("handle"), sp_str_lit("1"));
      sp_template_set(child, sp_str_lit("kind"), abi_val_kind(abi, *arg));
    }
  }
}

gen_render_t abi_render_impl(abi_t* abi) {
  sp_template_scope_t* root = sp_template_scope_create(abi->mem);
  sp_template_list(root, sp_str_lit("fns"));
  sp_da_for(abi->exports, it) {
    bind_export(abi, sp_template_push(root, sp_str_lit("fns")), &abi->exports[it]);
  }
  return (gen_render_t) { .template = sp_str_lit("abi/impl.c"), .scope = root };
}
