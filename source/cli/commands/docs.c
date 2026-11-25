#include "host/host.h"

#include "sp/sp_glob.h"

#include "str/str.h"
#include "tui/tui.h"

#include "spn.embed.h"
#include "yyjson.h"

static struct {
  sp_str_t page;
} args;

typedef struct {
  sp_str_t name;
  sp_str_t title;
  sp_str_t body;
} doc_page_t;

static yyjson_val* read_json(sp_mem_t mem, const c8* path) {
  sp_carr_for(spn_embed_manifest, it) {
    const spn_embed_entry_t* entry = &spn_embed_manifest[it];
    if (sp_cstr_equal(entry->path, path)) {
      u64 size = yyjson_read_max_memory_usage(entry->size, 0);
      yyjson_alc alc = sp_zero;
      yyjson_alc_pool_init(&alc, sp_alloc(mem, size), size);
      yyjson_doc* doc = yyjson_read_opts((c8*)entry->data, entry->size, 0, &alc, SP_NULLPTR);
      sp_assert(doc);
      return yyjson_doc_get_root(doc);
    }
  }
  sp_unreachable_return(SP_NULLPTR);
}

static sp_cli_result_t docs(sp_cli_t* cli) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_cli_result_t result = SP_CLI_OK;

  sp_glob_t* glob = sp_glob_new_str(s.mem, sp_str_lit("docs/*.{md,mdx}"));
  sp_da(sp_str_t) names = sp_da_new(s.mem, sp_str_t);
  doc_page_t page = sp_zero;
  sp_carr_for(spn_embed_manifest, it) {
    const spn_embed_entry_t* entry = &spn_embed_manifest[it];
    sp_str_t path = sp_cstr_as_str(entry->path);
    if (!sp_glob_match(glob, path)) {
      continue;
    }

    sp_str_t content = sp_str((const c8*)entry->data, (u32)entry->size);
    doc_page_t found = {
      .name = sp_fs_get_stem(path),
      .body = content,
    };
    bool site = true;
    sp_str_line_it_t line = sp_str_line_it_begin(content);
    bool frontmatter = sp_str_equal_cstr(sp_str_trim_right(line.line), "---");
    for (sp_str_line_it_next(&line); frontmatter && sp_str_line_it_valid(&line); sp_str_line_it_next(&line)) {
      sp_str_pair_t field = sp_str_cleave_c8(line.line, ':');
      sp_str_t key = sp_str_trim(field.first);
      sp_str_t value = sp_str_trim(field.second);
      if (sp_str_equal_cstr(key, "---")) {
        found.body = sp_str_suffix(content, content.len - line.cursor);
        frontmatter = false;
      }
      if (sp_str_equal_cstr(key, "title")) {
        found.title = value;
      }
      if (sp_str_equal_cstr(key, "site")) {
        site = !sp_str_equal_cstr(value, "false");
      }
    }

    if (site) {
      sp_da_push(names, found.name);
    }
    if (sp_str_equal(found.name, args.page)) {
      page = found;
    }
  }

  if (sp_str_empty(args.page)) {
    sp_da_sort(names, sp_str_sort_kernel_alphabetical);
    sp_da_for(names, it) {
      spn_print(&tui, "{}", sp_fmt_str(names[it]));
    }
  }
  else if (sp_str_empty(page.name)) {
    result = spn_cli_usage("unknown page {.cyan}; run {.cyan} to list pages", sp_fmt_str(args.page), sp_fmt_cstr("spn docs"));
  }
  else {
    yyjson_val* links = read_json(s.mem, "docs/links.json");
    yyjson_val* install = read_json(s.mem, "docs/install.json");
    sp_str_t fence = sp_str_lit("```");
    sp_str_t scheme = sp_str_lit("(link:");
    bool fenced = false;
    bool gap = true;
    sp_tty_fmt(tui.out, "# {}\n", sp_fmt_str(page.title));
    sp_str_for_line(page.body, it) {
      sp_str_t line = sp_str_trim(it.line);
      if (fenced) {
        sp_tty_fmt(tui.out, "{}\n", sp_fmt_str(it.line));
        fenced = !sp_str_starts_with(line, fence);
        continue;
      }
      if (sp_str_empty(line)) {
        gap = true;
        continue;
      }
      if (sp_str_starts_with(it.line, sp_str_lit("import ")) || sp_str_equal_cstr(line, "<Offline />")) {
        continue;
      }
      if (gap) {
        sp_tty_fmt(tui.out, "\n");
        gap = false;
      }
      if (sp_str_equal_cstr(line, "<Stub />")) {
        sp_tty_fmt(tui.out, "> This section is a stub.\n");
        continue;
      }
      if (sp_str_equal_cstr(line, "<Install />")) {
        u64 index = 0;
        u64 count = 0;
        yyjson_val* target = SP_NULLPTR;
        yyjson_arr_foreach(install, index, count, target) {
          sp_tty_fmt(tui.out, "{}### {}\n\n```{}\n{}\n```\n",
            sp_fmt_cstr(index ? "\n" : ""),
            sp_fmt_cstr(yyjson_get_str(yyjson_obj_get(target, "label"))),
            sp_fmt_cstr(yyjson_get_str(yyjson_obj_get(target, "lang"))),
            sp_fmt_cstr(yyjson_get_str(yyjson_obj_get(target, "command")))
          );
        }
        continue;
      }

      fenced = sp_str_starts_with(line, fence);
      sp_str_t rest = it.line;
      for (s32 at = sp_str_find(rest, scheme); at != SP_STR_NO_MATCH; at = sp_str_find(rest, scheme)) {
        sp_str_pair_t link = sp_str_cleave_c8(sp_str_skip(rest, at + 1), ')');
        yyjson_val* node = links;
        sp_str_for_split(sp_str_strip_left(link.first, sp_str_lit("link:")), '.', key) {
          node = yyjson_obj_getn(node, key.value.data, key.value.len);
        }
        sp_str_t url = sp_cstr_as_str(yyjson_get_str(node));
        sp_tty_fmt(tui.out, "{}({})", sp_fmt_str(sp_str_prefix(rest, at)), sp_fmt_str(sp_str_empty(url) ? link.first : url));
        rest = link.second;
      }
      sp_tty_fmt(tui.out, "{}\n", sp_fmt_str(rest));
    }
  }

  sp_glob_free(glob);
  sp_mem_end_scratch(s);
  return result;
}

sp_cli_cmd_t spn_cmd_docs = {
  .name = "docs",
  .summary = "Print documentation",
  .args = {
    {
      .name = "page",
      .arity = SP_CLI_ARG_OPTIONAL,
      .kind = SP_CLI_OPT_STR,
      .summary = "Page to print; lists pages if omitted",
      .ptr = &args.page,
    },
  },
  .handler = docs,
};
