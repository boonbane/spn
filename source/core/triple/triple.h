#ifndef SPN_TRIPLE_TRIPLE_H
#define SPN_TRIPLE_TRIPLE_H

#include "sp.h"
#include "spn/core.h"

typedef enum {
  SPN_TRIPLE_ENTRY_OK,
  SPN_TRIPLE_ENTRY_MISSING_ARCH,
  SPN_TRIPLE_ENTRY_MISSING_OS,
  SPN_TRIPLE_ENTRY_MISSING_ABI,
  SPN_TRIPLE_ENTRY_FOREIGN_ARCH,
  SPN_TRIPLE_ENTRY_FOREIGN_ABI,
} spn_triple_entry_t;

spn_arch_t         spn_arch_from_str(sp_str_t str);
sp_str_t           spn_arch_to_str(spn_arch_t arch);
spn_os_t           spn_os_from_str(sp_str_t str);
sp_str_t           spn_os_to_str(spn_os_t os);
spn_abi_t          spn_abi_from_str(sp_str_t str);
sp_str_t           spn_abi_to_str(spn_abi_t abi);
u32                spn_os_archs(spn_os_t os, const spn_arch_t** archs);
u32                spn_os_abis(spn_os_t os, const spn_abi_t** abis);
u32                spn_os_completions(spn_os_t os, const spn_abi_t** abis);
spn_obj_format_t   spn_os_to_native_object_format(spn_os_t os);
spn_err_t          spn_triple_parse(sp_str_t str, spn_triple_t* triple);
sp_str_t           spn_triple_to_str(sp_mem_t mem, spn_triple_t triple);
bool               spn_triple_equal(spn_triple_t a, spn_triple_t b);
spn_triple_t       spn_triple_merge(spn_triple_t base, spn_triple_t partial);
spn_triple_entry_t spn_triple_entry(spn_triple_t partial, spn_triple_t* full);

#endif
