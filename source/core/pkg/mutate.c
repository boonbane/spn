#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "pkg/types.h"
#include "profile/types.h"
#include "toolchain/types.h"

#include "enum/enum.h"
#include "intern/intern.h"
#include "pkg/mutate.h"
#include "target/target.h"

sp_mem_t spn_pkg_mem(spn_pkg_info_t* pkg) {
  return sp_mem_arena_as_allocator(pkg->arena);
}

void spn_pkg_init(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t name) {
  pkg->arena = sp_mem_arena_new(mem);
  pkg->name = spn_intern(name);

  sp_str_om_init(pkg->profiles);
  sp_str_om_init(pkg->indexes);
  sp_str_om_init(pkg->toolchains);
  sp_str_om_init(pkg->options);
}

void spn_pkg_set_name(spn_pkg_info_t* pkg, const c8* name) {
  spn_pkg_set_name_ex(pkg, sp_str_view(name));
}

void spn_pkg_set_name_ex(spn_pkg_info_t* pkg, sp_str_t name) {
  pkg->name = spn_intern(name);
}

void spn_pkg_set_repo(spn_pkg_info_t* pkg, const c8* repo) {
  spn_pkg_set_repo_ex(pkg, sp_str_view(repo));
}

void spn_pkg_set_repo_ex(spn_pkg_info_t* pkg, sp_str_t repo) {
  pkg->repo = sp_str_copy(spn_pkg_mem(pkg), repo);
}

void spn_pkg_set_author(spn_pkg_info_t* pkg, const c8* author) {
  spn_pkg_set_author_ex(pkg, sp_str_view(author));
}

void spn_pkg_set_author_ex(spn_pkg_info_t* pkg, sp_str_t author) {
  pkg->author = sp_str_copy(spn_pkg_mem(pkg), author);
}

void spn_pkg_set_maintainer(spn_pkg_info_t* pkg, const c8* maintainer) {
  spn_pkg_set_maintainer_ex(pkg, sp_str_view(maintainer));
}

void spn_pkg_set_maintainer_ex(spn_pkg_info_t* pkg, sp_str_t maintainer) {
  pkg->maintainer = sp_str_copy(spn_pkg_mem(pkg), maintainer);
}

void spn_pkg_add_include(spn_pkg_info_t* pkg, spn_path_t path) {
  si_da_push(spn_pkg_mem(pkg), pkg->configured.include, path);
}

void spn_pkg_add_define(spn_pkg_info_t* pkg, const c8* define) {
  spn_pkg_add_define_ex(pkg, sp_str_view(define));
}

void spn_pkg_add_define_ex(spn_pkg_info_t* pkg, sp_str_t define) {
  si_da_push(spn_pkg_mem(pkg), pkg->define, sp_str_copy(spn_pkg_mem(pkg), define));
}

void spn_pkg_add_system_dep(spn_pkg_info_t* pkg, const c8* dep) {
  spn_pkg_add_system_dep_ex(pkg, sp_str_view(dep));
}

void spn_pkg_add_system_dep_ex(spn_pkg_info_t* pkg, sp_str_t dep) {
  si_da_push(spn_pkg_mem(pkg), pkg->system_deps, sp_str_copy(spn_pkg_mem(pkg), dep));
}

spn_target_info_t* spn_pkg_add_target(spn_pkg_info_t* pkg, sp_str_t name, spn_target_kind_t kind) {
  (void)pkg;
  (void)name;
  (void)kind;
  SP_UNIMPLEMENTED();
  return SP_NULLPTR;
}
