#ifndef SPN_TARGET_MUTATE_H
#define SPN_TARGET_MUTATE_H

#include "core/types.h"
#include "target/types.h"

void spn_target_add_embed(sp_mem_t mem, spn_target_info_t* target, spn_embed_t embed);
void spn_linkage_set_add(spn_linkage_set_t* set, spn_linkage_t kind);
bool spn_linkage_set_has(spn_linkage_set_t set, spn_linkage_t kind);
spn_linkage_t spn_linkage_set_default(spn_linkage_set_t set);

#endif
