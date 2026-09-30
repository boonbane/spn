#include "spn.h"

#include <stdio.h>

static s32 read_value(const c8* path) {
  FILE* file = fopen(path, "r");
  if (!file) {
    return -1;
  }
  s32 value = -1;
  if (fscanf(file, "#define %*s %d", &value) != 1) {
    value = -1;
  }
  fclose(file);
  return value;
}

static void write_value(const c8* path, const c8* name, s32 value) {
  c8 line [64];
  snprintf(line, sizeof(line), "#define %s %d\n", name, value);
  spn_io_write(path, line);
}

SPN_EXPORT
s32 gen_a(spn_t* spn) {
  write_value("/work/A.h", "A", 1);
  return 0;
}

SPN_EXPORT
s32 gen_l(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  write_value("/work/L.h", "L", a * 2);
  return 0;
}

SPN_EXPORT
s32 gen_r(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  write_value("/work/R.h", "R", a * 3);
  return 0;
}

SPN_EXPORT
s32 gen_f(spn_t* spn) {
  s32 l = read_value("/work/L.h");
  s32 r = read_value("/work/R.h");
  if (l < 0 || r < 0) {
    return 1;
  }
  write_value("/work/F.h", "F", l + r);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  const c8* a_h = spn_get_subdir(spn, SPN_DIR_WORK, "A.h");
  const c8* l_h = spn_get_subdir(spn, SPN_DIR_WORK, "L.h");
  const c8* r_h = spn_get_subdir(spn, SPN_DIR_WORK, "R.h");

  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "gen_a");
  spn_node_add_output(a, SPN_DIR_WORK, "A.h");

  spn_node_t* l = spn_add_node(config, "L");
  spn_node_set_fn(l, "gen_l");
  spn_node_add_input(l, a_h);
  spn_node_add_output(l, SPN_DIR_WORK, "L.h");

  spn_node_t* r = spn_add_node(config, "R");
  spn_node_set_fn(r, "gen_r");
  spn_node_add_input(r, a_h);
  spn_node_add_output(r, SPN_DIR_WORK, "R.h");

  spn_node_t* f = spn_add_node(config, "F");
  spn_node_set_fn(f, "gen_f");
  spn_node_add_input(f, l_h);
  spn_node_add_input(f, r_h);
  spn_node_add_output(f, SPN_DIR_WORK, "F.h");
  return SPN_OK;
}
