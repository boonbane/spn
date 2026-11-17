#include "index/release.h"

#include "target/mutate.h"

spn_err_t spn_index_release_from_manifest(sp_mem_t mem, const si_pkg_info_t* manifest, spn_pkg_root_t published, spn_index_release_t* release, spn_err_union_t* err) {
  *release = (spn_index_release_t) {
    .name = manifest->name,
    .version = manifest->version,
    .source = manifest->upstream,
    .paths = {
      .manifest = sp_str_lit("spn.toml"),
    },
    .options = manifest->options,
  };

  if (release->source.kind == SPN_PKG_ROOT_NONE) {
    release->source = published;
  } else {
    release->manifest = published;
  }

  sp_da_init(mem, release->deps);
  sp_da_init(mem, release->targets);

  si_da_for(manifest->deps, it) {
    spn_requested_dep_t* req = &manifest->deps[it];
    spn_index_dep_kind_t kind = sp_zero;
    switch (req->kind) {
      case SPN_DEP_KIND_PACKAGE: kind = SPN_INDEX_DEP_NORMAL; break;
      case SPN_DEP_KIND_BUILD:   kind = SPN_INDEX_DEP_BUILD; break;
      case SPN_DEP_KIND_TEST:
      case SPN_DEP_KIND_TOOL:    continue;
    }
    if (req->source != SPN_PKG_SOURCE_INDEX) {
      *err = (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_PATH_DEP,
        .pkg = { .name = manifest->name.str, .requested = req->name.str },
      };
      return err->kind;
    }

    sp_da_push(release->deps, ((spn_index_dep_t) {
      .kind = kind,
      .public = req->public,
      .name = req->name,
      .range = req->index.range,
      .when = req->when,
      .options = req->options,
    }));
  }

  si_om_for(manifest->targets, it) {
    si_target_info_t* lib = si_om_at(manifest->targets, it);
    if (lib->kind != SPN_TARGET_KIND_LIB) {
      continue;
    }
    spn_index_target_t target = { .name = lib->name.str };
    sp_da_init(mem, target.linkages);

    const spn_linkage_t kinds [] = { SPN_LIB_KIND_STATIC, SPN_LIB_KIND_SHARED };
    sp_carr_for(kinds, kind) {
      if (spn_linkage_set_has(lib->linkages, kinds[kind])) {
        sp_da_push(target.linkages, kinds[kind]);
      }
    }

    sp_da_push(release->targets, target);
  }

  return SPN_OK;
}
