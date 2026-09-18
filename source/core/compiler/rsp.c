#include "compiler/rsp.h"

#include "io/io.h"
#include "paths/paths.h"

static bool needs_quotes(spn_rsp_style_t style, sp_str_t arg) {
  if (sp_str_empty(arg) || arg.data[0] == '#') {
    return true;
  }
  sp_for(it, arg.len) {
    c8 c = arg.data[it];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'') {
      return true;
    }
    if (c == '\\' && style == SPN_RSP_STYLE_GNU) {
      return true;
    }
  }
  return false;
}

static void write_quoted_windows(sp_io_writer_t* io, sp_str_t arg) {
  sp_io_write_c8(io, '"');
  u32 backslashes = 0;
  sp_for(it, arg.len) {
    c8 c = arg.data[it];
    if (c == '\\') {
      backslashes++;
      continue;
    }
    if (c == '"') {
      backslashes = backslashes * 2 + 1;
    }
    sp_for(jt, backslashes) {
      sp_io_write_c8(io, '\\');
    }
    backslashes = 0;
    sp_io_write_c8(io, c);
  }
  sp_for(jt, backslashes * 2) {
    sp_io_write_c8(io, '\\');
  }
  sp_io_write_c8(io, '"');
}

static void write_quoted_gnu(sp_io_writer_t* io, sp_str_t arg) {
  sp_io_write_c8(io, '"');
  sp_for(it, arg.len) {
    c8 c = arg.data[it];
    if (c == '\\' || c == '"') {
      sp_io_write_c8(io, '\\');
    }
    sp_io_write_c8(io, c);
  }
  sp_io_write_c8(io, '"');
}

static void write_arg(sp_io_writer_t* io, spn_rsp_style_t style, sp_str_t arg) {
  if (!needs_quotes(style, arg)) {
    sp_io_write_str(io, arg, SP_NULLPTR);
    return;
  }
  switch (style) {
    case SPN_RSP_STYLE_WINDOWS: {
      write_quoted_windows(io, arg);
      break;
    }
    case SPN_RSP_STYLE_GNU: {
      write_quoted_gnu(io, arg);
      break;
    }
  }
}

spn_rsp_style_t spn_rsp_style(spn_cc_driver_t driver) {
  switch (driver) {
    case SPN_CC_DRIVER_GCC: {
      return SPN_RSP_STYLE_GNU;
    }
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG:
    case SPN_CC_DRIVER_MSVC:
    case SPN_CC_DRIVER_NONE: {
      return SPN_RSP_STYLE_WINDOWS;
    }
  }
  sp_unreachable_return(SPN_RSP_STYLE_WINDOWS);
}

void spn_rsp_render(sp_io_writer_t* io, const spn_path_roots_t* roots, spn_rsp_style_t style, sp_da(spn_arg_t) args) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_da_for(args, it) {
    write_arg(io, style, spn_arg_str(roots, s.mem, args[it]));
    sp_io_write_new_line(io);
  }
  sp_mem_end_scratch(s);
}
