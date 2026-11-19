#ifndef SPN_COMPILER_EXPORTS_H
#define SPN_COMPILER_EXPORTS_H

#include "sp.h"
#include "spn/core.h"
#include "array/array.h"
#include "compiler/types.h"

void spn_exports_render_version_script(sp_io_writer_t* io, si_da(spn_cc_export_t) symbols);
void spn_exports_render_symbol_list(sp_io_writer_t* io, si_da(spn_cc_export_t) symbols);
void spn_exports_render_def(sp_io_writer_t* io, sp_str_t library, si_da(spn_cc_export_t) symbols);

#endif
