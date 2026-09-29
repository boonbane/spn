#ifndef SPN_STAGE_H
#define SPN_STAGE_H

#include "sp.h"
#include "stage/types.h"

spn_stage_plan_t spn_stage_plan(sp_mem_t mem, sp_str_t manifest, sp_da(spn_stage_record_t) live);
void             spn_stage_render(sp_io_writer_t* w, sp_da(spn_stage_record_t) records);

#endif
