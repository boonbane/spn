#ifndef spn_compiler_rsp_h
#define spn_compiler_rsp_h

#include "compiler/types.h"
#include "paths/types.h"

spn_rsp_style_t spn_rsp_style(spn_cc_driver_t driver);
void            spn_rsp_render(sp_io_writer_t* io, const spn_path_roots_t* roots, spn_rsp_style_t style, sp_da(spn_arg_t) args);

#endif
