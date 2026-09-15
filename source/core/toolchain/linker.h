#ifndef SPN_TOOLCHAIN_LINKER_H
#define SPN_TOOLCHAIN_LINKER_H

#include "toolchain/types.h"

spn_ld_dialect_t      spn_ld_dialect(spn_triple_t target);
bool                  spn_ld_loader(spn_triple_t target, spn_linking_t linking);
spn_linking_refusal_t spn_ld_linking(spn_triple_t target, spn_linking_t request, spn_linking_t* linking);
bool                  spn_ld_scripts(spn_ld_family_t family, spn_format_t format);
bool                  spn_ld_accepts(spn_cc_driver_t driver, spn_ld_family_t declared);
spn_ld_family_t       spn_ld_native(spn_cc_driver_t driver, spn_triple_t target);

#endif
