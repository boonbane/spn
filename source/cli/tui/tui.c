#include "spn/err.h"
#include "spn/host.h"

#include "io/io.h"
#include "macro/macro.h"
#include "sp/sp_prompt.h"
#include "str/str.h"

#include "tui/tui.h"


sp_prompt_widget_t sp_prompt_progress_widget(sp_prompt_ctx_t* ctx, sp_prompt_progress_t config);

#ifdef SP_WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif

  #include <windows.h>
#endif

#if defined(SP_POSIX)
  #include <fcntl.h>
  #include <unistd.h>
#endif


static void write_name(sp_tty_t* tty, sp_str_t name);
static void flush_writer(spn_tui_line_writer_t* writer);

static sp_color_t name_to_color(sp_str_t name) {
  static const f32 hues [] = {
    30, 40, 50, 60,
    160, 180,
    200, 220, 240,
    250, 280, 310, 340
  };

  sp_color_t hsv = {
    .h = hues[sp_hash_str(name) % sp_carr_len(hues)],
    .s = 40.0f,
    .v = 75.f,
  };
  return sp_color_hsv_to_rgb(hsv);
}

static void write_name_color(sp_tty_t* tty, sp_str_t name) {
  sp_color_t rgb = name_to_color(name);
  sp_tty_rgb(tty, (u8)(rgb.r * 255.0f), (u8)(rgb.g * 255.0f), (u8)(rgb.b * 255.0f));
}

static void write_name(sp_tty_t* tty, sp_str_t name) {
  write_name_color(tty, name);
  sp_io_write_str(tty->io, sp_str_lit("\u2590 "), SP_NULLPTR);
  sp_io_write_str(tty->io, name, SP_NULLPTR);
  sp_tty_reset(tty);
}

static sp_str_t colored_name(sp_tty_color_t color, sp_mem_t mem, sp_str_t name) {
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_tty_t tty = { .io = &buf.base, .color = color };
  write_name_color(&tty, name);
  sp_io_write_str(&buf.base, name, SP_NULLPTR);
  sp_tty_reset(&tty);
  return sp_io_dyn_mem_writer_take_str(&buf);
}

static sp_str_t colored_fmt(sp_tty_color_t color, sp_mem_t mem, const c8* fmt, ...) {
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_tty_t tty = { .io = &buf.base, .color = color };

  va_list args;
  va_start(args, fmt);
  sp_tty_fmt_v(&tty, sp_cstr_as_str(fmt), args);
  va_end(args);

  return sp_io_dyn_mem_writer_take_str(&buf);
}

static sp_str_t colored_sanitizers(sp_tty_color_t color, sp_mem_t mem, sp_fmt_style_t style, spn_sanitizer_set_t set) {
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_tty_t tty = { .io = &buf.base, .color = color };
  u32 written = 0;
  for (spn_sanitizer_set_t bit = SPN_SANITIZER_ADDRESS; bit <= SPN_SANITIZER_LEAK; bit <<= 1) {
    if (!(set & bit)) {
      continue;
    }
    sp_tty_fmt(&tty, written ? ", {.$}" : "{.$}", sp_fmt_style(style), sp_fmt_str(spn_sanitizer_to_str((spn_sanitizer_t)bit)));
    written++;
  }
  return sp_io_dyn_mem_writer_take_str(&buf);
}

static void write_issue(sp_tty_t* w, const spn_toml_issue_t* issue) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t field = spn_toml_path_str(scratch.mem, &issue->at);
  switch (issue->code) {
    case SPN_ERR_CODEGEN_MISSING_KEY:
      if (sp_da_empty(issue->choices)) {
        sp_tty_fmt(w, "Missing required field {.cyan}", SP_FMT_STR(field));
      } else {
        sp_str_t choices = sp_str_join_n(scratch.mem, issue->choices, (u32)sp_da_size(issue->choices), sp_str_lit(", "));
        sp_tty_fmt(w, "Missing field {.cyan}, or {.cyan} in its place", SP_FMT_STR(field), SP_FMT_STR(choices));
      }
      break;
    case SPN_ERR_CODEGEN_EXPECTED_STR:
      sp_tty_fmt(w, "Field {.cyan} must be a string", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_EXPECTED_INT:
      sp_tty_fmt(w, "Field {.cyan} must be a non-negative integer", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_EXPECTED_POSITIVE:
      sp_tty_fmt(w, "Field {.cyan} must be a positive integer", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_EXPECTED_BOOL:
      sp_tty_fmt(w, "Field {.cyan} must be a boolean", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_EXPECTED_OBJECT:
      sp_tty_fmt(w, "Field {.cyan} must be a table", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_DUPLICATE_KEY:
      sp_tty_fmt(w, "Duplicate {.red} at {.cyan}", SP_FMT_STR(issue->detail), SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_UNKNOWN_KEY:
      if (sp_str_empty(field)) {
        sp_tty_fmt(w, "Unknown field {.red}", SP_FMT_STR(issue->detail));
      } else {
        sp_tty_fmt(w, "Unknown field {.red} in {.cyan}", SP_FMT_STR(issue->detail), SP_FMT_STR(field));
      }
      break;
    case SPN_ERR_CODEGEN_INVALID:
      sp_tty_fmt(w, "Invalid value at {.cyan}", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_LINKER:
      if (sp_da_empty(issue->choices)) {
        sp_tty_fmt(w, "Driver {.cyan} doesn't support linker {.red}. Leave it unset for the native linker", SP_FMT_STR(issue->detail), SP_FMT_STR(issue->value));
      } else {
        sp_str_t choices = sp_str_join_n(scratch.mem, issue->choices, (u32)sp_da_size(issue->choices), sp_str_lit(", "));
        sp_tty_fmt(w, "Driver {.cyan} doesn't support linker {.red}. Use {.cyan}, or leave it unset for the native linker", SP_FMT_STR(issue->detail), SP_FMT_STR(issue->value), SP_FMT_STR(choices));
      }
      break;
    case SPN_ERR_CODEGEN_PARSE:
      if (sp_str_empty(issue->detail)) {
        sp_io_write_str(w->io, sp_str_lit("Not valid toml"), SP_NULLPTR);
      } else {
        sp_tty_fmt(w, "Not valid toml: {}", SP_FMT_STR(issue->detail));
      }
      break;
    case SPN_ERR_FS_READ:
      sp_tty_fmt(w, "Failed to read file: {}", SP_FMT_STR(issue->detail));
      break;
    case SPN_ERR_CODEGEN_ROOT_ONLY:
      sp_tty_fmt(w, "Field {.cyan} is only allowed in the root manifest", SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_PATH:
      sp_tty_fmt(w, "Path {.red} at {.cyan} has a '.', '..', or empty component", SP_FMT_STR(issue->detail), SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_UNROOTED:
      sp_tty_fmt(w, "Relative path {.red} at {.cyan} has no base directory; use an absolute path", SP_FMT_STR(issue->detail), SP_FMT_STR(field));
      break;
    case SPN_ERR_CODEGEN_ABSOLUTE:
      sp_tty_fmt(w, "Path {.red} at {.cyan} must be relative", SP_FMT_STR(issue->detail), SP_FMT_STR(field));
      break;
    default:
      sp_unreachable_case();
  }
  sp_mem_end_scratch(scratch);
}

static sp_str_t lock_root_to_str(sp_mem_t mem, const spn_err_lock_root_t* root) {
  if (sp_str_empty(root->dir)) {
    return root->url;
  }
  return sp_fmt(mem, "{}/{}", sp_fmt_str(root->url), sp_fmt_str(root->dir)).value;
}

static sp_str_t get_issue_scope(sp_da(spn_toml_issue_t) issues) {
  sp_str_t scope = sp_da_empty(issues) ? sp_str_lit("") : issues[0].scope;
  sp_da_for(issues, it) {
    if (!sp_str_equal(issues[it].scope, scope)) {
      return sp_str_lit("");
    }
  }
  return scope;
}

static sp_str_t requester_to_str(spn_linkage_requester_t requester) {
  switch (requester) {
    case SPN_LINKAGE_REQUESTER_PROFILE: return sp_str_lit("static_deps = true");
    case SPN_LINKAGE_REQUESTER_ROOT_MANIFEST: return sp_str_lit("the root manifest");
    case SPN_LINKAGE_REQUESTER_LIBC: return sp_str_lit("libc = \"static\"");
    case SPN_LINKAGE_REQUESTER_TARGET: return sp_str_lit("the target");
    case SPN_LINKAGE_REQUESTER_NO_LOADER: return sp_str_lit("a target with no dynamic loader");
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

static sp_str_t setter_to_str(spn_err_setter_t setter) {
  switch (setter.kind) {
    case SPN_OPTION_SETTER_NONE: {
      return sp_str_lit("");
    }
    case SPN_OPTION_SETTER_DEFAULT: {
      return sp_str_lit("the default");
    }
    case SPN_OPTION_SETTER_PROFILE: {
      return sp_str_lit("the profile");
    }
    case SPN_OPTION_SETTER_ROOT_MANIFEST: {
      return sp_str_lit("the root manifest");
    }
    case SPN_OPTION_SETTER_UNION: {
      return sp_str_lit("the union of requests");
    }
    case SPN_OPTION_SETTER_CONSUMER: {
      return setter.name;
    }
  }
  SP_UNREACHABLE_RETURN(sp_str_lit(""));
}

static sp_str_t root_label(spn_path_root_t root) {
  switch (root) {
    case SPN_PATH_ROOT_PROJECT:   return sp_str_lit(".");
    case SPN_PATH_ROOT_STORAGE:   return sp_str_lit("$SPN_STORAGE");
    case SPN_PATH_ROOT_TOOLCHAIN: return sp_str_lit("$SPN_TOOLCHAIN");
    case SPN_PATH_ROOT_NONE:
    case SPN_PATH_ROOT_COUNT:     break;
  }
  return sp_str_lit("");
}

static sp_str_t contextual_path(sp_mem_t mem, spn_path_t path) {
  if (path.root == SPN_PATH_ROOT_NONE) {
    return path.sub;
  }
  sp_str_t label = root_label(path.root);
  if (sp_str_empty(path.sub)) {
    return label;
  }
  return sp_fmt(mem, "{}/{}", sp_fmt_str(label), sp_fmt_str(path.sub)).value;
}

static sp_str_t get_contextual_path(spn_ctx_t* ctx, sp_mem_t mem, sp_str_t path) {
  return contextual_path(mem, spn_ctx_collapse_path(ctx, path));
}

#define SPN_TUI_LIST_CAP 6

static sp_str_t subject_name(sp_mem_t mem, sp_str_t pkg, sp_str_t name) {
  if (sp_str_equal(pkg, name)) {
    return name;
  }
  return sp_fmt(mem, "{}/{}", sp_fmt_str(pkg), sp_fmt_str(name)).value;
}

typedef struct {
  sp_tty_t* w;
  sp_str_t label;
  sp_fmt_style_t style;
  u32 count;
} list_t;

static void list_item(list_t* list, sp_str_t item) {
  if (list->count == 0) {
    sp_tty_fmt(list->w, "  {}:", sp_fmt_str(list->label));
  }
  if (list->count < SPN_TUI_LIST_CAP) {
    sp_tty_fmt(list->w, list->count ? ", {.$}" : " {.$}", sp_fmt_style(list->style), sp_fmt_str(item));
  }
  list->count++;
}

static void list_end(list_t* list) {
  if (list->count > SPN_TUI_LIST_CAP) {
    sp_io_write_str(list->w->io, sp_str_lit(", ..."), SP_NULLPTR);
  }
  if (list->count) {
    sp_io_write_c8(list->w->io, '\n');
  }
}

static void write_supported_targets(sp_tty_t* w, sp_da(spn_triple_t) triples) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  list_t list = { .w = w, .label = sp_str_lit("supported targets"), .style = sp_fmt_style_yellow };
  sp_da_for(triples, it) {
    list_item(&list, spn_triple_to_str(scratch.mem, triples[it]));
  }
  list_end(&list);
  sp_mem_end_scratch(scratch);
}

static void write_unpack(sp_tty_t* w, const spn_err_artifact_t* artifact) {
  switch (artifact->unpack) {
    case SPN_UNPACK_NONE: {
      break;
    }
    case SPN_UNPACK_UNREADABLE: {
      sp_tty_fmt(w, "The downloaded archive couldn't be read");
      break;
    }
    case SPN_UNPACK_UNRECOGNIZED: {
      sp_tty_fmt(w, "The downloaded file isn't an archive spn can extract");
      break;
    }
    case SPN_UNPACK_CORRUPT: {
      if (sp_str_empty(artifact->entry)) {
        sp_tty_fmt(w, "The archive is corrupt");
      }
      else {
        sp_tty_fmt(w, "The archive is corrupt at {.cyan}", sp_fmt_str(artifact->entry));
      }
      break;
    }
    case SPN_UNPACK_UNSUPPORTED: {
      sp_tty_fmt(w, "The archive contains an entry spn can't extract");
      break;
    }
    case SPN_UNPACK_UNSAFE: {
      sp_tty_fmt(w, "{.cyan} would be written outside the toolchain directory", sp_fmt_str(artifact->entry));
      break;
    }
    case SPN_UNPACK_UNWRITABLE: {
      if (sp_str_empty(artifact->entry)) {
        sp_tty_fmt(w, "The toolchain directory couldn't be written");
      }
      else {
        sp_tty_fmt(w, "{.cyan} couldn't be written", sp_fmt_str(artifact->entry));
      }
      break;
    }
    case SPN_UNPACK_EMPTY: {
      sp_tty_fmt(w, "The archive has nothing under its top-level directory");
      break;
    }
  }
}

static void write_refusal(sp_tty_t* w, sp_mem_t mem, spn_triple_t target, spn_linking_refusal_t reason) {
  sp_str_t triple = spn_triple_to_str(mem, target);
  switch (reason) {
    case SPN_LINKING_REFUSAL_NO_LOADER: {
      sp_tty_fmt(w, "Target {.yellow} can't link anything shared; {} has no dynamic loader", sp_fmt_str(triple), sp_fmt_str(spn_os_to_str(target.os)));
      break;
    }
    case SPN_LINKING_REFUSAL_OS_LIBC: {
      sp_tty_fmt(w, "Target {.yellow} can't use {.red}; its libc is only shared", sp_fmt_str(triple), sp_fmt_str(sp_str_lit("libc = \"static\"")));
      break;
    }
    case SPN_LINKING_REFUSAL_OS_RUNTIME: {
      sp_tty_fmt(w, "Target {.yellow} can't use {.red}; its runtime is only shared", sp_fmt_str(triple), sp_fmt_str(sp_str_lit("runtime = \"static\"")));
      break;
    }
    case SPN_LINKING_REFUSAL_SHARED_RUNTIME: {
      sp_tty_fmt(w, "Target {.yellow} can't use {.red} with {.red}; a static libc needs a static runtime", sp_fmt_str(triple), sp_fmt_str(sp_str_lit("libc = \"static\"")), sp_fmt_str(sp_str_lit("runtime = \"shared\"")));
      break;
    }
    case SPN_LINKING_REFUSAL_HYBRID_CRT: {
      sp_tty_fmt(w, "Target {.yellow} can't split {.red} from {.red}; the msvc C library is part of the runtime", sp_fmt_str(triple), sp_fmt_str(sp_str_lit("libc")), sp_fmt_str(sp_str_lit("runtime")));
      break;
    }
    case SPN_LINKING_REFUSAL_SHARED_DEPS: {
      sp_tty_fmt(w, "Target {.yellow} can't load shared libraries with {.red}; a static libc removes the dynamic loader", sp_fmt_str(triple), sp_fmt_str(sp_str_lit("libc = \"static\"")));
      break;
    }
    case SPN_LINKING_REFUSAL_NONE: {
      sp_unreachable_case();
    }
  }
}

static void write_toolchain_candidates(sp_tty_t* w, const spn_err_toolchain_t* err) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t label = sp_fmt(scratch.mem, "toolchains for {}", sp_fmt_str(spn_triple_to_str(scratch.mem, err->query))).value;
  list_t list = { .w = w, .label = label, .style = sp_fmt_style_green };
  sp_da_for(err->candidates, it) {
    list_item(&list, err->candidates[it]);
  }
  list_end(&list);
  sp_mem_end_scratch(scratch);
}

static sp_str_t render_event_detail(spn_tui_t* tui, sp_mem_t mem, spn_event_t* event) {
  spn_ctx_t* ctx = tui->ctx;
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_tty_t w = { .io = &buf.base, .color = tui->journal.color };

  switch (event->kind) {
    case SPN_EVENT_SYNC:
    case SPN_EVENT_SYNC_PATCH:
    case SPN_EVENT_INDEX_ADDED: {
      sp_tty_fmt(&w, "{} {.gray}", sp_fmt_str(event->sync.name), sp_fmt_str(event->sync.url));
      break;
    }
    case SPN_EVENT_SYNC_START: {
      sp_tty_fmt(&w, "packages={} index={} file={}",
        sp_fmt_uint(event->sync_start.num_packages),
        sp_fmt_uint(event->sync_start.num_index),
        sp_fmt_uint(event->sync_start.num_file)
      );
      break;
    }
    case SPN_EVENT_SYNC_PACKAGE: {
      sp_tty_fmt(&w, "{} fetched={} source={.gray}",
        sp_fmt_str(event->sync_pkg.name),
        sp_fmt_cstr(event->sync_pkg.fetched ? "true" : "false"),
        sp_fmt_str(event->sync_pkg.source_path)
      );
      break;
    }
    case SPN_EVENT_SYNC_END: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->sync_end.time);
      sp_tty_fmt(&w, "synced {} packages in {.cyan}",
        sp_fmt_uint(event->sync_end.num_synced),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_PUBLISH: {
      sp_tty_fmt(&w, "{} v{}",
        sp_fmt_str(event->publish.name),
        sp_fmt_str(event->publish.version)
      );
      break;
    }
    case SPN_EVENT_PUBLISH_END: {
      sp_tty_fmt(&w, "{} v{} to {} {.gray}",
        sp_fmt_str(event->publish.name),
        sp_fmt_str(event->publish.version),
        sp_fmt_str(event->publish.index),
        sp_fmt_str(event->publish.url)
      );
      break;
    }
    case SPN_EVENT_COMPILE_START: {
      if (!sp_str_empty(event->compile_start.version)) {
        sp_tty_fmt(&w, "v{}", sp_fmt_str(event->compile_start.version));
      }
      break;
    }
    case SPN_EVENT_SCRIPT_COMPILE: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->script_compile.time);
      sp_tty_fmt(&w, "{.gray} in {.gray}",
        sp_fmt_str(event->script_compile.script_path),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_SCRIPT_USER_FN: {
      sp_tty_fmt(&w, "{}", sp_fmt_str(event->script_user_fn.tag));
      break;
    }
    case SPN_EVENT_TARGET_BUILD_PASSED: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->target_passed.run.time);
      sp_tty_fmt(&w, "{.gray} in {.gray}",
        sp_fmt_str(contextual_path(mem, event->target_passed.source_file)),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_LINK_PASSED: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->link_passed.run.time);
      sp_tty_fmt(&w, "{.gray} in {.gray}",
        sp_fmt_str(event->link_passed.file_name),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_EMBED_START: {
      sp_tty_fmt(&w, "{} files", sp_fmt_uint(event->embed_start.num_files));
      break;
    }
    case SPN_EVENT_EMBED_PASSED: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->embed_passed.time);
      sp_tty_fmt(&w, "{.gray} in {.gray}",
        sp_fmt_str(event->embed_passed.target),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_BUILD_SUMMARY: {
      c8 duration [64] = sp_zero;
      c8 bytes [64] = sp_zero;
      sp_fmt_write_duration_buf(duration, sizeof(duration), event->build_summary.time);
      sp_fmt_write_size_buf(bytes, sizeof(bytes), event->build_summary.hashed_bytes);
      sp_tty_fmt(&w, "{} executed, {} cached in {.gray} | hashed {} ({.gray}), stats {}, rows {}, cache {}r/{}w",
        sp_fmt_uint(event->build_summary.misses),
        sp_fmt_uint(event->build_summary.hits),
        sp_fmt_cstr(duration),
        sp_fmt_uint(event->build_summary.hashed_files),
        sp_fmt_cstr(bytes),
        sp_fmt_uint(event->build_summary.stats),
        sp_fmt_uint(event->build_summary.obs_rows),
        sp_fmt_uint(event->build_summary.cache_reads),
        sp_fmt_uint(event->build_summary.cache_writes)
      );
      break;
    }
    case SPN_EVENT_RESOLVE_PACKAGE: {
      sp_tty_fmt(&w, "{} {.gray}",
        sp_fmt_str(event->resolve_pkg.name),
        sp_fmt_str(event->resolve_pkg.version)
      );
      break;
    }
    case SPN_EVENT_RESOLVE_END: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->resolve_end.time);
      sp_tty_fmt(&w, "{} {} in {.gray}",
        sp_fmt_uint(event->resolve_end.num_resolved),
        sp_fmt_cstr(event->resolve_end.num_resolved == 1 ? "package" : "packages"),
        sp_fmt_cstr(buffer)
      );
      break;
    }
    case SPN_EVENT_LOCK_ADDED:
    case SPN_EVENT_LOCK_REMOVED: {
      sp_tty_fmt(&w, "{.gray}", sp_fmt_str(event->resolve_pkg.version));
      break;
    }
    case SPN_EVENT_CACHE_SWEEP:
    case SPN_EVENT_CACHE_CLEAN: {
      c8 freed [64] = sp_zero;
      c8 size [64] = sp_zero;
      c8 duration [64] = sp_zero;
      sp_fmt_write_size_buf(freed, sizeof(freed), event->sweep.freed);
      sp_fmt_write_size_buf(size, sizeof(size), event->sweep.size);
      sp_fmt_write_duration_buf(duration, sizeof(duration), event->sweep.time);
      sp_tty_fmt(&w, "{} of {} cached in {.gray}",
        sp_fmt_cstr(freed),
        sp_fmt_cstr(size),
        sp_fmt_cstr(duration)
      );
      break;
    }
    case SPN_EVENT_ADDED: {
      sp_tty_fmt(&w, "{.cyan}=={.green}",
        sp_fmt_str(event->added.name),
        sp_fmt_str(event->added.version)
      );
      break;
    }
    case SPN_EVENT_UPDATE_INCOMPATIBLE: {
      sp_tty_fmt(&w, "{.cyan} {.green} (latest {.yellow} is semver incompatible)",
        sp_fmt_str(event->update.name),
        sp_fmt_str(event->update.version),
        sp_fmt_str(event->update.latest)
      );
      break;
    }
    case SPN_EVENT_TARGET_RUN: {
      sp_tty_fmt(&w, "{.gray}", sp_fmt_str(get_contextual_path(ctx, mem, event->target_run.command)));
      break;
    }
    case SPN_EVENT_TEST_PASSED: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->test_passed.time);
      sp_tty_fmt(&w, "in {.gray}", sp_fmt_cstr(buffer));
      break;
    }
    case SPN_EVENT_TEST_FAILED: {
      sp_tty_fmt(&w, "{.yellow} exited with code {}",
        sp_fmt_str(event->test_failed.name),
        sp_fmt_int(event->test_failed.code)
      );
      break;
    }
    case SPN_EVENT_TEST_SUMMARY: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->test_summary.time);
      if (event->test_summary.failed) {
        sp_tty_fmt(&w, "{} passed, {.red} failed in {.gray}",
          sp_fmt_uint(event->test_summary.passed),
          sp_fmt_uint(event->test_summary.failed),
          sp_fmt_cstr(buffer)
        );
      }
      else {
        sp_tty_fmt(&w, "{} passed, {} failed in {.gray}",
          sp_fmt_uint(event->test_summary.passed),
          sp_fmt_uint(event->test_summary.failed),
          sp_fmt_cstr(buffer)
        );
      }
      break;
    }
    case SPN_EVENT_BUILD_PASSED: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->build_passed.time);
      sp_tty_fmt(&w,
        "profile {.cyan} in {.gray} {.gray}",
        sp_fmt_str(event->build_passed.profile),
        sp_fmt_cstr(buffer),
        sp_fmt_str(sp_fmt(mem, "({} executed, {} cached",
          sp_fmt_uint(event->build_passed.misses),
          sp_fmt_uint(event->build_passed.hits)).value)
      );
      if (event->build_passed.warned) {
        sp_tty_fmt(&w, "{.gray}{.yellow}",
          sp_fmt_cstr(", "),
          sp_fmt_str(sp_fmt(mem, "{} with warnings", sp_fmt_uint(event->build_passed.warned)).value)
        );
      }
      sp_tty_fmt(&w, "{.gray}", sp_fmt_cstr(")"));
      break;
    }
    case SPN_EVENT_SCRIPT_CRASHED: {
      if (sp_str_empty(event->crashed.error)) {
        sp_io_write_str(w.io, sp_str_lit("build script crashed"), SP_NULLPTR);
      }
      else {
        sp_tty_fmt(&w, "build script crashed: {.red}", sp_fmt_str(event->crashed.error));
      }
      break;
    }
    case SPN_EVENT_SCRIPT_COMPILE_FAILED: {
      sp_tty_fmt(&w, "could not compile {.cyan}", sp_fmt_str(get_contextual_path(ctx, mem, event->compile_failed.script_path)));
      break;
    }
    case SPN_EVENT_TARGET_BUILD_FAILED: {
      sp_tty_fmt(&w, "could not compile {.cyan}", sp_fmt_str(contextual_path(mem, event->target_failed.source_file)));
      break;
    }
    case SPN_EVENT_TARGET_BUILD_WARNED: {
      sp_tty_fmt(&w, "{.cyan}", sp_fmt_str(contextual_path(mem, event->target_warned.source_file)));
      break;
    }
    case SPN_EVENT_WARM_START: {
      sp_tty_fmt(&w, "{.yellow}", sp_fmt_str(event->warm.triple));
      break;
    }
    case SPN_EVENT_WARM_FAILED: {
      sp_tty_fmt(&w, "could not warm libc for {.yellow}", sp_fmt_str(event->warm_failed.triple));
      break;
    }
    case SPN_EVENT_NODE_FAILED: {
      sp_str_t path = contextual_path(mem, event->node_failed.path);
      sp_str_t tag = event->node_failed.tag;
      switch (event->node_failed.reason) {
        case SPN_NODE_FAILURE_REMOVE: {
          sp_tty_fmt(&w, "{.cyan} could not be removed before node {} ran", sp_fmt_str(path), sp_fmt_str(tag));
          break;
        }
        case SPN_NODE_FAILURE_CREATE: {
          sp_tty_fmt(&w, "{.cyan} could not be created before node {} ran", sp_fmt_str(path), sp_fmt_str(tag));
          break;
        }
        case SPN_NODE_FAILURE_STRAY: {
          sp_tty_fmt(&w, "{.cyan} was written by node {}, which may only write under the work root", sp_fmt_str(path), sp_fmt_str(tag));
          break;
        }
        case SPN_NODE_FAILURE_UNPRODUCED: {
          sp_tty_fmt(&w, "{.cyan} was declared as an output of node {} but was not produced", sp_fmt_str(path), sp_fmt_str(tag));
          break;
        }
        case SPN_NODE_FAILURE_COPY: {
          sp_tty_fmt(&w, "{.cyan} output of node {} could not be copied into the build", sp_fmt_str(path), sp_fmt_str(tag));
          break;
        }
        case SPN_NODE_FAILURE_NONE: {
          sp_unreachable_case();
        }
      }
      break;
    }
    case SPN_EVENT_PUBLISH_FAILED: {
      sp_tty_fmt(&w, "{.cyan} could not be published to {}",
        sp_fmt_str(contextual_path(mem, event->publish_failed.path)),
        sp_fmt_str(sp_fs_join_path(mem, spn_publish_root_to_str(event->publish_failed.root), event->publish_failed.dest))
      );
      break;
    }
    case SPN_EVENT_SYNC_FAILED: {
      sp_tty_fmt(
        &w,
        "{} failed to sync from {.gray}: {}",
        sp_fmt_str(colored_name(w.color, mem, event->sync_failed.name)),
        sp_fmt_str(event->sync_failed.url),
        sp_fmt_str(event->sync_failed.error)
      );
      break;
    }
    case SPN_EVENT_SYNC_STALE: {
      sp_tty_fmt(
        &w,
        "{} could not be fetched from {.gray}; using the cached copy",
        sp_fmt_str(colored_name(w.color, mem, event->sync.name)),
        sp_fmt_str(event->sync.url)
      );
      break;
    }
    case SPN_EVENT_SYNC_RETRY: {
      sp_tty_fmt(
        &w,
        "{.gray} ({} {} remaining): {}",
        sp_fmt_str(event->sync_retry.url),
        sp_fmt_uint(event->sync_retry.remaining),
        sp_fmt_cstr(event->sync_retry.remaining == 1 ? "try" : "tries"),
        sp_fmt_str(event->sync_retry.cause)
      );
      break;
    }
    case SPN_EVENT_LOCK_WAIT: {
      switch (event->lock_wait.lock) {
        case SPN_FILE_LOCK_REPO: {
          sp_tty_fmt(&w, "for another spn to release {.gray}", sp_fmt_str(event->lock_wait.name));
          break;
        }
        case SPN_FILE_LOCK_TOOLCHAIN: {
          sp_tty_fmt(&w, "for another spn to release toolchain {}", sp_fmt_str(colored_name(w.color, mem, event->lock_wait.name)));
          break;
        }
      }
      break;
    }
    case SPN_EVENT_ERR: {
      switch (event->err.kind) {
        case SPN_ERR_PATCH_UNUSED: {
          sp_tty_fmt(
            &w,
            "[patch.{.cyan}] does not name a package in this build",
            sp_fmt_str(event->err.subject)
          );
          break;
        }
        case SPN_ERR_PATCH_NOT_GIT: {
          sp_tty_fmt(
            &w,
            "[patch.{.cyan}] names a package whose sources are not fetched from git; only git sources can be patched",
            sp_fmt_str(event->err.subject)
          );
          break;
        }
        case SPN_ERR_PATCH_FILE_MISSING: {
          sp_tty_fmt(
            &w,
            "[patch.{.cyan}] lists {.gray}, which does not exist",
            sp_fmt_str(event->err.subject),
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.patch_file.path))
          );
          break;
        }
        case SPN_ERR_OPTION_UNDECLARED: {
          sp_tty_fmt(
            &w,
            "{} does not declare an option named {.yellow} (set by {.cyan})",
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(event->err.option.option),
            sp_fmt_str(setter_to_str(event->err.option.a))
          );
          break;
        }
        case SPN_ERR_OPTION_BAD_VALUE: {
          sp_tty_fmt(
            &w,
            "{.yellow} is not a valid value for {}.{.cyan} (set by {.cyan})",
            sp_fmt_str(event->err.option.value),
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(event->err.option.option),
            sp_fmt_str(setter_to_str(event->err.option.a))
          );
          break;
        }
        case SPN_ERR_OPTION_CONFLICT: {
          sp_tty_fmt(
            &w,
            "Option conflict on {}.{.cyan}: {.cyan} and {.cyan} request different values",
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(event->err.option.option),
            sp_fmt_str(setter_to_str(event->err.option.a)),
            sp_fmt_str(setter_to_str(event->err.option.b))
          );
          break;
        }
        case SPN_ERR_OPTION_VETO: {
          sp_tty_fmt(
            &w,
            "{.cyan} requires {}.{.cyan} != {.yellow}, but {} set it",
            sp_fmt_str(setter_to_str(event->err.option.a)),
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(event->err.option.option),
            sp_fmt_str(event->err.option.value),
            sp_fmt_str(setter_to_str(event->err.option.b))
          );
          break;
        }
        case SPN_ERR_OPTION_NO_VALUE: {
          sp_tty_fmt(
            &w,
            "No value for {}.{.cyan}: no default matched and nothing set it",
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(event->err.option.option)
          );
          break;
        }
        case SPN_ERR_OPTION_LATE_GATE: {
          sp_tty_fmt(
            &w,
            "The dependency gate on {}'s edge to {.cyan} never settled",
            sp_fmt_str(colored_name(w.color, mem, event->err.option.pkg)),
            sp_fmt_str(setter_to_str(event->err.option.a))
          );
          break;
        }
        case SPN_ERR_OPTION_UNKNOWN_PKG: {
          sp_tty_fmt(
            &w,
            "The root manifest configures {.yellow}, which is not a package in this build",
            sp_fmt_str(event->err.option.pkg)
          );
          break;
        }
        case SPN_ERR_MANIFEST_PARSE: {
          sp_tty_fmt(
            &w,
            "Failed to parse manifest {.cyan}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.manifest_parse.path))
          );
          break;
        }
        case SPN_ERR_MANIFEST_ISSUES: {
          sp_str_t scope = get_issue_scope(event->err.manifest.issues);
          sp_str_t path = get_contextual_path(ctx, mem, event->err.manifest.path);
          if (!sp_str_empty(scope)) {
            sp_tty_fmt(&w, "Invalid toolchain {} ({.gray})", sp_fmt_str(colored_name(w.color, mem, scope)), sp_fmt_str(path));
          }
          else if (sp_str_empty(event->err.subject)) {
            sp_tty_fmt(&w, "Invalid manifest ({.gray})", sp_fmt_str(path));
          }
          else {
            sp_tty_fmt(&w, "{} has an invalid manifest ({.gray})", sp_fmt_str(colored_name(w.color, mem, event->err.subject)), sp_fmt_str(path));
          }
          break;
        }
        case SPN_ERR_PKG_MISMATCH: {
          sp_tty_fmt(
            &w,
            "The manifest at {.cyan} declares {}, but it was requested as {}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.mismatch.path)),
            sp_fmt_str(colored_name(w.color, mem, event->err.mismatch.declared)),
            sp_fmt_str(colored_name(w.color, mem, event->err.mismatch.requested))
          );
          break;
        }
        case SPN_ERR_PKG_UNKNOWN: {
          sp_tty_fmt(
            &w,
            "{} could not be located",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject))
          );
          break;
        }
        case SPN_ERR_PKG_ABSENT: {
          sp_tty_fmt(
            &w,
            "{} is not in {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(sp_fmt(mem, "[deps.{}]", sp_fmt_str(spn_dep_kind_to_str(event->err.absent.table))).value)
          );
          break;
        }
        case SPN_ERR_LOCK_INVALID: {
          sp_tty_fmt(
            &w,
            "Invalid lockfile ({.gray}); delete it and build again to make a new one",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.manifest.path))
          );
          break;
        }
        case SPN_ERR_LOCK_VERSION: {
          sp_tty_fmt(
            &w,
            "{.cyan} is lockfile version {.yellow}, but this spn reads version {.yellow}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.lock_version.path)),
            sp_fmt_uint(event->err.lock_version.version),
            sp_fmt_uint(event->err.lock_version.supported)
          );
          break;
        }
        case SPN_ERR_LOCK_STALE: {
          if (event->err.lock_stale.missing) {
            sp_tty_fmt(&w, "{.cyan} does not exist, and {.cyan} forbids writing it", sp_fmt_cstr("spn.lock"), sp_fmt_cstr("--locked"));
          }
          else {
            sp_tty_fmt(
              &w,
              "{.cyan} is out of date for {}, and {.cyan} forbids updating it",
              sp_fmt_cstr("spn.lock"),
              sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
              sp_fmt_cstr("--locked")
            );
          }
          break;
        }
        case SPN_ERR_LOCK_MISMATCH: {
          spn_err_lock_mismatch_t* err = &event->err.lock_mismatch;
          sp_tty_fmt(
            &w,
            "{.cyan} has {} {.yellow} at {.yellow} ({.gray}), but the index has it at {.yellow} ({.gray}); run {.cyan} to accept the change",
            sp_fmt_cstr("spn.lock"),
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, err->version)),
            sp_fmt_str(err->locked.rev),
            sp_fmt_str(lock_root_to_str(mem, &err->locked)),
            sp_fmt_str(err->found.rev),
            sp_fmt_str(lock_root_to_str(mem, &err->found)),
            sp_fmt_str(sp_fmt(mem, "spn update {}", sp_fmt_str(sp_str_join_n(mem, err->update, sp_da_size(err->update), sp_str_lit(" ")))).value)
          );
          break;
        }
        case SPN_ERR_LOCK_ABSENT: {
          sp_tty_fmt(
            &w,
            "{} is not in {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_cstr("spn.lock")
          );
          break;
        }
        case SPN_ERR_PKG_NO_MATCH: {
          spn_err_unsatisfiable_t* err = &event->err.unsatisfiable;
          sp_str_t requester = sp_str_empty(err->requester) ?
            sp_str_lit("the project") :
            sp_fmt(mem, "{} {}", sp_fmt_str(colored_name(w.color, mem, err->requester)), sp_fmt_str(spn_semver_to_str(mem, err->requester_version))).value;

          sp_tty_fmt(
            &w,
            "No version of {} satisfies {.yellow}, required by {}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(err->range),
            sp_fmt_str(requester)
          );
          break;
        }
        case SPN_ERR_PKG_CONFLICT: {
          spn_err_unsatisfiable_t* err = &event->err.unsatisfiable;
          sp_str_t requester = sp_str_empty(err->requester) ?
            sp_str_lit("the project") :
            sp_fmt(mem, "{} {}", sp_fmt_str(colored_name(w.color, mem, err->requester)), sp_fmt_str(spn_semver_to_str(mem, err->requester_version))).value;

          sp_tty_fmt(
            &w,
            "{} is already selected at {.yellow}, but {} requires {.yellow}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, err->selected)),
            sp_fmt_str(requester),
            sp_fmt_str(err->range)
          );
          break;
        }
        case SPN_ERR_PKG_CONFLICT_EXACT: {
          spn_err_unsatisfiable_t* err = &event->err.unsatisfiable;
          sp_str_t requester = sp_str_empty(err->requester) ?
            sp_str_lit("the project") :
            sp_fmt(mem, "{} {}", sp_fmt_str(colored_name(w.color, mem, err->requester)), sp_fmt_str(spn_semver_to_str(mem, err->requester_version))).value;

          sp_tty_fmt(
            &w,
            "{} is already selected at {.yellow}, which conflicts with the version required by {}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, err->selected)),
            sp_fmt_str(requester)
          );
          break;
        }
        case SPN_ERR_PKG_CONFLICT_PATH: {
          spn_err_path_conflict_t* err = &event->err.path_conflict;
          sp_str_t requester = sp_str_empty(err->requester) ?
            sp_str_lit("the project") :
            sp_fmt(mem, "{} {}", sp_fmt_str(colored_name(w.color, mem, err->requester)), sp_fmt_str(spn_semver_to_str(mem, err->requester_version))).value;

          sp_str_t selected = sp_str_empty(err->selected) ? sp_str_lit("an index") : get_contextual_path(ctx, mem, err->selected);
          sp_str_t requested = sp_str_empty(err->requested) ? sp_str_lit("an index") : get_contextual_path(ctx, mem, err->requested);

          sp_tty_fmt(
            &w,
            "{} is already selected from {.cyan}, but {} requires it from {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(selected),
            sp_fmt_str(requester),
            sp_fmt_str(requested)
          );
          break;
        }
        case SPN_ERR_DEP_CYCLE: {
          sp_tty_fmt(
            &w,
            "{} transitively includes itself",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject))
          );
          break;
        }
        case SPN_ERR_UNIT_CYCLE: {
          sp_tty_fmt(
            &w,
            "{} {.yellow} can't build: its build depends on a tool that links this same instance",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, event->err.unit_cycle.version))
          );
          break;
        }
        case SPN_ERR_DYNAMIC_DUPLICATE: {
          sp_tty_fmt(
            &w,
            "{} {.yellow} and {.yellow} would both load into one process as shared libraries",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, event->err.dynamic_dup.low)),
            sp_fmt_str(spn_semver_to_str(mem, event->err.dynamic_dup.high))
          );
          break;
        }
        case SPN_ERR_STATIC_DUPLICATE: {
          sp_tty_fmt(
            &w,
            "{} {.yellow} and {.yellow} would both link into {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject)),
            sp_fmt_str(spn_semver_to_str(mem, event->err.static_dup.low)),
            sp_fmt_str(spn_semver_to_str(mem, event->err.static_dup.high)),
            sp_fmt_str(event->err.static_dup.target)
          );
          break;
        }
        case SPN_ERR_RESOLVE_TOO_COMPLEX: {
          sp_tty_fmt(
            &w,
            "Resolving {} is too complex; pin a version to reduce the search",
            sp_fmt_str(colored_name(w.color, mem, event->err.subject))
          );
          break;
        }
        case SPN_ERR_PROFILE_INVALID: {
          sp_tty_fmt(
            &w,
            "Invalid profile name {.red}",
            sp_fmt_str(event->err.profile.name)
          );
          break;
        }
        case SPN_ERR_PROFILE_UNDEFINED: {
          sp_tty_fmt(
            &w,
            "No profile named {.red}",
            sp_fmt_str(event->err.profile.name)
          );
          break;
        }
        case SPN_ERR_PROFILE_ARCH:
        case SPN_ERR_PROFILE_ABI: {
          sp_tty_fmt(&w, "{.yellow} isn't a valid target", sp_fmt_str(spn_triple_to_str(mem, event->err.profile.target)));
          break;
        }
        case SPN_ERR_TRIPLE_INVALID: {
          spn_err_triple_parse_t* err = &event->err.triple_parse;
          if (err->segment == SPN_TRIPLE_SEGMENT_EXTRA) {
            sp_tty_fmt(&w, "Target {.yellow} has too many segments; expected {.cyan}", sp_fmt_str(err->value), sp_fmt_cstr("<arch>-<os>-<abi>"));
          }
          else if (sp_str_empty(err->text)) {
            sp_tty_fmt(&w, "Target {.yellow} has an empty {}", sp_fmt_str(err->value), sp_fmt_str(spn_triple_segment_to_str(err->segment)));
          }
          else {
            sp_tty_fmt(&w, "Unknown {} {.red} in target {.yellow}", sp_fmt_str(spn_triple_segment_to_str(err->segment)), sp_fmt_str(err->text), sp_fmt_str(err->value));
          }
          break;
        }
        case SPN_ERR_PROFILE_LINKING: {
          spn_err_profile_t* err = &event->err.profile;
          if (sp_da_size(err->refusals) == 1) {
            write_refusal(&w, mem, err->refusals[0].triple, err->refusals[0].reason);
          }
          else {
            sp_tty_fmt(
              &w,
              "No abi of {.yellow} can link profile {.cyan}",
              sp_fmt_str(spn_triple_to_str(mem, err->target)),
              sp_fmt_str(err->name)
            );
          }
          break;
        }
        case SPN_ERR_SANITIZER_UNSUPPORTED: {
          sp_tty_fmt(
            &w,
            "Toolchain {} doesn't support sanitizer(s) {} for {.yellow}",
            sp_fmt_str(colored_name(w.color, mem, event->err.sanitizer.toolchain)),
            sp_fmt_str(colored_sanitizers(w.color, mem, sp_fmt_style_red, event->err.sanitizer.unsupported)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.sanitizer.target))
          );
          break;
        }
        case SPN_ERR_SANITIZER_STATIC: {
          sp_tty_fmt(
            &w,
            "Sanitizer(s) {} need a dynamic loader, which a static libc removes. Use {.cyan}, or disable the sanitizer(s)",
            sp_fmt_str(colored_sanitizers(w.color, mem, sp_fmt_style_red, event->err.sanitizer.unsupported)),
            sp_fmt_str(sp_str_lit("libc = \"shared\""))
          );
          break;
        }
        case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
          spn_err_compiler_t* err = &event->err.compiler;
          sp_str_t subject = subject_name(mem, err->pkg, err->name);
          sp_str_t target = spn_triple_to_str(mem, err->target);
          sp_str_t toolchain = colored_name(w.color, mem, err->toolchain);
          switch (err->feature) {
            case SPN_CC_FEATURE_LINK_SHARED: {
              sp_tty_fmt(&w, "Library {.cyan} is linked dynamically, but {.yellow} doesn't support shared libraries", sp_fmt_str(subject), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_FRAMEWORKS: {
              sp_str_t frameworks = sp_str_join_n(mem, err->frameworks, (u32)sp_da_size(err->frameworks), sp_str_lit(", "));
              if (err->host.os == SPN_OS_MACOS) {
                sp_tty_fmt(&w, "{.cyan} requires macOS frameworks ({}), but no macOS SDK is present. Install the Xcode Command Line Tools, or set {.cyan}", sp_fmt_str(subject), sp_fmt_str(frameworks), sp_fmt_str(sp_str_lit("SPN_MACOS_SDK")));
              } else {
                sp_tty_fmt(&w, "{.cyan} requires macOS frameworks ({}), but no macOS SDK is present. Due to Apple's licensing, we can only recommend that you compile natively on a macOS host", sp_fmt_str(subject), sp_fmt_str(frameworks));
              }
              break;
            }
            case SPN_CC_FEATURE_LINKER_SCRIPT: {
              sp_tty_fmt(&w, "{.cyan} specifies a linker script, but toolchain {} doesn't support them for {.yellow}", sp_fmt_str(subject), sp_fmt_str(toolchain), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_LINK_EXE: {
              sp_tty_fmt(&w, "Toolchain {} can't link executables for {.yellow}", sp_fmt_str(toolchain), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_LINK_REACTOR: {
              sp_tty_fmt(&w, "Toolchain {} can't link reactor modules for {.yellow}", sp_fmt_str(toolchain), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_ARCHIVE: {
              sp_tty_fmt(&w, "Toolchain {} can't build static archives for {.yellow}", sp_fmt_str(toolchain), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_EXCEPTIONS: {
              sp_tty_fmt(&w, "{.cyan} enables C++ exceptions, but toolchain {} doesn't support them for {.yellow}", sp_fmt_str(subject), sp_fmt_str(toolchain), sp_fmt_str(target));
              break;
            }
            case SPN_CC_FEATURE_STATIC_RUNTIME: {
              sp_tty_fmt(&w, "Library {.cyan} is linked dynamically, but toolchain {} can't link a shared library against a static runtime for {.yellow}. Use {.cyan}", sp_fmt_str(subject), sp_fmt_str(toolchain), sp_fmt_str(target), sp_fmt_str(sp_str_lit("runtime = \"shared\"")));
              break;
            }
          }
          break;
        }
        case SPN_ERR_FS_REMOVE: {
          sp_tty_fmt(
            &w,
            "Failed to remove {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_WASM_INIT_FAILED: {
          sp_io_write_str(w.io, sp_str_lit("Failed to initialize the wasm runtime"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_HTTP_INIT_FAILED: {
          sp_io_write_str(w.io, sp_str_lit("Failed to initialize libcurl"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_CONFIGURE_SOURCE_GLOB: {
          sp_tty_fmt(
            &w,
            "{.cyan} declares a configure source {.yellow}, which matched no files",
            sp_fmt_str(event->err.configure_source.name),
            sp_fmt_str(event->err.configure_source.source)
          );
          break;
        }
        case SPN_ERR_CONFIGURE_SOURCE_MISSING: {
          sp_tty_fmt(
            &w,
            "{.cyan} declares a configure source {.yellow}, which is not a file",
            sp_fmt_str(event->err.configure_source.name),
            sp_fmt_str(event->err.configure_source.source)
          );
          break;
        }
        case SPN_ERR_FS_READ: {
          sp_tty_fmt(
            &w,
            "Failed to read {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_TOC_MAGIC: {
          sp_tty_fmt(
            &w,
            "{.cyan} is not a static archive",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_TOC_TRUNCATED: {
          sp_tty_fmt(
            &w,
            "The symbol table in {.cyan} is truncated",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_TOC_MISSING: {
          sp_tty_fmt(
            &w,
            "{.cyan} has no symbol table",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_TOC_OPAQUE: {
          sp_tty_fmt(
            &w,
            "{.cyan} holds an object with no readable symbol table",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_SCAFFOLD_TEMPLATE: {
          sp_tty_fmt(
            &w,
            "Failed to render the template for {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_FS_CREATE_DIR: {
          sp_tty_fmt(
            &w,
            "Failed to create directory {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_FS_WRITE: {
          sp_tty_fmt(
            &w,
            "Failed to write {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_INDEX_UNKNOWN: {
          sp_tty_fmt(
            &w,
            "Index {.cyan} not found",
            sp_fmt_str(event->err.index.name)
          );
          break;
        }
        case SPN_ERR_INDEX_SYNC: {
          sp_tty_fmt(
            &w,
            "Failed to sync index {.cyan} from {.gray}",
            sp_fmt_str(event->err.index.name),
            sp_fmt_str(event->err.index.url)
          );
          break;
        }
        case SPN_ERR_MANIFEST_EDIT: {
          sp_tty_fmt(
            &w,
            "Failed to edit {.cyan}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.manifest_parse.path))
          );
          break;
        }
        case SPN_ERR_NO_MANIFEST: {
          sp_tty_fmt(
            &w,
            "No manifest found at {.cyan}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.no_manifest.path))
          );
          break;
        }
        case SPN_ERR_NOT_GIT_REPO: {
          sp_tty_fmt(
            &w,
            "{.cyan} is not inside a git repository",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.not_git_repo.path))
          );
          break;
        }
        case SPN_ERR_GIT: {
          sp_tty_fmt(
            &w,
            "Git command failed: {.yellow}",
            sp_fmt_str(event->err.git.command)
          );
          break;
        }
        case SPN_ERR_GIT_FETCH: {
          sp_tty_fmt(
            &w,
            "Failed to fetch {.cyan} from {.gray}",
            sp_fmt_str(event->err.subject),
            sp_fmt_str(event->err.git_fetch.url)
          );
          break;
        }
        case SPN_ERR_VERSION_EXISTS: {
          sp_tty_fmt(
            &w,
            "Version {.yellow} of {.cyan} already exists in the index",
            sp_fmt_str(event->err.version_exists.version),
            sp_fmt_str(event->err.version_exists.name)
          );
          break;
        }
        case SPN_ERR_VERSION_INVALID: {
          sp_tty_fmt(
            &w,
            "Invalid version {.red}",
            sp_fmt_str(event->err.version_invalid.requested)
          );
          break;
        }
        case SPN_ERR_INDEX_PINNED: {
          sp_tty_fmt(
            &w,
            "Index {.cyan} is pinned to a revision and cannot be published to",
            sp_fmt_str(event->err.index.name)
          );
          break;
        }
        case SPN_ERR_INDEX_EXISTS: {
          sp_tty_fmt(
            &w,
            "Index {.cyan} is already declared; pick another with {.cyan}",
            sp_fmt_str(event->err.index.name),
            sp_fmt_cstr("--name")
          );
          break;
        }
        case SPN_ERR_INDEX_UNMARKED: {
          sp_tty_fmt(
            &w,
            "{.gray} is not a package index; index {.cyan} has no {.cyan}",
            sp_fmt_str(event->err.index.url),
            sp_fmt_str(event->err.index.name),
            sp_fmt_cstr("spn.index.toml")
          );
          break;
        }
        case SPN_ERR_INDEX_UNPINNED: {
          sp_tty_fmt(
            &w,
            "Index {.cyan} holds manifests and is declared in the user config, so it needs a {.cyan}",
            sp_fmt_str(event->err.index.name),
            sp_fmt_cstr("rev")
          );
          break;
        }
        case SPN_ERR_INDEX_MARKER_INVALID: {
          sp_tty_fmt(
            &w,
            "Invalid index marker ({.gray})",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.manifest.path))
          );
          break;
        }
        case SPN_ERR_INDEX_VERSION: {
          sp_tty_fmt(
            &w,
            "{.cyan} is index version {.yellow}, but this spn reads version {.yellow}; update spn",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.index_version.path)),
            sp_fmt_uint(event->err.index_version.version),
            sp_fmt_uint(event->err.index_version.supported)
          );
          break;
        }
        case SPN_ERR_INDEX_FORMAT: {
          sp_tty_fmt(
            &w,
            "{.cyan} is a {.yellow} index, which this spn does not read; update spn",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.index_format.path)),
            sp_fmt_str(event->err.index_format.format)
          );
          break;
        }
        case SPN_ERR_INDEX_PUBLISH_PROTOCOL: {
          sp_tty_fmt(
            &w,
            "Index {.cyan} does not support publishing",
            sp_fmt_str(event->err.index.name)
          );
          break;
        }
        case SPN_ERR_INDEX_CORRUPT: {
          sp_tty_fmt(
            &w,
            "Index entry for {.cyan} at {.cyan} is corrupt",
            sp_fmt_str(event->err.index_corrupt.name),
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.index_corrupt.path))
          );
          break;
        }
        case SPN_ERR_PUBLISH_PUSH: {
          sp_tty_fmt(
            &w,
            "Failed to push to {.gray}",
            sp_fmt_str(event->err.publish.url)
          );
          break;
        }
        case SPN_ERR_PUBLISH_DIRTY: {
          sp_tty_fmt(
            &w,
            "{.cyan} has uncommitted changes; commit them or pass {.yellow}",
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.publish.path)),
            sp_fmt_cstr("--allow-dirty")
          );
          break;
        }
        case SPN_ERR_PUBLISH_UNPUSHED: {
          sp_tty_fmt(
            &w,
            "Commit {.yellow} is not on any branch of {.gray}; push it first",
            sp_fmt_str(event->err.publish.rev),
            sp_fmt_str(event->err.publish.url)
          );
          break;
        }
        case SPN_ERR_PUBLISH_ABBREVIATED: {
          sp_tty_fmt(
            &w,
            "Upstream commit {.yellow} of {.gray} is not a full commit; {.cyan} needs all 40 characters",
            sp_fmt_str(event->err.publish.rev),
            sp_fmt_str(event->err.publish.url),
            sp_fmt_cstr("[package.upstream]")
          );
          break;
        }
        case SPN_ERR_INDEX_PATH_DEP: {
          sp_tty_fmt(
            &w,
            "{.cyan} depends on {.cyan} by path; index packages must depend on versions",
            sp_fmt_str(event->err.pkg.name),
            sp_fmt_str(event->err.pkg.requested)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_FETCH: {
          sp_tty_fmt(
            &w,
            "Failed to download toolchain {} from {.gray}",
            sp_fmt_str(colored_name(w.color, mem, event->err.artifact.name)),
            sp_fmt_str(event->err.artifact.url)
          );
          break;
        }
        case SPN_ERR_CERT_UNTRUSTED: {
          sp_tty_fmt(
            &w,
            "The certificate of {.gray} is not trusted",
            sp_fmt_str(event->err.cert.url)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_SHA: {
          sp_tty_fmt(
            &w,
            "Toolchain {} downloaded from {.gray} failed sha256 verification",
            sp_fmt_str(colored_name(w.color, mem, event->err.artifact.name)),
            sp_fmt_str(event->err.artifact.url)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_READ: {
          sp_tty_fmt(
            &w,
            "Failed to read toolchain {} downloaded from {.gray}",
            sp_fmt_str(colored_name(w.color, mem, event->err.artifact.name)),
            sp_fmt_str(event->err.artifact.url)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_EXTRACT: {
          sp_tty_fmt(
            &w,
            "Failed to extract toolchain {} downloaded from {.gray}",
            sp_fmt_str(colored_name(w.color, mem, event->err.artifact.name)),
            sp_fmt_str(event->err.artifact.url)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_UNKNOWN: {
          sp_tty_fmt(&w, "No toolchain named {}", sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)));
          break;
        }
        case SPN_ERR_TOOLCHAIN_TARGET: {
          sp_tty_fmt(
            &w,
            "Requested toolchain {}, but it doesn't support {.yellow}",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.target))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_SYSROOT: {
          sp_tty_fmt(
            &w,
            "Toolchain {} requires a sysroot to build for {.yellow}, but none is present",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.target))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_SDK_MACOS: {
          sp_tty_fmt(
            &w,
            "Toolchain {} requires a macOS SDK to build for {.yellow}, but none is present",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.target))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_SDK_MSVC: {
          sp_tty_fmt(
            &w,
            "Toolchain {} requires the MSVC SDK to build for {.yellow}, but none is present",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.target))
          );
          break;
        }
        case SPN_ERR_MSVC_TOOLSET: {
          sp_tty_fmt(
            &w,
            "No installed MSVC toolset matches {.yellow} for {.yellow}",
            sp_fmt_str(event->err.msvc.request),
            sp_fmt_str(spn_triple_to_str(mem, event->err.msvc.target))
          );
          break;
        }
        case SPN_ERR_MSVC_SDK: {
          sp_tty_fmt(
            &w,
            "No installed Windows SDK matches {.yellow} for {.yellow}",
            sp_fmt_str(event->err.msvc.request),
            sp_fmt_str(spn_triple_to_str(mem, event->err.msvc.target))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_MSVC_LINKER_HOST: {
          sp_tty_fmt(
            &w,
            "Toolchain {} can't link {.yellow} with {.red} on {.yellow}; set {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.target)),
            sp_fmt_str(sp_str_lit("link.exe")),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.host)),
            sp_fmt_str(sp_str_lit("linker = \"lld\""))
          );
          break;
        }
        case SPN_ERR_TARGET_ABI: {
          sp_tty_fmt(
            &w,
            "{.yellow} is missing an abi. Pass {.cyan}",
            sp_fmt_str(spn_triple_to_str(mem, event->err.profile.target)),
            sp_fmt_str(sp_str_lit("--abi"))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_NONE: {
          sp_str_t target = spn_triple_to_str(mem, event->err.toolchain.target);
          if (event->err.toolchain.sanitizers) {
            sp_tty_fmt(
              &w,
              "No toolchain supports sanitizer(s) {} for {.yellow}",
              sp_fmt_str(colored_sanitizers(w.color, mem, sp_fmt_style_red, event->err.toolchain.sanitizers)),
              sp_fmt_str(target)
            );
          } else {
            sp_tty_fmt(&w, "No toolchain can build for {.yellow}", sp_fmt_str(target));
          }
          break;
        }
        case SPN_ERR_TOOLCHAIN_UNAVAILABLE_FOR_HOST: {
          sp_tty_fmt(
            &w,
            "Toolchain {} isn't available on {.yellow}",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.host))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_NOT_INSTALLED: {
          sp_tty_fmt(
            &w,
            "Toolchain {} isn't installed on {.yellow}",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain.host))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_MISSING: {
          sp_tty_fmt(
            &w,
            "Toolchain {} can't find {.cyan}",
            sp_fmt_str(colored_name(w.color, mem, event->err.program.name)),
            sp_fmt_str(event->err.program.program)
          );
          break;
        }
        case SPN_ERR_WASM_READ_FAILED: {
          sp_tty_fmt(
            &w,
            "Failed to read build script {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path))
          );
          break;
        }
        case SPN_ERR_WASM_MODULE_LOAD_FAILED: {
          sp_tty_fmt(
            &w,
            "Failed to load build script {.cyan}: {.red}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path)),
            sp_fmt_str(event->err.wasm.error)
          );
          break;
        }
        case SPN_ERR_WASM_MODULE_INSTANCE_FAILED: {
          sp_tty_fmt(
            &w,
            "Failed to instantiate build script {.cyan}: {.red}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path)),
            sp_fmt_str(event->err.wasm.error)
          );
          break;
        }
        case SPN_ERR_WASM_THREAD_ENV_FAILED: {
          sp_tty_fmt(
            &w,
            "Failed to init wasm thread env for build script {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path))
          );
          break;
        }
        case SPN_ERR_WASM_CTX_FAILED: {
          sp_tty_fmt(
            &w,
            "Failed to create wasm context for build script {.cyan}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path))
          );
          break;
        }
        case SPN_ERR_WASM_MODULE_CALL_FAILED: {
          sp_tty_fmt(
            &w,
            "Build script {.cyan} crashed: {.red}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path)),
            sp_fmt_str(event->err.wasm.error)
          );
          break;
        }
        case SPN_ERR_WASM_SCRIPT_ERROR: {
          sp_tty_fmt(
            &w,
            "Build script {.cyan} returned {.red}",
            sp_fmt_str(contextual_path(mem, event->err.wasm.path)),
            sp_fmt_int(event->err.wasm.rc)
          );
          break;
        }
        case SPN_ERR_HANDLE_NULL: {
          sp_tty_fmt(&w, "Build script {.cyan} passed a null handle; {.yellow} returns one when its tag contains '/'", sp_fmt_str(contextual_path(mem, event->err.wasm.path)), sp_fmt_cstr("spn_add_node"));
          break;
        }
        case SPN_ERR_HANDLE_KIND: {
          sp_tty_fmt(&w, "Build script {.cyan} passed a handle of the wrong kind", sp_fmt_str(contextual_path(mem, event->err.wasm.path)));
          break;
        }
        case SPN_ERR_NODE_NO_SCRIPT: {
          sp_tty_fmt(&w, "Build script {.cyan} set a node function, but the package has no {.yellow} script", sp_fmt_str(contextual_path(mem, event->err.wasm.path)), sp_fmt_cstr("[package.build]"));
          break;
        }
        case SPN_ERR_NODE_NO_OUTPUTS: {
          sp_tty_fmt(&w, "Build script {.cyan} linked from a node that has declared no outputs", sp_fmt_str(contextual_path(mem, event->err.wasm.path)));
          break;
        }
        case SPN_ERR_PATH_NOT_NORMAL: {
          sp_tty_fmt(&w, "Build script {.cyan} used a path that is absolute, contains a backslash, or has '.', '..', or empty components, or a component that is a drive letter; a path is relative to its root", sp_fmt_str(contextual_path(mem, event->err.wasm.path)));
          break;
        }
        case SPN_ERR_PATH_NOT_MOUNTED: {
          sp_tty_fmt(&w, "Build script {.cyan} used a root that is not mounted; the roots are the three that {.yellow} hands out", sp_fmt_str(contextual_path(mem, event->err.wasm.path)), sp_fmt_cstr("spn_get_root_*"));
          break;
        }
        case SPN_ERR_OUTPUT_OUTSIDE: {
          sp_tty_fmt(&w, "Build script {.cyan} declared an output that is not under the work root", sp_fmt_str(contextual_path(mem, event->err.wasm.path)));
          break;
        }
        case SPN_ERR_OUTPUT_IS_ROOT: {
          sp_tty_fmt(&w, "Build script {.cyan} declared an output that names the work root instead of a path under it", sp_fmt_str(contextual_path(mem, event->err.wasm.path)));
          break;
        }
        case SPN_ERR_STR_NULL: {
          sp_tty_fmt(&w, "Build script {.cyan} passed a null string; {.yellow} returns one when its format is malformed or has more placeholders than arguments", sp_fmt_str(contextual_path(mem, event->err.wasm.path)), sp_fmt_cstr("spn_fmt"));
          break;
        }
        case SPN_ERR_WASM_EXPORT_NOT_FOUND: {
          sp_tty_fmt(
            &w,
            "Referenced symbol {.yellow} was not found in {.cyan}",
            sp_fmt_str(event->err.wasm.error),
            sp_fmt_str(contextual_path(mem, event->err.wasm.path))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_NO_CXX: {
          sp_tty_fmt(
            &w,
            "Toolchain {} has no C++ compiler; set {.cyan} in its [[toolchain]] table",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain.name)),
            sp_fmt_str(sp_str_lit("cxx"))
          );
          break;
        }
        case SPN_ERR_TARGET_LINKAGE: {
          sp_tty_fmt(
            &w,
            "Library {.cyan} doesn't support {.red} linkage, requested by {}",
            sp_fmt_str(subject_name(mem, event->err.target.pkg, event->err.target.name)),
            sp_fmt_str(event->err.target.requested),
            sp_fmt_str(requester_to_str(event->err.target.requester))
          );
          if (!sp_str_empty(event->err.target.consumer)) {
            sp_tty_fmt(&w, ", but {.cyan} links it", sp_fmt_str(event->err.target.consumer));
          }
          switch (event->err.target.requester) {
            case SPN_LINKAGE_REQUESTER_TARGET: {
              sp_str_t kind = spn_linkage_to_str(event->err.target.supported[0]);
              sp_tty_fmt(
                &w,
                ". Set {.cyan} in the root manifest to link it as {}",
                sp_fmt_str(sp_fmt(mem, "[config.{}] kind = \"{}\"", sp_fmt_str(event->err.target.pkg), sp_fmt_str(kind)).value),
                sp_fmt_str(kind)
              );
              break;
            }
            case SPN_LINKAGE_REQUESTER_PROFILE:
            case SPN_LINKAGE_REQUESTER_ROOT_MANIFEST:
            case SPN_LINKAGE_REQUESTER_LIBC:
            case SPN_LINKAGE_REQUESTER_NO_LOADER: {
              break;
            }
          }
          break;
        }
        case SPN_ERR_TARGET_DUPLICATE: {
          sp_tty_fmt(
            &w,
            "{.cyan} declares a target {.yellow}, which collides with another target of the same name and kind",
            sp_fmt_str(event->err.target.pkg),
            sp_fmt_str(event->err.target.name)
          );
          break;
        }
        case SPN_ERR_TARGET_RESERVED: {
          sp_tty_fmt(
            &w,
            "{.cyan} names an executable {.yellow}, which collides with a build output directory (store, work, test)",
            sp_fmt_str(event->err.target.pkg),
            sp_fmt_str(event->err.target.name)
          );
          break;
        }
        case SPN_ERR_TARGET_COLLISION: {
          sp_tty_fmt(
            &w,
            "{.cyan} and {.cyan} both produce {.yellow}; they cannot be staged beside {.cyan}",
            sp_fmt_str(event->err.collision.pkg),
            sp_fmt_str(event->err.collision.other),
            sp_fmt_str(event->err.collision.name),
            sp_fmt_str(event->err.collision.exe)
          );
          break;
        }
        case SPN_ERR_TARGET_SOURCE_GLOB: {
          sp_tty_fmt(
            &w,
            "{.cyan} declares a source {.yellow} for {.yellow}, which could not be expanded",
            sp_fmt_str(event->err.target_source.pkg),
            sp_fmt_str(event->err.target_source.source),
            sp_fmt_str(event->err.target_source.name)
          );
          break;
        }
        case SPN_ERR_TARGET_STUB_REQUIRED: {
          sp_tty_fmt(
            &w,
            "{.cyan} ships {.yellow} for {.yellow} with no {.cyan}, but this target links a DLL through its import library",
            sp_fmt_str(event->err.target_source.pkg),
            sp_fmt_str(event->err.target_source.source),
            sp_fmt_str(event->err.target_source.name),
            sp_fmt_str(sp_str_lit("stub"))
          );
          break;
        }
        case SPN_ERR_TARGET_PREBUILTS: {
          sp_tty_fmt(
            &w,
            "{.cyan} declares more than one shared entry for {.yellow} in this build, including {.yellow}",
            sp_fmt_str(event->err.target_source.pkg),
            sp_fmt_str(event->err.target_source.name),
            sp_fmt_str(event->err.target_source.source)
          );
          break;
        }
        case SPN_ERR_TARGET_HOST_DEPS: {
          spn_err_host_deps_t* err = &event->err.host_deps;
          sp_str_t subject = subject_name(mem, err->pkg, err->name);
          sp_str_t libs = sp_str_join_n(mem, err->libs, (u32)sp_da_size(err->libs), sp_str_lit(", "));
          if (err->script) {
            sp_tty_fmt(
              &w,
              "{.cyan} links host libraries ({.red}) into a build script, which is compiled to WebAssembly and can't use them. Gate them with {.cyan}",
              sp_fmt_str(subject),
              sp_fmt_str(libs),
              sp_fmt_str(sp_str_lit("when"))
            );
            break;
          }
          sp_tty_fmt(
            &w,
            "{.cyan} links host libraries ({.red}), which need a build against this machine, and toolchain {.yellow} targeting {.yellow} isn't one",
            sp_fmt_str(subject),
            sp_fmt_str(libs),
            sp_fmt_str(err->toolchain),
            sp_fmt_str(spn_triple_to_str(mem, err->target))
          );
          break;
        }
        case SPN_ERR_TARGET_SELECTION: {
          sp_tty_fmt(
            &w,
            "Target {.yellow} is not defined for the selected target kinds",
            sp_fmt_str(event->err.target.name)
          );
          break;
        }
        case SPN_ERR_TOOL_MISSING: {
          sp_tty_fmt(
            &w,
            "Tool {.yellow} has no sources, so it has no executable",
            sp_fmt_str(event->err.target.name)
          );
          break;
        }
        case SPN_ERR_INIT_EXISTS: {
          sp_tty_fmt(
            &w,
            "{.cyan} already exists",
            sp_fmt_str(contextual_path(mem, event->err.fs.path))
          );
          break;
        }
        case SPN_ERR_INIT_NAME: {
          sp_tty_fmt(
            &w,
            "Invalid name {.quote}",
            sp_fmt_str(event->err.pkg.name)
          );
          break;
        }
        case SPN_ERR_SCRIPT_MISSING: {
          sp_tty_fmt(
            &w,
            "Script {.yellow} has no binary at {.cyan}",
            sp_fmt_str(event->err.script.name),
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.script.path))
          );
          break;
        }
        case SPN_ERR_TEST_MISSING: {
          sp_tty_fmt(
            &w,
            "Test {.yellow} has no binary at {.cyan}",
            sp_fmt_str(event->err.script.name),
            sp_fmt_str(get_contextual_path(ctx, mem, event->err.script.path))
          );
          break;
        }
        case SPN_ERR_TEST_FAILED: {
          sp_io_write_str(w.io, sp_str_lit("One or more tests failed"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_SCRIPT_FAILED: {
          sp_tty_fmt(
            &w,
            "Script {.yellow} failed with exit code {}",
            sp_fmt_str(event->err.script.name),
            sp_fmt_int(event->err.script.code)
          );
          break;
        }
        case SPN_ERR_DAG_DUPLICATE_OUTPUT: {
          sp_io_write_str(w.io, sp_str_lit("Two build actions produce the same output file"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_DAG_OUTPUT_NAME: {
          sp_io_write_str(w.io, sp_str_lit("A build action output has no file name"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_DAG_PINNED_OUTPUT: {
          sp_tty_fmt(&w, "{.cyan} is declared as a build action output, but it is inside an immutable cache directory", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_PATH_KIND: {
          sp_tty_fmt(&w, "{.cyan} is declared as both a file and a directory", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_TREE_ROOT: {
          sp_tty_fmt(&w, "{.cyan} is declared as a directory output, but it contains a build root", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_TREE_INPUT: {
          sp_tty_fmt(&w, "{.cyan} is a directory, but no build action produces it", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_NESTED_OUTPUT: {
          sp_tty_fmt(&w, "{.cyan} is a build action output, but it is inside a directory that is itself a build action output", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_NESTED_INPUT: {
          sp_tty_fmt(&w, "{.cyan} is a build input, but it is inside a directory produced by a build action", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_MISSING_INPUT: {
          sp_tty_fmt(&w, "{.cyan} doesn't exist, but is listed as an input", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_STAGE_UNPRODUCED: {
          sp_tty_fmt(&w, "{.cyan} is listed under [stage], but no node of this package produces it", sp_fmt_str(event->err.dag.path));
          break;
        }
        case SPN_ERR_STAGE_OVERLAP: {
          sp_tty_fmt(&w, "{.cyan} is both a [stage] destination and part of the build", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_STAGE_OBSERVED: {
          sp_tty_fmt(&w, "{.cyan} is a [stage] destination, which the build may not read", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_STAGE_COLLISION: {
          sp_tty_fmt(&w, "{.cyan} already exists and was not staged by spn; remove it or change its [stage] destination", sp_fmt_str(contextual_path(mem, event->err.fs.path)));
          break;
        }
        case SPN_ERR_TOOLCHAIN_FILE_MISSING: {
          sp_tty_fmt(
            &w,
            "Toolchain {} stages {.cyan} beside executables for {.yellow}, but it is not a file",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain_file.toolchain)),
            sp_fmt_str(contextual_path(mem, event->err.toolchain_file.path)),
            sp_fmt_str(spn_triple_to_str(mem, event->err.toolchain_file.target))
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_FILE_DUPLICATE: {
          sp_tty_fmt(
            &w,
            "Toolchain {} stages {.cyan} and {.cyan} beside {.cyan}, but they have the same name",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain_file_dup.toolchain)),
            sp_fmt_str(contextual_path(mem, event->err.toolchain_file_dup.other)),
            sp_fmt_str(contextual_path(mem, event->err.toolchain_file_dup.path)),
            sp_fmt_str(event->err.toolchain_file_dup.exe)
          );
          break;
        }
        case SPN_ERR_TOOLCHAIN_FILE_COLLISION: {
          sp_tty_fmt(
            &w,
            "Toolchain {} stages {.cyan} beside {.cyan}, where {.cyan} stages a library of the same name",
            sp_fmt_str(colored_name(w.color, mem, event->err.toolchain_file_collision.toolchain)),
            sp_fmt_str(contextual_path(mem, event->err.toolchain_file_collision.path)),
            sp_fmt_str(event->err.toolchain_file_collision.exe),
            sp_fmt_str(event->err.toolchain_file_collision.pkg)
          );
          break;
        }
        case SPN_ERR_DAG_MISSING_OUTPUT: {
          sp_tty_fmt(&w, "{.cyan} was not produced by the action that declares it as an output", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_DEPFILE: {
          sp_tty_fmt(&w, "The compiler did not write a valid dependency file for {.cyan}", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_STORE_READ: {
          sp_tty_fmt(&w, "{.cyan} could not be read from the content store", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_STORE_WRITE: {
          sp_tty_fmt(&w, "{.cyan} could not be written to the content store", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_OUTPUT_WRITE: {
          sp_tty_fmt(&w, "{.cyan} could not be written", sp_fmt_str(get_contextual_path(ctx, mem, event->err.dag.path)));
          break;
        }
        case SPN_ERR_DAG_SCRATCH: {
          sp_io_write_str(w.io, sp_str_lit("Failed to create a scratch directory for the build"), SP_NULLPTR);
          break;
        }
        case SPN_ERR_DAG_STALLED: {
          sp_io_write_str(w.io, sp_str_lit("The build graph stalled before completing"), SP_NULLPTR);
          break;
        }
        default: {
          sp_io_write_str(w.io, sp_str_lit("Unknown error"), SP_NULLPTR);
          break;
        }
      }
      break;
    }
    case SPN_EVENT_USER_LOG: {
      sp_io_write_str(w.io, event->user_log.message, SP_NULLPTR);
      break;
    }
    case SPN_EVENT_INIT_BUILD_GRAPH: {
      if (sp_str_empty(event->graph_init.version)) {
        sp_tty_fmt(&w, "{}, {.yellow}, {.green}",
          sp_fmt_str(event->graph_init.profile),
          sp_fmt_str(event->graph_init.target),
          sp_fmt_str(event->graph_init.toolchain)
        );
      } else {
        sp_tty_fmt(&w, "{}, {.yellow}, {.green} {.gray}",
          sp_fmt_str(event->graph_init.profile),
          sp_fmt_str(event->graph_init.target),
          sp_fmt_str(event->graph_init.toolchain),
          sp_fmt_str(event->graph_init.version)
        );
      }
      break;
    }
    case SPN_EVENT_LINK_START: {
      sp_tty_fmt(&w, "{.cyan}",
        sp_fmt_str(event->link_start.subject.target)
      );
      break;
    }
    case SPN_EVENT_LINK_FAILED: {
      sp_tty_fmt(&w, "could not link {.cyan}", sp_fmt_str(event->link_failed.subject.target));
      break;
    }
    case SPN_EVENT_LINK_WARNED: {
      sp_tty_fmt(&w, "{.cyan}", sp_fmt_str(event->link_warned.file_name));
      break;
    }
    case SPN_EVENT_CONFIGURE_OK: {
      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->configure.time);
      sp_tty_fmt(&w, "in {.gray}", sp_fmt_cstr(buffer));
      break;
    }
    case SPN_EVENT_EMBED_FAILED: {
      sp_tty_fmt(&w, "{}: {}",
        sp_fmt_str(contextual_path(mem, event->embed_failed.path)),
        sp_fmt_str(event->embed_failed.error)
      );
      break;
    }
    case SPN_EVENT_BUILD_FAILED: {
      sp_tty_fmt(&w, "profile {.cyan}", sp_fmt_str(event->build_failed.profile));
      break;
    }
    default: {
      break;
    }
  }

  return sp_io_dyn_mem_writer_take_str(&buf);
}

static void write_event(sp_tty_t* tty, sp_str_t verb, sp_fmt_style_t style, sp_str_t pkg, sp_str_t detail) {
  sp_tty_fmt(tty, "{:>12 .bold .$}", sp_fmt_style(style), sp_fmt_str(verb));

  if (sp_str_empty(pkg)) {
    sp_tty_fmt(tty, " {.gray}", sp_fmt_cstr("▐"));
  } else {
    sp_io_write_c8(tty->io, ' ');
    write_name(tty, pkg);
  }
  if (!sp_str_empty(detail)) {
    sp_tty_fmt(tty, " {}", sp_fmt_str(detail));
  }
  sp_io_write_c8(tty->io, '\n');
}

static void write_error(sp_tty_t* tty, sp_str_t detail) {
  sp_tty_fmt(tty, "{.bold .red}", sp_fmt_cstr("error:"));
  if (!sp_str_empty(detail)) {
    sp_tty_fmt(tty, " {}", sp_fmt_str(detail));
  }
  sp_io_write_c8(tty->io, '\n');
}

static sp_str_t event_subject(spn_event_t* event) {
  switch (event->kind) {
    case SPN_EVENT_SYNC_FAILED:               return event->sync_failed.name;
    case SPN_EVENT_ERR: {
      return sp_str_empty(event->err.subject) ? event->pkg : event->err.subject;
    }
    case SPN_EVENT_TARGET_BUILD_PASSED:       return event->target_passed.subject.pkg;
    case SPN_EVENT_TARGET_BUILD_FAILED:       return event->target_failed.subject.pkg;
    case SPN_EVENT_TARGET_BUILD_WARNED:       return event->target_warned.subject.pkg;
    case SPN_EVENT_LINK_START:                return event->link_start.subject.pkg;
    case SPN_EVENT_LINK_PASSED:               return event->link_passed.subject.pkg;
    case SPN_EVENT_LINK_FAILED:               return event->link_failed.subject.pkg;
    case SPN_EVENT_LINK_WARNED:               return event->link_warned.subject.pkg;
    default:                                  return event->pkg;
  }
}

static void write_run_output(sp_tty_t* w, const spn_event_run_t* run) {
  sp_io_write_str(w->io, run->out, SP_NULLPTR);
  sp_io_write_str(w->io, run->err, SP_NULLPTR);
}

static void render_event_extra(sp_tty_t* w, spn_event_t* event) {
  switch (event->kind) {
    case SPN_EVENT_TARGET_BUILD_FAILED: {
      write_run_output(w, &event->target_failed.run);
      break;
    }
    case SPN_EVENT_WARM_FAILED: {
      write_run_output(w, &event->warm_failed.run);
      break;
    }
    case SPN_EVENT_LINK_FAILED: {
      write_run_output(w, &event->link_failed.run);
      break;
    }
    case SPN_EVENT_TARGET_BUILD_WARNED: {
      sp_io_write_str(w->io, event->target_warned.output, SP_NULLPTR);
      break;
    }
    case SPN_EVENT_LINK_WARNED: {
      sp_io_write_str(w->io, event->link_warned.output, SP_NULLPTR);
      break;
    }
    case SPN_EVENT_TEST_FAILED: {
      sp_io_write_str(w->io, event->test_failed.out, SP_NULLPTR);
      sp_io_write_str(w->io, event->test_failed.err, SP_NULLPTR);
      break;
    }
    case SPN_EVENT_SCRIPT_COMPILE_FAILED: {
      sp_io_write_str(w->io, event->compile_failed.error, SP_NULLPTR);
      break;
    }
    case SPN_EVENT_ERR: {
      switch (event->err.kind) {
        case SPN_ERR_LOCK_INVALID:
        case SPN_ERR_INDEX_MARKER_INVALID:
        case SPN_ERR_MANIFEST_ISSUES: {
          sp_da(spn_toml_issue_t) issues = event->err.manifest.issues;
          if (sp_str_empty(get_issue_scope(issues))) {
            sp_da_for(issues, it) {
              sp_io_write_str(w->io, sp_str_lit("  - "), SP_NULLPTR);
              if (!sp_str_empty(issues[it].scope)) {
                sp_tty_fmt(w, "Toolchain {}: ", SP_FMT_STR(issues[it].scope));
              }
              write_issue(w, &issues[it]);
              sp_io_write_c8(w->io, '\n');
            }
          } else {
            sp_da_for(issues, it) {
              sp_io_write_str(w->io, sp_str_lit("  - "), SP_NULLPTR);
              write_issue(w, &issues[it]);
              sp_io_write_c8(w->io, '\n');
            }
          }
          break;
        }
        case SPN_ERR_PUBLISH_PUSH: {
          sp_io_write_str(w->io, event->err.publish.output, SP_NULLPTR);
          break;
        }
        case SPN_ERR_INDEX_SYNC: {
          sp_io_write_str(w->io, event->err.index.cause, SP_NULLPTR);
          break;
        }
        case SPN_ERR_TOOLCHAIN_FETCH: {
          sp_io_write_str(w->io, event->err.artifact.output, SP_NULLPTR);
          break;
        }
        case SPN_ERR_TOOLCHAIN_EXTRACT: {
          write_unpack(w, &event->err.artifact);
          break;
        }
        case SPN_ERR_CERT_UNTRUSTED: {
          const spn_err_cert_t* cert = &event->err.cert;
          sp_tty_fmt(w, "{}\n", sp_fmt_str(sp_str_trim_right(cert->cause)));
          switch (cert->trust.kind) {
            case SPN_NET_TRUST_NONE: {
              sp_tty_fmt(
                w,
                "No CA bundle was found on this machine.\nSet {.yellow} under {.yellow} in {.cyan}, or {.yellow}, to a PEM file of trusted root certificates.\n",
                sp_fmt_cstr("ca_bundle"),
                sp_fmt_cstr("[net]"),
                sp_fmt_str(cert->config),
                sp_fmt_cstr("SSL_CERT_FILE")
              );
              break;
            }
            case SPN_NET_TRUST_SYSTEM: {
              sp_io_write_str(w->io, sp_str_lit("The system certificate store is in use.\n"), SP_NULLPTR);
              break;
            }
            case SPN_NET_TRUST_FOUND: {
              sp_tty_fmt(
                w,
                "The CA bundle in use is {.cyan}.\nTo use another, set {.yellow} under {.yellow} in {.cyan}, or {.yellow}, to a PEM file of trusted root certificates.\n",
                sp_fmt_str(cert->trust.bundle),
                sp_fmt_cstr("ca_bundle"),
                sp_fmt_cstr("[net]"),
                sp_fmt_str(cert->config),
                sp_fmt_cstr("SSL_CERT_FILE")
              );
              break;
            }
            case SPN_NET_TRUST_CONFIG: {
              sp_tty_fmt(
                w,
                cert->trust.missing
                  ? "The CA bundle {.cyan}, named by {.yellow} in {.cyan}, is not a file.\n"
                  : "The CA bundle in use is {.cyan}, named by {.yellow} in {.cyan}.\n",
                sp_fmt_str(cert->trust.bundle),
                sp_fmt_cstr("ca_bundle"),
                sp_fmt_str(cert->config)
              );
              break;
            }
            case SPN_NET_TRUST_ENV: {
              sp_tty_fmt(
                w,
                cert->trust.missing
                  ? "The CA bundle {.cyan}, named by {.yellow}, is not a file.\n{.yellow} overrides {.yellow} in {.cyan}.\n"
                  : "The CA bundle in use is {.cyan}, named by {.yellow}.\n{.yellow} overrides {.yellow} in {.cyan}.\n",
                sp_fmt_str(cert->trust.bundle),
                sp_fmt_cstr("SSL_CERT_FILE"),
                sp_fmt_cstr("SSL_CERT_FILE"),
                sp_fmt_cstr("ca_bundle"),
                sp_fmt_str(cert->config)
              );
              break;
            }
          }
          if (cert->git) {
            sp_tty_fmt(
              w,
              "If git on this machine trusts it, set {.yellow} under {.yellow} in {.cyan} to fetch with git instead.\n",
              sp_fmt_cstr("system_git = true"),
              sp_fmt_cstr("[net]"),
              sp_fmt_str(cert->config)
            );
          }
          break;
        }
        case SPN_ERR_GIT_FETCH: {
          const spn_err_git_fetch_t* fetch = &event->err.git_fetch;
          sp_tty_fmt(w, "{}\n", sp_fmt_str(sp_str_trim_right(fetch->cause)));
          switch (fetch->route) {
            case SPN_GIT_ROUTE_BUILTIN: {
              sp_tty_fmt(
                w,
                "If git on this machine can fetch it, set {.yellow} under {.yellow} in {.cyan} to fetch with git instead.\n",
                sp_fmt_cstr("system_git = true"),
                sp_fmt_cstr("[net]"),
                sp_fmt_str(fetch->config)
              );
              break;
            }
            case SPN_GIT_ROUTE_CONFIGURED: {
              sp_tty_fmt(
                w,
                "git is in use because {.yellow} is set under {.yellow} in {.cyan}.\nTo reproduce, run {.yellow} {.yellow}\n",
                sp_fmt_cstr("system_git"),
                sp_fmt_cstr("[net]"),
                sp_fmt_str(fetch->config),
                sp_fmt_cstr("git ls-remote"),
                sp_fmt_str(fetch->url)
              );
              break;
            }
            case SPN_GIT_ROUTE_SSH: {
              sp_tty_fmt(
                w,
                "git is in use because the URL is SSH.\nTo reproduce, run {.yellow} {.yellow}\n",
                sp_fmt_cstr("git ls-remote"),
                sp_fmt_str(fetch->url)
              );
              break;
            }
            case SPN_GIT_ROUTE_LOGIN: {
              sp_tty_fmt(
                w,
                "git is in use because the server asked for a login.\nTo reproduce, run {.yellow} {.yellow}\n",
                sp_fmt_cstr("git ls-remote"),
                sp_fmt_str(fetch->url)
              );
              break;
            }
          }
          break;
        }
        case SPN_ERR_TOOLCHAIN_SHA: {
          sp_tty_fmt(w, "  expected: {.yellow}\n    actual: {.red}\n", sp_fmt_str(event->err.artifact.expected), sp_fmt_str(event->err.artifact.actual));
          break;
        }
        case SPN_ERR_TOOLCHAIN_TARGET: {
          write_supported_targets(w, event->err.toolchain.targets);
          write_toolchain_candidates(w, &event->err.toolchain);
          break;
        }
        case SPN_ERR_TOOLCHAIN_SYSROOT:
        case SPN_ERR_TOOLCHAIN_SDK_MACOS:
        case SPN_ERR_TOOLCHAIN_SDK_MSVC:
        case SPN_ERR_TOOLCHAIN_UNAVAILABLE_FOR_HOST:
        case SPN_ERR_TOOLCHAIN_NOT_INSTALLED:
        case SPN_ERR_TOOLCHAIN_UNKNOWN:
        case SPN_ERR_TOOLCHAIN_NONE: {
          write_toolchain_candidates(w, &event->err.toolchain);
          break;
        }
        case SPN_ERR_MSVC_TOOLSET:
        case SPN_ERR_MSVC_SDK: {
          sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
          list_t list = { .w = w, .label = sp_str_lit("installed"), .style = sp_fmt_style_green };
          sp_da_for(event->err.msvc.installed, it) {
            spn_err_msvc_install_t install = event->err.msvc.installed[it];
            list_item(&list, sp_str_empty(install.generation) ? install.version : sp_fmt(scratch.mem, "{} ({})", sp_fmt_str(install.version), sp_fmt_str(install.generation)).value);
          }
          list_end(&list);
          sp_mem_end_scratch(scratch);
          break;
        }
        case SPN_ERR_SANITIZER_UNSUPPORTED: {
          list_t list = { .w = w, .label = sp_str_lit("supported sanitizers"), .style = sp_fmt_style_green };
          for (spn_sanitizer_set_t bit = SPN_SANITIZER_ADDRESS; bit <= SPN_SANITIZER_LEAK; bit <<= 1) {
            if (event->err.sanitizer.supported & bit) {
              list_item(&list, spn_sanitizer_to_str((spn_sanitizer_t)bit));
            }
          }
          list_end(&list);
          break;
        }
        case SPN_ERR_TARGET_ABI: {
          write_supported_targets(w, event->err.profile.targets);
          break;
        }
        case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
          spn_err_compiler_t* err = &event->err.compiler;
          sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
          sp_str_t label = sp_fmt(scratch.mem, "toolchains that do, targeting {}", sp_fmt_str(spn_triple_to_str(scratch.mem, err->target))).value;
          list_t list = { .w = w, .label = label, .style = sp_fmt_style_green };
          sp_da_for(err->candidates, it) {
            list_item(&list, err->candidates[it]);
          }
          list_end(&list);
          sp_mem_end_scratch(scratch);
          break;
        }
        case SPN_ERR_TARGET_HOST_DEPS: {
          spn_err_host_deps_t* err = &event->err.host_deps;
          if (err->script) {
            break;
          }
          sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
          sp_str_t label = sp_fmt(scratch.mem, "toolchains that are, targeting {}", sp_fmt_str(spn_triple_to_str(scratch.mem, err->host))).value;
          list_t list = { .w = w, .label = label, .style = sp_fmt_style_green };
          sp_da_for(err->candidates, it) {
            list_item(&list, err->candidates[it]);
          }
          list_end(&list);
          sp_mem_end_scratch(scratch);
          break;
        }
        case SPN_ERR_TARGET_LINKAGE: {
          list_t list = { .w = w, .label = sp_str_lit("supported linkages"), .style = sp_fmt_style_green };
          sp_da_for(event->err.target.supported, it) {
            list_item(&list, spn_linkage_to_str(event->err.target.supported[it]));
          }
          list_end(&list);
          break;
        }
        case SPN_ERR_PROFILE_ARCH:
        case SPN_ERR_PROFILE_ABI: {
          write_supported_targets(w, event->err.profile.targets);
          break;
        }
        case SPN_ERR_TRIPLE_INVALID: {
          spn_err_triple_parse_t* err = &event->err.triple_parse;
          list_t archs = { .w = w, .label = sp_str_lit("supported arches"), .style = sp_fmt_style_green };
          sp_da_for(err->archs, it) {
            list_item(&archs, spn_arch_to_str(err->archs[it]));
          }
          list_end(&archs);
          list_t oses = { .w = w, .label = sp_str_lit("supported oses"), .style = sp_fmt_style_green };
          sp_da_for(err->oses, it) {
            list_item(&oses, spn_os_to_str(err->oses[it]));
          }
          list_end(&oses);
          sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
          sp_str_t label = sp_fmt(scratch.mem, "abis for {}", sp_fmt_str(spn_triple_to_str(scratch.mem, err->parsed))).value;
          list_t abis = { .w = w, .label = label, .style = sp_fmt_style_green };
          sp_da_for(err->abis, it) {
            list_item(&abis, spn_abi_to_str(err->abis[it]));
          }
          list_end(&abis);
          sp_mem_end_scratch(scratch);
          break;
        }
        case SPN_ERR_PROFILE_LINKING: {
          spn_err_profile_t* err = &event->err.profile;
          if (sp_da_size(err->refusals) > 1) {
            sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
            sp_da_for(err->refusals, it) {
              sp_io_write_str(w->io, sp_str_lit("  - "), SP_NULLPTR);
              write_refusal(w, scratch.mem, err->refusals[it].triple, err->refusals[it].reason);
              sp_io_write_c8(w->io, '\n');
            }
            sp_mem_end_scratch(scratch);
          }
          break;
        }
        default: {
          break;
        }
      }
      break;
    }
    default: {
      break;
    }
  }
}

void spn_tui_log_event(spn_tui_t* tui, spn_event_t* event) {
  if (tui->json) {
    spn_event_log_jsonl(tui->out->io, event);
    return;
  }

  if (event->kind == SPN_EVENT_SYNC_PACKAGE && event->sync_pkg.fetched && !sp_str_ht_get(tui->seen_fetch, event->sync_pkg.name)) {
    sp_str_ht_insert(tui->seen_fetch, sp_str_copy(tui->mem, event->sync_pkg.name), true);
    tui->num_downloads++;
  }

  const spn_event_info_t* info = &spn_event_info[event->kind];
  if (info->severity < SPN_EVENT_SEVERITY_ERROR && info->verbosity > tui->verbosity) {
    if (event->kind == SPN_EVENT_USER_LOG) {
      sp_da_push(tui->buffered_logs, ((spn_tui_buffered_log_t) {
        .pkg = sp_str_copy(tui->mem, event->pkg),
        .message = sp_str_copy(tui->mem, event->user_log.message),
      }));
    }
    return;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;
  sp_tty_t* tty = &tui->journal;
  sp_str_t verb = sp_cstr_as_str(info->verb);

  if (info->severity == SPN_EVENT_SEVERITY_WARNING) {
    write_event(tty, verb, sp_fmt_style_yellow, event_subject(event), render_event_detail(tui, mem, event));
    render_event_extra(tty, event);
    flush_writer(&tui->writer);
    sp_mem_end_scratch(scratch);
    return;
  }

  if (info->severity >= SPN_EVENT_SEVERITY_ERROR) {
    sp_da_for(tui->buffered_logs, it) {
      spn_tui_buffered_log_t* log = &tui->buffered_logs[it];
      write_event(tty, sp_str_lit(""), sp_fmt_style_green, log->pkg, log->message);
    }
    sp_da_clear(tui->buffered_logs);

    sp_str_t detail = render_event_detail(tui, mem, event);
    if (info->severity == SPN_EVENT_SEVERITY_FATAL) {
      sp_str_t subject = event_subject(event);
      if (!sp_str_empty(subject)) {
        write_event(tty, sp_str_lit("Failed"), sp_fmt_style_red, subject, sp_str_lit(""));
        sp_io_write_c8(tty->io, '\n');
      }
      write_error(tty, detail);
    } else {
      write_event(tty, verb, sp_fmt_style_red, event_subject(event), detail);
    }
    render_event_extra(tty, event);
    flush_writer(&tui->writer);
    sp_mem_end_scratch(scratch);
    return;
  }

  switch (event->kind) {
    case SPN_EVENT_SYNC: {
      if (!sp_str_ht_get(tui->seen_url, event->sync.url)) {
        sp_str_ht_insert(tui->seen_url, sp_str_copy(tui->mem, event->sync.url), true);
        write_event(
          tty, verb, sp_fmt_style_green,
          event->sync.name,
          colored_fmt(tty->color, mem, "{.gray}", sp_fmt_str(event->sync.url))
        );
      }
      break;
    }

    case SPN_EVENT_SYNC_END: {
      if (!tui->num_downloads) {
        break;
      }

      c8 buffer [64] = sp_zero;
      sp_fmt_write_duration_buf(buffer, sizeof(buffer), event->sync_end.time);
      write_event(
        tty, verb, sp_fmt_style_green,
        sp_str_lit(""),
        colored_fmt(tty->color, mem, "{} {} in {.gray}",
          sp_fmt_uint(tui->num_downloads),
          sp_fmt_cstr(tui->num_downloads == 1 ? "package" : "packages"),
          sp_fmt_cstr(buffer)
        )
      );
      break;
    }

    case SPN_EVENT_RESOLVE_END:
    case SPN_EVENT_BUILD_PASSED:
    case SPN_EVENT_TEST_SUMMARY: {
      write_event(tty, verb, sp_fmt_style_green, sp_str_lit(""), render_event_detail(tui, mem, event));
      break;
    }

    case SPN_EVENT_TEST_PASSED: {
      write_event(tty, verb, sp_fmt_style_green, event->test_passed.name, render_event_detail(tui, mem, event));
      break;
    }

    case SPN_EVENT_WARM_START: {
      write_event(tty, verb, sp_fmt_style_green, sp_str_lit("libc"), render_event_detail(tui, mem, event));
      break;
    }

    case SPN_EVENT_TARGET_RUN: {
      sp_str_t name = event->target_run.name;
      if (sp_str_empty(name)) {
        name = event->pkg;
      }
      write_event(tty, verb, sp_fmt_style_green, name, render_event_detail(tui, mem, event));
      break;
    }

    case SPN_EVENT_USER_LOG: {
      sp_str_t name = event->pkg;
      write_event(tty, sp_str_lit(""), sp_fmt_style_green, name, event->user_log.message);
      break;
    }

    default: {
      write_event(tty, verb, sp_fmt_style_green, event_subject(event), render_event_detail(tui, mem, event));
      break;
    }
  }

  flush_writer(&tui->writer);
  sp_mem_end_scratch(scratch);
}

void spn_tui_error_v(spn_tui_t* tui, const c8* fmt, va_list args) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(scratch.mem, &buf);
  sp_tty_t tty = { .io = &buf.base, .color = tui->journal.color };
  sp_tty_fmt_v(&tty, sp_cstr_as_str(fmt), args);

  write_error(&tui->journal, sp_io_dyn_mem_writer_as_str(&buf));
  flush_writer(&tui->writer);
  sp_mem_end_scratch(scratch);
}

void spn_tui_error(spn_tui_t* tui, const c8* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  spn_tui_error_v(tui, fmt, args);
  va_end(args);
}

void spn_tui_usage(spn_tui_t* tui, sp_cli_err_t err) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(scratch.mem, &buf);
  sp_tty_t tty = { .io = &buf.base, .color = tui->journal.color };
  sp_cli_err_print(&tty, err);

  spn_tui_error(tui, "{}", sp_fmt_str(sp_io_dyn_mem_writer_as_str(&buf)));
  sp_mem_end_scratch(scratch);
}

static void write_line(spn_tui_line_writer_t* writer, sp_str_t line) {
  if (writer->prompt) {
    sp_prompt_log_str(writer->prompt, line);
  } else {
    sp_io_write_str(writer->downstream, line, SP_NULLPTR);
    sp_io_write_c8(writer->downstream, '\n');
  }
}

static void complete_line(spn_tui_line_writer_t* writer) {
  sp_str_t line = sp_str_trim_right(sp_str(writer->partial, sp_da_size(writer->partial)));

  if (sp_str_empty(line)) {
    writer->deferred_blanks++;
  } else {
    sp_for(it, writer->deferred_blanks) {
      write_line(writer, sp_str_lit(""));
    }
    writer->deferred_blanks = 0;
    write_line(writer, line);
  }

  sp_da_clear(writer->partial);
}

static sp_err_t on_write(sp_io_writer_t* w, const void* ptr, u64 size, u64* bytes_written) {
  spn_tui_line_writer_t* writer = (spn_tui_line_writer_t*)w;
  const c8* bytes = (const c8*)ptr;

  sp_for(it, size) {
    c8 c = bytes[it];
    if (c == '\n') {
      complete_line(writer);
    } else {
      sp_da_push(writer->partial, c);
    }
  }

  if (bytes_written) {
    *bytes_written = size;
  }
  return SP_OK;
}

static void flush_writer(spn_tui_line_writer_t* writer) {
  if (!sp_da_empty(writer->partial)) {
    complete_line(writer);
  }
  writer->deferred_blanks = 0;
}

static void attach_prompt(spn_tui_t* tui, sp_prompt_ctx_t* ctx) {
  flush_writer(&tui->writer);
  tui->writer.prompt = ctx;
}

static void detach_prompt(spn_tui_t* tui) {
  tui->writer.prompt = SP_NULLPTR;
  flush_writer(&tui->writer);
}

void spn_tui_init(spn_tui_t* tui, spn_tui_desc_t desc) {
  tui->ctx = desc.ctx;
  tui->json = desc.json;
  tui->verbosity = desc.verbosity;
  tui->wake = desc.wake;

  tui->mem = sp_mem_arena_as_allocator(sp_mem_arena_new(sp_mem_os_new()));

  tui->out = sp_tty_std_out();
  tui->err = sp_tty_std_err();
  if (desc.json) {
    sp_io_writer_set_buffer(tui->out->io, tui->jsonl, sizeof(tui->jsonl));
  }
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_env_t env = sp_env_capture(scratch.mem);
  bool dumb = sp_str_equal(sp_env_get_c(&env, "TERM"), sp_str_lit("dumb"));
  sp_mem_end_scratch(scratch);
  if (desc.no_color || dumb) {
    tui->out->color = SP_TTY_COLOR_NONE;
    tui->err->color = SP_TTY_COLOR_NONE;
  }
  tui->interactive = !desc.json && !dumb && sp_sys_is_tty(sp_sys_stderr) && sp_sys_is_tty(sp_sys_stdin);
#ifdef SP_WIN32
  if (sp_sys_is_tty(sp_sys_stdout) || sp_sys_is_tty(sp_sys_stderr)) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
  }
#endif

  tui->writer = (spn_tui_line_writer_t) {
    .base.write = on_write,
    .downstream = tui->err->io,
  };
  tui->journal = (sp_tty_t) {
    .io = &tui->writer.base,
    .color = tui->err->color,
  };
  sp_da_init(tui->mem, tui->writer.partial);
  sp_str_ht_init(tui->mem, tui->seen_url);
  sp_str_ht_init(tui->mem, tui->seen_fetch);
  sp_da_init(tui->mem, tui->buffered_logs);
  sp_ht_init(tui->mem, tui->thread_ids);
}

static u32 get_short_tid(spn_tui_t* tui, u64 thread_id) {
  static u32 next_thread_id = 0;
  if (!sp_ht_key_exists(tui->thread_ids, thread_id)) {
    sp_ht_insert(tui->thread_ids, thread_id, next_thread_id++);
  }
  return *sp_ht_getp(tui->thread_ids, thread_id);
}

static void emit_event(spn_tui_t* tui, spn_event_t* event) {
  event->thread_id = get_short_tid(tui, event->thread_id);
  spn_tui_log_event(tui, event);
}

void spn_tui_flush(spn_tui_t* tui) {
  if (!tui->ctx) {
    return;
  }

  while (true) {
    spn_event_t* event = spn_ctx_drain(tui->ctx);
    if (!event) {
      break;
    }

    switch (event->kind) {
      case SPN_EVENT_BUILD_PASSED: {
        spn_prompt_stop(tui, SP_PROMPT_STATE_SUBMIT);
        break;
      }
      case SPN_EVENT_BUILD_FAILED: {
        spn_prompt_stop(tui, SP_PROMPT_STATE_ERROR);
        break;
      }
      default: {
        break;
      }
    }

    emit_event(tui, event);
  }
  sp_io_flush(tui->out->io);
}

static void print_line(sp_tty_t* tty, const c8* fmt, va_list args) {
  sp_tty_fmt_v(tty, sp_cstr_as_str(fmt), args);
  sp_io_write_new_line(tty->io);
}

static sp_tty_t* print_tty(spn_tui_t* tui) {
  if (tui->json) {
    return tui->err;
  }
  return tui->out;
}

void spn_print(spn_tui_t* tui, const c8* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  print_line(print_tty(tui), fmt, args);
  va_end(args);
}

void spn_print_err(spn_tui_t* tui, const c8* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  print_line(tui->err, fmt, args);
  va_end(args);
}

static void prompt_start(spn_tui_t* tui) {
  if (tui->prompt.started) {
    return;
  }
  tui->prompt.started = true;

  if (!tui->interactive) {
    return;
  }

  tui->prompt.ctx = sp_prompt_begin_ex(tui->mem, (sp_prompt_config_t) { .passive = true });
  if (!tui->prompt.ctx) {
    return;
  }
  sp_prompt_use_wake(tui->prompt.ctx, tui->wake);

  sp_prompt_widget_t widget = sp_prompt_progress_widget(tui->prompt.ctx, (sp_prompt_progress_t) {
    .prompt = "Building",
    .color = { .rgb = { .r = 99, .g = 160, .b = 136 } },
  });
  tui->prompt.app = sp_app_new(tui->mem, sp_prompt_app(tui->prompt.ctx, widget));
  tui->prompt.last = sp_zero_s(spn_progress_t);
  tui->prompt.on = true;

  attach_prompt(tui, tui->prompt.ctx);
}

void spn_prompt_stop(spn_tui_t* tui, sp_prompt_state_t state) {
  if (!tui->prompt.on) {
    return;
  }

  sp_prompt_set_state(tui->prompt.ctx, state);
  sp_app_tick(tui->prompt.app);
  sp_prompt_end(tui->prompt.ctx);
  tui->prompt.on = false;
  detach_prompt(tui);
}

static void prompt_pump(spn_tui_t* tui, bool building, spn_progress_t progress) {
  if (!tui->prompt.on) {
    if (!building || (!progress.misses && !progress.warm)) return;
    prompt_start(tui);
    if (!tui->prompt.on) return;
  }

  // @spader wtf?
  if (building && (progress.completed != tui->prompt.last.completed || progress.total != tui->prompt.last.total || progress.warm != tui->prompt.last.warm)) {
    tui->prompt.last = progress;

    sp_mem_arena_marker_t s = sp_mem_begin_scratch();
    sp_str_t status = progress.warm
      ? sp_fmt(s.mem, "{}/{} units, libc {}", sp_fmt_uint(progress.completed), sp_fmt_uint(progress.total), sp_fmt_uint(progress.warm)).value
      : sp_fmt(s.mem, "{}/{} units", sp_fmt_uint(progress.completed), sp_fmt_uint(progress.total)).value;
    sp_prompt_send_status_str(tui->prompt.ctx, status);
    sp_mem_end_scratch(s);

    f32 value = progress.total ? (f32)progress.completed / (f32)progress.total : 0.f;
    sp_prompt_send_progress_f32(tui->prompt.ctx, value);
  }

  sp_app_tick(tui->prompt.app);
}

void spn_tui_poll(spn_tui_t* tui) {
  if (tui->handoff.granted) {
    return;
  }

  spn_tui_flush(tui);
  spn_progress_t progress = sp_zero;
  bool building = spn_ctx_progress(tui->ctx, &progress);
  prompt_pump(tui, building, progress);
}

bool spn_tui_wants_input(spn_tui_t* tui) {
  return tui->prompt.on;
}

void spn_tui_op_done(spn_tui_t* tui, spn_op_t* op) {
  if (tui->handoff.granted) {
    return;
  }

  spn_tui_flush(tui);
  spn_prompt_stop(tui, spn_op_result(op).err ? SP_PROMPT_STATE_ERROR : SP_PROMPT_STATE_SUBMIT);
}

void spn_tui_handoff(spn_tui_t* tui) {
  spn_prompt_stop(tui, SP_PROMPT_STATE_SUBMIT);
  spn_tui_flush(tui);
  tui->handoff.granted = true;
}
