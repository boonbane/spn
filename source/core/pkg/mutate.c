#include "intern/types.h"
#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "pkg/types.h"
#include "profile/types.h"
#include "toolchain/types.h"

#include "enum/enum.h"
#include "intern/intern.h"
#include "pkg/mutate.h"

void spn_pkg_init(spn_pkg_info_t* pkg, sp_str_t name) {
  pkg->name = spn_intern(name).str;
}

void spn_pkg_set_name(spn_pkg_info_t* pkg, const c8* name) {
  spn_pkg_set_name_ex(pkg, sp_str_view(name));
}

void spn_pkg_set_name_ex(spn_pkg_info_t* pkg, sp_str_t name) {
  pkg->name = spn_intern(name).str;
}

void spn_pkg_set_repo(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* repo) {
  spn_pkg_set_repo_ex(mem, pkg, sp_str_view(repo));
}

void spn_pkg_set_repo_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t repo) {
  pkg->repo = sp_str_copy(mem, repo);
}

void spn_pkg_set_author(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* author) {
  spn_pkg_set_author_ex(mem, pkg, sp_str_view(author));
}

void spn_pkg_set_author_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t author) {
  pkg->author = sp_str_copy(mem, author);
}

void spn_pkg_set_maintainer(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* maintainer) {
  spn_pkg_set_maintainer_ex(mem, pkg, sp_str_view(maintainer));
}

void spn_pkg_set_maintainer_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t maintainer) {
  pkg->maintainer = sp_str_copy(mem, maintainer);
}

void spn_pkg_add_include(sp_mem_t mem, spn_pkg_info_t* pkg, spn_path_t path) {
  si_da_push(mem, pkg->configured.include, path);
}

void spn_pkg_add_define(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* define) {
  spn_pkg_add_define_ex(mem, pkg, sp_str_view(define));
}

void spn_pkg_add_define_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t define) {
  si_da_push(mem, pkg->define, sp_str_copy(mem, define));
}

void spn_pkg_add_system_dep(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* dep) {
  spn_pkg_add_system_dep_ex(mem, pkg, sp_str_view(dep));
}

void spn_pkg_add_system_dep_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t dep) {
  si_da_push(mem, pkg->system_deps, sp_str_copy(mem, dep));
}

spn_err_t spn_pkg_add_target(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t name, spn_target_kind_t kind, spn_target_info_t** out) {
  sp_intern_str_t interned = spn_intern(name);
  spn_target_id_t id = { .name = interned.id, .kind = kind };
  if (si_om_has(pkg->targets, id)) {
    return SPN_ERR_TARGET_DUPLICATE;
  }

  si_om_insert(mem, pkg->targets, id, ((spn_target_info_t) {
    .id = id,
    .name = interned,
    .kind = kind
  }));
  *out = si_om_back(pkg->targets);
  return SPN_OK;
}
