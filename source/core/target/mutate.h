#ifndef SPN_TARGET_MUTATE_H
#define SPN_TARGET_MUTATE_H

#include "core/types.h"
#include "target/types.h"

void spn_target_add_embed(sp_mem_t mem, si_target_t* target, spn_embed_t embed);
bool spn_linkage_set_has(spn_linkage_set_t set, spn_linkage_t kind);

#endif
