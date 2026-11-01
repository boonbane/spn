#include "target/mutate.h"

#include "intern/intern.h"

bool spn_linkage_set_has(spn_linkage_set_t set, spn_linkage_t kind) {
  switch (kind) {
    case SPN_LIB_KIND_SOURCE: {
      return set.source;
    }
    case SPN_LIB_KIND_SHARED: {
      return set.shared;
    }
    case SPN_LIB_KIND_STATIC: {
      return set.static_lib;
    }
    case SPN_LIB_KIND_OBJECT: {
      return set.object;
    }
    case SPN_LIB_KIND_NONE: sp_unreachable_case();
  }

  SP_UNREACHABLE_RETURN(false);
}

void spn_target_add_embed(sp_mem_t mem, si_target_t* target, spn_embed_t embed) {
  embed.dest = spn_intern(embed.dest).str;
  embed.types.data = spn_intern(embed.types.data).str;
  embed.types.size = spn_intern(embed.types.size).str;
  si_da_push(mem, target->embed, embed);
}
