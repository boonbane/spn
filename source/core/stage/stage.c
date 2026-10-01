#include "stage/stage.h"

static void settle(spn_stage_it_t* it) {
  for (; sp_str_line_it_valid(&it->line); sp_str_line_it_next(&it->line)) {
    sp_str_t line = it->line.line;
    s32 tab = sp_str_find_c8(line, '\t');
    if (tab == SP_STR_NO_MATCH) {
      continue;
    }
    it->record = (spn_stage_record_t) {
      .owner = sp_str_prefix(line, tab),
      .path = sp_str_suffix(line, line.len - tab - 1),
    };
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

void spn_stage_write(sp_io_writer_t* w, sp_str_t owner, sp_str_t path) {
  sp_fmt_io(w, "{}\t{}\n", sp_fmt_str(owner), sp_fmt_str(path));
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
