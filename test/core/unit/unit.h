#pragma once

#include "spn_test.h"

#include "ctx/types.h"
#include "intern/intern.h"
#include "manifest.h"
#include "pkg/pkg.h"
#include "resolve/types.h"
#include "session/types.h"
#include "unit/types.h"
#include "unit/unit.h"

#define UNIT_TEST_MAX_PKGS 8
#define UNIT_TEST_MAX_DEPS 8
#define UNIT_TEST_MAX_TARGETS 4
#define UNIT_TEST_MAX_STRS 8
#define UNIT_TEST_MAX_ROOTS 2
#define UNIT_TEST_MAX_NODES 2
#define UNIT_TEST_MAX_OPTIONS 2

#define static_only { .static_lib = true }
#define shared_only { .shared = true }
#define object_only { .object = true }
#define source_only { .source = true }

typedef struct {
  const c8* to;
  spn_dep_kind_t kind;
  bool private;
} graph_dep_t;

typedef struct {
  const c8* name;
  spn_linkage_set_t linkages;
  spn_target_kind_t kind;
  bool no_link;
  const c8* source [UNIT_TEST_MAX_STRS];
  const c8* deps [UNIT_TEST_MAX_STRS];
  const c8* include [UNIT_TEST_MAX_STRS];
  const c8* define [UNIT_TEST_MAX_STRS];
  const c8* system_deps [UNIT_TEST_MAX_STRS];
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* link_flags [UNIT_TEST_MAX_STRS];
  const c8* linker_script [UNIT_TEST_MAX_STRS];
  const c8* embed [UNIT_TEST_MAX_STRS];
  spn_os_version_t min_os;
} graph_target_t;

typedef struct {
  const c8* tag;
  const c8* inputs [UNIT_TEST_MAX_STRS];
  const c8* outputs [UNIT_TEST_MAX_STRS];
} graph_node_t;

typedef struct {
  const c8* name;
  bool value;
  bool is_default;
  const c8* define;
  bool public;
} graph_option_t;

typedef struct {
  const c8* name;
  spn_pkg_source_t source;
  spn_linkage_t config;
  graph_dep_t deps [UNIT_TEST_MAX_DEPS];
  graph_target_t targets [UNIT_TEST_MAX_TARGETS];
  graph_option_t options [UNIT_TEST_MAX_OPTIONS];
  const c8* include [UNIT_TEST_MAX_STRS];
  const c8* define [UNIT_TEST_MAX_STRS];
  const c8* system_deps [UNIT_TEST_MAX_STRS];
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* publish [UNIT_TEST_MAX_STRS];
  graph_node_t nodes [UNIT_TEST_MAX_NODES];
  spn_os_version_t min_os;
} graph_pkg_t;

typedef struct {
  spn_os_t os;
  spn_abi_t abi;
  spn_cc_driver_t driver;
  spn_mode_t mode;
  spn_opt_level_t opt;
  spn_c_standard_t standard;
  spn_sanitizer_t sanitize;
  const c8* cc;
  const c8* link_args [UNIT_TEST_MAX_STRS];
  const c8* sdk;
  const c8* sha256;
  bool sdk_libc;
  bool no_cxx;
  spn_linking_t request;
  spn_linkage_t linkage;
  spn_runtime_t runtime;
  spn_runtime_t libc;
  graph_pkg_t pkgs [UNIT_TEST_MAX_PKGS];
} graph_t;

typedef struct {
  const c8* name;
  spn_target_kind_t kind;
} graph_root_t;

typedef struct {
  sp_mem_t mem;
  const graph_t* graph;
  spn_ctx_t ctx;
  spn_session_t session;
  spn_resolve_t resolve;
  spn_units_t configure;
  spn_units_t units;
  spn_build_plan_t configured;
  spn_build_plan_t plan;
} units_t;

units_t*            units_new(sp_test_t* t, const graph_t* graph);
spn_resolved_pkg_t* units_resolved(units_t* u, const c8* pkg);
si_pkg_info_t*      units_manifest(units_t* u, const c8* pkg);
spn_err_t           units_add_package(units_t* u, const c8* pkg, spn_pkg_unit_t** unit);
spn_err_t           units_add_target(units_t* u, spn_pkg_unit_t* pkg, graph_root_t root, spn_target_unit_t** unit);
spn_err_t           units_add_roots(units_t* u, const graph_root_t* roots, u32 count, si_da(spn_target_unit_t*)* targets);
spn_err_t           units_plan(units_t* u, si_da(spn_target_unit_t*) targets);
spn_target_unit_t*  units_find_target(units_t* u, const c8* name, spn_target_kind_t kind);
sp_da(spn_event_t)  units_errs(units_t* u);
