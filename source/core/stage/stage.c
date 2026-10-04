#include "stage/stage.h"

static const c8* declarers [] = {
  [SPN_STAGE_DECLARER_NONE] = "",
  [SPN_STAGE_DECLARER_STAGE] = "stage",
  [SPN_STAGE_DECLARER_EXE] = "exe",
  [SPN_STAGE_DECLARER_COMPILE_COMMANDS] = "compile_commands",
};

static void settle(spn_stage_it_t* it) {
  for (; sp_str_line_it_valid(&it->line); sp_str_line_it_next(&it->line)) {
    sp_str_pair_t declarer = sp_str_cleave_c8(it->line.line, '\t');
    sp_str_pair_t owner = sp_str_cleave_c8(declarer.second, '\t');
    spn_stage_record_t record = {
      .owner = owner.first,
      .path = owner.second,
    };
    sp_carr_for(declarers, dt) {
      if (sp_str_equal_cstr(declarer.first, declarers[dt])) {
        record.declarer = (spn_stage_declarer_t)dt;
      }
    }
    if (record.declarer == SPN_STAGE_DECLARER_NONE || sp_str_empty(record.path)) {
      continue;
    }
    it->record = record;
    return;
  }
}

bool spn_stage_it_valid(const spn_stage_it_t* it) {
  return sp_str_line_it_valid(&it->line);
}

void spn_stage_it_next(spn_stage_it_t* it) {
  sp_str_line_it_next(&it->line);
  settle(it);
}

spn_stage_it_t spn_stage_it_begin(sp_str_t manifest) {
  spn_stage_it_t it = { .line = sp_str_line_it_begin(manifest) };
  settle(&it);
  return it;
}

void spn_stage_write(sp_io_writer_t* w, spn_stage_record_t record) {
  sp_fmt_io(w, "{}\t{}\t{}\n", sp_fmt_cstr(declarers[record.declarer]), sp_fmt_str(record.owner), sp_fmt_str(record.path));
}

sp_err_t spn_stage_remove(sp_path_t file) {
  sp_err_t err = sp_fs_remove_file_at(file);
  if (err && err != SP_ERR_SYS_NOT_FOUND) {
    return err;
  }
  sp_str_t dir = file.sub;
  for (s32 sep = sp_str_find_c8_reverse(dir, '/'); sep > 0; sep = sp_str_find_c8_reverse(dir, '/')) {
    dir = sp_str_prefix(dir, sep);
    if (sp_sys_rmdir_s(file.dir, dir)) {
      break;
    }
  }
  return SP_OK;
}
