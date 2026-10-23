#ifndef SPN_OPTIONS_TYPES_H
#define SPN_OPTIONS_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "array/array.h"
#include "intern/types.h"
#include "when/types.h"

typedef struct {
  sp_intern_str_t name;
  bool is_root;
  spn_option_map_t options;
} spn_option_subject_t;

typedef struct {
  sp_intern_str_t consumer;
  const spn_when_t* options;
} spn_option_request_t;

typedef sp_da(spn_option_request_t) spn_option_requests_t;
typedef sp_ht(sp_intern_id_t, spn_option_requests_t) spn_option_seeds_t;

typedef struct {
  spn_option_setter_kind_t kind;
  sp_intern_str_t name;
} spn_option_setter_t;

typedef struct {
  sp_intern_str_t name;
  spn_option_value_t value;
  spn_option_setter_t setter;
  bool is_default;
} spn_resolved_option_t;

typedef sp_da(spn_resolved_option_t) spn_resolved_options_t;

typedef enum {
  SPN_OPTION_ERR_UNDECLARED,
  SPN_OPTION_ERR_BAD_VALUE,
  SPN_OPTION_ERR_CONFLICT,
  SPN_OPTION_ERR_VETO,
  SPN_OPTION_ERR_NO_VALUE,
  SPN_OPTION_ERR_LATE_GATE,
  SPN_OPTION_ERR_UNKNOWN_PKG,
} spn_option_err_t;

typedef struct {
  spn_option_err_t kind;
  sp_intern_str_t pkg;
  sp_str_t option;
  spn_option_value_t value;
  spn_option_setter_t a;
  spn_option_setter_t b;
} spn_option_violation_t;

typedef sp_da(spn_option_violation_t) spn_option_violations_t;

typedef struct {
  spn_resolved_options_t options;
  spn_option_violations_t violations;
} spn_merged_options_t;

#endif
