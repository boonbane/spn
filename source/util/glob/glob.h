#ifndef SPN_SP_GLOB_H
#define SPN_SP_GLOB_H

#include "sp.h"

typedef struct {
  sp_str_t dir;
  sp_str_t rest;
  sp_str_t name;
  bool literal;
  bool deep;
} sp_glob_meta_t;

sp_glob_meta_t sp_glob_parse_meta(sp_str_t pattern);

#endif
