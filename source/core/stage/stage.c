#include "stage/stage.h"
#include "str/str.h"

static bool parse(sp_str_t line, spn_stage_record_t* record) {
  s32 tab = sp_str_find_c8(line, '\t');
  if (tab == SP_STR_NO_MATCH) {
    return false;
  }
  *record = (spn_stage_record_t) {
    .owner = sp_str_prefix(line, tab),
    .path = sp_str_suffix(line, line.len - tab - 1),
  };
  return true;
}

spn_stage_plan_t spn_stage_plan(sp_mem_t mem, sp_str_t manifest, sp_da(spn_stage_record_t) live) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_str_ht(bool) owners = SP_NULLPTR;
  sp_str_ht_init(s.mem, owners);
  sp_str_ht(bool) paths = SP_NULLPTR;
  sp_str_ht_init(s.mem, paths);

  spn_stage_plan_t plan = sp_zero;
  sp_da_init(mem, plan.records);
  sp_da_init(mem, plan.dropped);

  sp_da_for(live, it) {
    sp_str_ht_insert(owners, live[it].owner, true);
    sp_str_ht_insert(paths, live[it].path, true);
    sp_da_push(plan.records, live[it]);
  }
  sp_str_for_line(manifest, it) {
    spn_stage_record_t record = sp_zero;
    if (!parse(it.line, &record) || sp_str_ht_get(owners, record.owner)) {
      continue;
    }
    sp_str_ht_insert(paths, record.path, true);
    sp_da_push(plan.records, record);
  }
  sp_str_for_line(manifest, it) {
    spn_stage_record_t record = sp_zero;
    if (!parse(it.line, &record) || sp_str_ht_get(paths, record.path)) {
      continue;
    }
    sp_da_push(plan.dropped, record.path);
  }

  sp_mem_end_scratch(s);
  return plan;
}

void spn_stage_render(sp_io_writer_t* w, sp_da(spn_stage_record_t) records) {
  sp_da_for(records, it) {
    sp_fmt_io(w, "{}\t{}\n", sp_fmt_str(records[it].owner), sp_fmt_str(records[it].path));
  }
}
