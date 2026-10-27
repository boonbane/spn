#ifndef SPN_GRAPH_NODES_H
#define SPN_GRAPH_NODES_H

#include "compiler/types.h"
#include "dag/types.h"
#include "sp.h"
#include "spn/core.h"
#include "external/cc.h"
#include "core/types.h"
#include "graph/build.h"
#include "toolchain/types.h"
#include "unit/types.h"

typedef struct {
  const spn_cc_link_plan_t* plan;
  sp_str_t file_name;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
  si_da(spn_dag_id_t) scripts;
  struct {
    si_da(spn_dag_id_t) archives;
    si_da(spn_dag_id_t) shared;
    si_da(spn_dag_id_t) whole_archives;
  } deps;
  spn_dag_id_t exports;
  spn_dag_id_t libc;
  spn_dag_id_t output;
  spn_dag_id_t implib;
} si_link_t;

typedef struct {
  const spn_cc_archive_plan_t* plan;
  sp_str_t file_name;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
  spn_dag_id_t output;
} si_archive_t;

typedef struct {
  const spn_cc_archive_plan_t* archive;
  spn_cc_exports_format_t format;
  spn_rsp_style_t rsp;
  sp_str_t library;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
  si_da(spn_dag_id_t) whole_archives;
  spn_dag_id_t output;
} si_exports_t;

typedef struct {
  const spn_cc_compile_plan_t* plan;
  spn_path_t source;
  spn_event_subject_t subject;
  spn_dag_id_t libc;
  spn_dag_id_t header;
  spn_dag_id_t object;
} si_compile_t;

typedef struct {
  spn_rsp_style_t style;
  si_da(spn_dag_id_t) objects;
  spn_dag_id_t output;
} si_rsp_t;

typedef struct {
  const spn_libc_t* libc;
  spn_dag_id_t output;
} si_libc_t;

typedef struct {
  spn_target_unit_t* target;
  spn_dag_id_t object;
  spn_dag_id_t header;
} si_embed_t;

typedef struct {
  spn_user_node_t* node;
  spn_dag_id_t script;
} si_user_node_t;

typedef struct {
  spn_pkg_unit_t* pkg;
  spn_dag_id_t include;
  spn_dag_id_t share;
} si_publish_t;

typedef struct {
  spn_dag_id_t source;
  spn_dag_id_t dest;
} si_publish_file_t;

typedef struct {
  const spn_cc_t* cc;
  const spn_profile_info_t* profile;
  spn_dag_id_t libc;
  spn_cc_link_t link;
  sp_str_t name;
  sp_str_t triple;
  spn_dag_id_t stamp;
} si_zig_warmup_t;

spn_err_t si_on_compile(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_compile_discover(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_archive(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_link(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_write_exports(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_write_rsp(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_write_libc(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_embed(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_user_node(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_publish(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_publish_file(const si_dag_ctx_t* c, void* user_data);
spn_err_t si_on_zig_warmup(const si_dag_ctx_t* c, void* user_data);

#endif
