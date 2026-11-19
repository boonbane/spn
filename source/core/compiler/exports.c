#include "compiler/exports.h"

#include "io/io.h"
#include "macro/macro.h"

void spn_exports_render_version_script(sp_io_writer_t* io, si_da(spn_cc_export_t) symbols) {
  sp_io_write_cstr(io, "{", SP_NULLPTR);
  sp_io_write_new_line(io);
  if (!si_da_empty(symbols)) {
    sp_io_write_cstr(io, "global:", SP_NULLPTR);
    sp_io_write_new_line(io);
    si_da_for(symbols, it) {
      sp_fmt_io(io, "  {};", sp_fmt_str(symbols[it].name));
      sp_io_write_new_line(io);
    }
  }
  sp_io_write_cstr(io, "local:", SP_NULLPTR);
  sp_io_write_new_line(io);
  sp_io_write_cstr(io, "  *;", SP_NULLPTR);
  sp_io_write_new_line(io);
  sp_io_write_cstr(io, "};", SP_NULLPTR);
  sp_io_write_new_line(io);
}

void spn_exports_render_symbol_list(sp_io_writer_t* io, si_da(spn_cc_export_t) symbols) {
  si_da_for(symbols, it) {
    sp_io_write_str(io, symbols[it].name, SP_NULLPTR);
    sp_io_write_new_line(io);
  }
}

void spn_exports_render_def(sp_io_writer_t* io, sp_str_t library, si_da(spn_cc_export_t) symbols) {
  sp_fmt_io(io, "LIBRARY {}", sp_fmt_str(library));
  sp_io_write_new_line(io);
  sp_io_write_cstr(io, "EXPORTS", SP_NULLPTR);
  sp_io_write_new_line(io);
  si_da_for(symbols, it) {
    sp_str_t name = symbols[it].name;
    // Every module that needs a deleting destructor emits its own, and link
    // warns (LNK4102) when one is exported
    if (sp_str_starts_with(name, sp_str_lit("??_G")) || sp_str_starts_with(name, sp_str_lit("??_E"))) {
      continue;
    }
    sp_fmt_io(io, symbols[it].data ? "  {} DATA" : "  {}", sp_fmt_str(name));
    sp_io_write_new_line(io);
  }
}
