#ifndef SPN_TOOLCHAIN_LIBC_H
#define SPN_TOOLCHAIN_LIBC_H

#include "sp.h"
#include "paths/types.h"
#include "toolchain/types.h"

void spn_libc_render(sp_io_writer_t* io, const spn_path_roots_t* roots, const spn_libc_t* libc);

#endif
