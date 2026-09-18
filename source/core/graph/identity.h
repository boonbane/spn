#ifndef SPN_GRAPH_IDENTITY_H
#define SPN_GRAPH_IDENTITY_H

#include "dag/types.h"
#include "pkg/types.h"
#include "unit/types.h"

typedef struct {
  spn_pkg_root_kind_t kind;
  sp_str_t rev;
  sp_str_t dir;
  sp_hash_t patches;
} spn_build_source_pin_t;

spn_build_source_pin_t spn_build_source_pin(spn_pkg_unit_t* unit);
spn_dag_digest_t       spn_build_tree_identity(spn_pkg_unit_t* unit, const spn_build_source_pin_t* pin);
spn_dag_digest_t       hash_user_node(spn_user_node_t* node, const spn_build_source_pin_t* pin);
spn_dag_digest_t       hash_compile_unit(const spn_compile_unit_t* unit);
spn_dag_digest_t       hash_compile_commands(const spn_path_roots_t* roots, spn_pkg_unit_t* unit, sp_da(spn_compile_unit_t*) objects);
spn_dag_digest_t       hash_link(sp_hash_t toolchain, const spn_invocation_t* invocation);
spn_dag_digest_t       hash_exports(sp_hash_t toolchain, spn_cc_exports_format_t format, sp_str_t name);
spn_dag_digest_t       hash_rsp(const spn_path_roots_t* roots, spn_rsp_style_t style, sp_da(spn_arg_t) args);

#endif
