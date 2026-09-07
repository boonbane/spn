#define SP_IMPLEMENTATION
#include "sp.h"
#include "sp/sp_cli.h"
#include "jtd.h"

typedef struct {
  const c8* schema;
  const c8* out;
  const c8* title;
} args_t;

typedef struct {
  sp_str_t name;
  jtd_schema_t* schema;
} doc_type_t;

typedef struct {
  sp_mem_t mem;
  jtd_result_t* jtd;
  sp_io_dyn_mem_writer_t out;
  sp_da(doc_type_t) types;
  sp_da(sp_str_t) externs;
} docs_t;

static void put(sp_io_writer_t* w, sp_str_t str) {
  sp_io_write_str(w, str, SP_NULLPTR);
}

static void docs_queue(docs_t* d, sp_str_t name, jtd_schema_t* schema) {
  sp_da_for(d->types, it) {
    if (sp_str_equal(d->types[it].name, name)) {
      return;
    }
  }
  doc_type_t type = { .name = name, .schema = schema };
  sp_da_push(d->types, type);
}

static void docs_note_extern(docs_t* d, sp_str_t name) {
  sp_da_for(d->externs, it) {
    if (sp_str_equal(d->externs[it], name)) {
      return;
    }
  }
  sp_da_push(d->externs, name);
}

static bool docs_type_single(docs_t* d, jtd_schema_t* schema) {
  switch (schema->form) {
    case JTD_FORM_ENUM: {
      return sp_da_size(schema->as.enumeration.values) == 1;
    }
    case JTD_FORM_REF: {
      jtd_schema_t* target = jtd_resolve(d->jtd, schema);
      return target->form == JTD_FORM_PROPERTIES || docs_type_single(d, target);
    }
    case JTD_FORM_ELEMENTS:
    case JTD_FORM_VALUES: {
      return false;
    }
    default: {
      return true;
    }
  }
}

// suffix lands inside the code span, so an array of `foo` reads `foo[]`
static void docs_type_cell(docs_t* d, jtd_schema_t* schema, const c8* suffix) {
  sp_io_writer_t* w = &d->out.base;
  switch (schema->form) {
    case JTD_FORM_TYPE: {
      const c8* name = "integer";
      switch (schema->as.type) {
        case JTD_TYPE_BOOLEAN:   name = "bool"; break;
        case JTD_TYPE_STRING:    name = "string"; break;
        case JTD_TYPE_FLOAT32:
        case JTD_TYPE_FLOAT64:   name = "float"; break;
        case JTD_TYPE_TIMESTAMP: name = "timestamp"; break;
        default:                 break;
      }
      sp_fmt_io(w, "`{}{}`", sp_fmt_cstr(name), sp_fmt_cstr(suffix));
      break;
    }
    case JTD_FORM_ENUM: {
      sp_da_for(schema->as.enumeration.values, it) {
        if (it) {
          put(w, sp_str_lit(" \\| "));
        }
        sp_fmt_io(w, "`{}{}`", sp_fmt_str(schema->as.enumeration.values[it]), sp_fmt_cstr(suffix));
      }
      break;
    }
    case JTD_FORM_REF: {
      jtd_schema_t* target = jtd_resolve(d->jtd, schema);
      if (target->form == JTD_FORM_PROPERTIES) {
        sp_str_t name = schema->as.ref.name;
        docs_queue(d, name, target);
        sp_fmt_io(w, "[`{}{}`](#{})", sp_fmt_str(name), sp_fmt_cstr(suffix), sp_fmt_str(name));
      } else {
        docs_type_cell(d, target, suffix);
      }
      break;
    }
    case JTD_FORM_ELEMENTS: {
      jtd_schema_t* element = schema->as.elements.schema;
      bool shorthand = jtd_metadata_has(schema, "shorthand");
      if (!shorthand && docs_type_single(d, element)) {
        docs_type_cell(d, element, "[]");
        break;
      }
      put(w, sp_str_lit("("));
      docs_type_cell(d, element, "");
      if (shorthand) {
        put(w, sp_str_lit(" \\| `string`"));
      }
      put(w, sp_str_lit(")`[]`"));
      break;
    }
    case JTD_FORM_VALUES: {
      put(w, sp_str_lit("map of "));
      docs_type_cell(d, schema->as.values.schema, "");
      if (jtd_metadata_has(schema, "shorthand")) {
        put(w, sp_str_lit(" \\| `string`"));
      }
      break;
    }
    case JTD_FORM_EMPTY: {
      sp_str_t external = jtd_metadata(schema, "extern");
      if (!sp_str_empty(external)) {
        docs_note_extern(d, external);
        sp_fmt_io(w, "[`{}{}`](#extern-types)", sp_fmt_str(external), sp_fmt_cstr(suffix));
      } else {
        sp_fmt_io(w, "`any{}`", sp_fmt_cstr(suffix));
      }
      break;
    }
    default: {
      sp_fmt_io(w, "`any{}`", sp_fmt_cstr(suffix));
      break;
    }
  }
}

// `doc` and `default` are free text; a literal pipe would end the cell
static void docs_cell(docs_t* d, sp_str_t text) {
  sp_io_writer_t* w = &d->out.base;
  if (sp_str_empty(text)) {
    put(w, sp_str_lit(" |"));
    return;
  }
  put(w, sp_str_lit(" "));
  sp_da(sp_str_t) parts = sp_str_split_c8(d->mem, text, '|');
  sp_da_for(parts, it) {
    if (it) {
      put(w, sp_str_lit("\\|"));
    }
    put(w, parts[it]);
  }
  put(w, sp_str_lit(" |"));
}

static bool docs_any(jtd_schema_t* schema, const c8* key) {
  sp_da_for(schema->as.properties.all, it) {
    if (jtd_metadata_has(schema->as.properties.all[it].schema, key)) {
      return true;
    }
  }
  return false;
}

static void docs_table(docs_t* d, jtd_schema_t* schema, bool root) {
  sp_io_writer_t* w = &d->out.base;
  bool required = sp_da_size(schema->as.properties.required) > 0;
  bool defaulted = docs_any(schema, "default");
  bool described = docs_any(schema, "doc");
  u32 columns = 2 + required + defaulted + described;
  put(w, root ? sp_str_lit("| Table | Contents |") : sp_str_lit("| Field | Type |"));
  if (required) {
    put(w, sp_str_lit(" Required |"));
  }
  if (defaulted) {
    put(w, sp_str_lit(" Default |"));
  }
  if (described) {
    put(w, sp_str_lit(" Description |"));
  }
  put(w, sp_str_lit("\n|"));
  sp_for(it, columns) {
    put(w, sp_str_lit("---|"));
  }
  put(w, sp_str_lit("\n"));

  sp_da_for(schema->as.properties.all, it) {
    jtd_property_t* prop = &schema->as.properties.all[it];
    if (!root) {
      sp_fmt_io(w, "| `{}` | ", sp_fmt_str(prop->key));
    } else {
      switch (prop->schema->form) {
        case JTD_FORM_ELEMENTS: sp_fmt_io(w, "| `[[{}]]` | ", sp_fmt_str(prop->key)); break;
        case JTD_FORM_VALUES:   sp_fmt_io(w, "| `[{}.<name>]` | ", sp_fmt_str(prop->key)); break;
        default:                sp_fmt_io(w, "| `[{}]` | ", sp_fmt_str(prop->key)); break;
      }
    }
    docs_type_cell(d, prop->schema, "");
    put(w, sp_str_lit(" |"));
    if (required) {
      docs_cell(d, prop->required ? sp_str_lit("yes") : sp_str_lit(""));
    }
    if (defaulted) {
      sp_str_t value = jtd_metadata(prop->schema, "default");
      docs_cell(d, sp_str_empty(value) ? value : sp_fmt(d->mem, "`{}`", sp_fmt_str(value)).value);
    }
    if (described) {
      docs_cell(d, jtd_metadata(prop->schema, "doc"));
    }
    put(w, sp_str_lit("\n"));
  }
}

static void docs_root(docs_t* d) {
  put(&d->out.base, sp_str_lit("## Top level\n\n"));
  docs_table(d, d->jtd->root, true);
}

static void docs_section(docs_t* d, sp_str_t name, jtd_schema_t* schema) {
  sp_io_writer_t* w = &d->out.base;
  sp_fmt_io(w, "\n## {}\n\n", sp_fmt_str(name));
  sp_str_t doc = jtd_metadata(schema, "doc");
  if (!sp_str_empty(doc)) {
    sp_fmt_io(w, "{}\n\n", sp_fmt_str(doc));
  }
  docs_table(d, schema, false);
}

static sp_cli_result_t run_cli(sp_cli_t* cli) {
  args_t* args = sp_cast(args_t*, cli->user_data);
  sp_mem_t mem = sp_mem_heap_as_allocator(sp_mem_heap_new());

  jtd_result_t jtd = sp_zero;
  jtd_err_t err = jtd_parse_file(mem, sp_cstr_as_str(args->schema), &jtd);
  if (err != JTD_OK || !jtd.ok) {
    return sp_cli_set_error(cli, sp_fmt(mem, "{}: {}: {}",
      sp_fmt_cstr(args->schema),
      sp_fmt_cstr(jtd_err_name(err ? err : jtd.diag.code)),
      sp_fmt_str(jtd_diagnostic_message(mem, &jtd.diag))).value);
  }
  if (!jtd.root || jtd.root->form != JTD_FORM_PROPERTIES) {
    return sp_cli_set_error(cli, sp_fmt(mem, "{}: root schema is not a properties form", sp_fmt_cstr(args->schema)).value);
  }

  docs_t docs = {
    .mem = mem,
    .jtd = &jtd,
  };
  sp_io_dyn_mem_writer_init(mem, &docs.out);
  docs.types = sp_da_new(mem, doc_type_t);
  docs.externs = sp_da_new(mem, sp_str_t);

  sp_io_writer_t* w = &docs.out.base;
  sp_fmt_io(w,
    "---\n"
    "title: {}\n"
    "group: Reference\n"
    "readme: false\n"
    "---\n"
    "\n"
    "<!-- Generated by `spn build --tool docs`. Do not edit by hand. -->\n"
    "\n",
    sp_fmt_cstr(args->title));

  docs_root(&docs);
  for (u64 it = 0; it < sp_da_size(docs.types); it++) {
    doc_type_t type = docs.types[it];
    docs_section(&docs, type.name, type.schema);
  }

  if (sp_da_size(docs.externs)) {
    put(w, sp_str_lit("\n## Extern types\n\nThese values are validated by spn itself, outside the manifest schema:\n\n"));
    sp_da_for(docs.externs, it) {
      sp_fmt_io(w, "- `{}`\n", sp_fmt_str(docs.externs[it]));
    }
  }

  sp_str_t content = sp_io_dyn_mem_writer_as_str(&docs.out);
  sp_io_file_writer_t file = sp_zero;
  if (sp_io_file_writer_from_path(&file, sp_cstr_as_str(args->out))) {
    return sp_cli_set_error(cli, sp_fmt(mem, "failed to open {}", sp_fmt_cstr(args->out)).value);
  }
  sp_err_t written = sp_io_write_all(&file.base, content.data, content.len, SP_NULLPTR);
  sp_err_t closed = sp_io_file_writer_close(&file);
  if (written || closed) {
    return sp_cli_set_error(cli, sp_fmt(mem, "failed to write {}", sp_fmt_cstr(args->out)).value);
  }
  return SP_CLI_OK;
}

s32 main(s32 num_args, const c8** args) {
  args_t parsed = sp_zero;

  sp_cli_cmd_t root = {
    .name = "docs",
    .summary = "Generate a markdown reference page from a merged JTD schema",
    .args = {
      {
        .name = "schema",
        .summary = "Path to a merged schema (e.g. source/core/codegen/gen/manifest.jtd.json)",
        .ptr = &parsed.schema,
      },
      {
        .name = "out",
        .summary = "Path to write the markdown file to",
        .ptr = &parsed.out,
      },
      {
        .name = "title",
        .summary = "Title of the page",
        .ptr = &parsed.title,
      },
    },
    .handler = run_cli,
  };

  return sp_cli_main((sp_cli_desc_t) {
    .root = &root,
    .num_args = num_args,
    .args = args,
    .user_data = &parsed,
  });
}
