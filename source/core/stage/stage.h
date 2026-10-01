#ifndef SPN_STAGE_H
#define SPN_STAGE_H

#include "sp.h"
#include "stage/types.h"

bool           spn_stage_it_valid(const spn_stage_it_t* it);
void           spn_stage_it_next(spn_stage_it_t* it);
spn_stage_it_t spn_stage_it_begin(sp_str_t manifest);
void           spn_stage_write(sp_io_writer_t* w, sp_str_t owner, sp_str_t path);

#define spn_stage_for(manifest, it) \
  for (spn_stage_it_t it = spn_stage_it_begin((manifest)); spn_stage_it_valid(&(it)); spn_stage_it_next(&(it)))

#endif
