#include "host/host.h"

#include "sp/sp_glob.h"

#include "str/str.h"
#include "tui/tui.h"

#include "spn.embed.h"

static struct {
  sp_str_t page;
} args;

typedef struct {
  sp_str_t name;
  sp_str_t content;
} doc_page_t;

static sp_str_t strip_frontmatter(sp_str_t content) {
  sp_str_line_it_t it = sp_str_line_it_begin(content);
  if (!sp_str_equal_cstr(sp_str_trim_right(it.line), "---")) {
    return content;
  }
  for (sp_str_line_it_next(&it); sp_str_line_it_valid(&it); sp_str_line_it_next(&it)) {
    if (sp_str_equal_cstr(sp_str_trim_right(it.line), "---")) {
      return sp_str_trim_left(sp_str_suffix(content, content.len - it.cursor));
    }
  }
  return content;
}

static s32 compare_pages(const void* a, const void* b) {
  const doc_page_t* pa = sp_ptr_cast(const doc_page_t*, a);
  const doc_page_t* pb = sp_ptr_cast(const doc_page_t*, b);
  return sp_str_compare_alphabetical(pa->name, pb->name);
}

static doc_page_t* find_page(sp_da(doc_page_t) pages, sp_str_t name) {
  sp_da_for(pages, it) {
    if (sp_str_equal(pages[it].name, name)) {
      return &pages[it];
    }
  }
  return SP_NULLPTR;
}

static sp_cli_result_t docs(sp_cli_t* cli) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_cli_result_t result = SP_CLI_OK;

  sp_glob_t* glob = sp_glob_new_str(s.mem, sp_str_lit("docs/*.{md,mdx}"));
  sp_da(doc_page_t) pages = sp_da_new(s.mem, doc_page_t);
  sp_carr_for(spn_embed_manifest, it) {
    const spn_embed_entry_t* entry = &spn_embed_manifest[it];
    sp_str_t path = sp_str_view(entry->path);
    if (sp_glob_match(glob, path)) {
      sp_da_push(pages, ((doc_page_t) {
        .name = sp_fs_get_stem(path),
        .content = strip_frontmatter(sp_str((const c8*)entry->data, (u32)entry->size)),
      }));
    }
  }
  sp_da_sort(pages, compare_pages);

  if (sp_str_empty(args.page)) {
    sp_da_for(pages, it) {
      spn_print(&tui, "{}", sp_fmt_str(pages[it].name));
    }
  }
  else {
    doc_page_t* page = find_page(pages, args.page);
    if (page) {
      sp_tty_fmt(tui.out, "{}", sp_fmt_str(page->content));
    }
    else {
      result = spn_cli_usage("unknown page {.cyan}; run {.cyan} to list pages", sp_fmt_str(args.page), sp_fmt_cstr("spn docs"));
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
