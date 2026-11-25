#include "macro/macro.h"
#include "index/json.h"
#include "git/url.h"
#include "intern/intern.h"

#include "release.gen.h"
#include "semver/compare.h"
#include "semver/convert.h"
#include "semver/parser.h"
#include "sp.h"
#include "str/str.h"

#define si_release_schema_version 1

spn_err_t si_index_parse_pkg(sp_mem_t mem, sp_intern_t* intern, sp_intern_str_t name, sp_str_t blob, spn_index_pkg_t* pkg) {
  sp_da_init(mem, pkg->releases);
  bool empty = true;
  sp_str_for_line(blob, row) {
    sp_str_t line = sp_str_trim(row.line);
    if (sp_str_empty(line)) {
      continue;
    }
    empty = false;

    spn_cg_release_t rel = sp_zero;
    bool read = spn_release_read(line, &rel, mem);
    if (!sp_opt_is_null(rel.v) && sp_opt_get(rel.v) > si_release_schema_version) {
      continue;
    }
    if (!read) {
      return SPN_ERROR;
    }

    if (!sp_str_equal(rel.name, name.str)) {
      return SPN_ERROR;
    }

    spn_index_release_t release = {
      .name = name,
      .yanked = rel.yanked,
      .paths = { .manifest = rel.paths.manifest },
    };
    spn_try(spn_semver_parse(rel.version, &release.version));

    if (!sp_str_empty(rel.source.url)) {
      if (!spn_git_url_valid(rel.source.url)) {
        return SPN_ERROR;
      }
      release.source = (spn_pkg_root_t) {
        .kind = SPN_PKG_ROOT_GIT,
        .git = { .url = rel.source.url, .rev = rel.source.rev, .dir = rel.source.dir },
      };
    }
    if (!sp_str_empty(rel.manifest.url)) {
      if (!spn_git_url_valid(rel.manifest.url)) {
        return SPN_ERROR;
      }
      release.manifest = (spn_pkg_root_t) {
        .kind = SPN_PKG_ROOT_GIT,
        .git = { .url = rel.manifest.url, .rev = rel.manifest.rev, .dir = rel.manifest.dir },
      };
    }

    sp_da_init(mem, release.deps);
    sp_da_for(rel.deps, it) {
      spn_index_dep_t dep = {
        .kind = rel.deps[it].kind,
        .public = !sp_opt_is_null(rel.deps[it].public) && sp_opt_get(rel.deps[it].public),
        .name = sp_intern(intern, rel.deps[it].name),
        .when = rel.deps[it].when,
        .options = rel.deps[it].options,
      };
      spn_try(spn_semver_parse_range(rel.deps[it].version, &dep.range));
      sp_da_push(release.deps, dep);
    }

    sp_da_init(mem, release.targets);
    sp_da_for(rel.targets, it) {
      spn_index_target_t target = { .name = rel.targets[it].name };
      sp_da_init(mem, target.linkages);
      sp_da_for(rel.targets[it].linkages, n) {
        sp_da_push(target.linkages, rel.targets[it].linkages[n]);
      }
      sp_da_push(release.targets, target);
    }

    sp_da_for(rel.options, it) {
      spn_cg_release_options_entry_t* entry = &rel.options[it];
      sp_intern_str_t key = sp_intern(intern, entry->key);
      spn_option_info_t option = {
        .id = key.id,
        .name = key,
        .type = entry->value.type,
        .additive = !sp_opt_is_null(entry->value.additive) && sp_opt_get(entry->value.additive),
        .defaults = entry->value.defaults,
      };
      sp_da_for(entry->value.values, vt) {
        si_da_push(mem, option.values, entry->value.values[vt]);
      }
      si_om_insert(mem, release.options, option.id, option);
    }

    sp_da_for(pkg->releases, n) {
      if (spn_semver_eq(pkg->releases[n].version, release.version)) {
        return SPN_ERROR;
      }
    }

    sp_da_push(pkg->releases, release);
  }

  if (empty) {
    return SPN_ERROR;
  }

  return SPN_OK;
}

sp_str_t si_index_release_to_json(sp_mem_t mem, spn_index_release_t* rel) {
  spn_cg_release_t release = {
    .name = rel->name.str,
    .version = spn_semver_to_str(mem, rel->version),
    .yanked = rel->yanked,
    .v = sp_opt_some(si_release_schema_version),
    .paths = { .manifest = rel->paths.manifest },
    .deps = sp_da_new(mem, spn_cg_release_dep_t),
    .targets = sp_da_new(mem, spn_cg_release_target_t),
    .options = sp_da_new(mem, spn_cg_release_options_entry_t),
  };

  if (rel->source.kind == SPN_PKG_ROOT_GIT) {
    release.source = (spn_cg_release_source_t) { .url = rel->source.git.url, .rev = rel->source.git.rev, .dir = rel->source.git.dir };
  }
  if (rel->manifest.kind == SPN_PKG_ROOT_GIT) {
    release.manifest = (spn_cg_release_source_t) { .url = rel->manifest.git.url, .rev = rel->manifest.git.rev, .dir = rel->manifest.git.dir };
  }

  sp_da_for(rel->deps, it) {
    spn_cg_release_dep_t dep = {
      .name = rel->deps[it].name.str,
      .version = spn_semver_range_to_str(mem, rel->deps[it].range),
      .kind = rel->deps[it].kind,
      .when = rel->deps[it].when,
      .options = rel->deps[it].options,
    };
    if (rel->deps[it].public) {
      sp_opt_set(dep.public, true);
    }
    sp_da_push(release.deps, dep);
  }

  sp_da_for(rel->targets, it) {
    sp_da_push(release.targets, ((spn_cg_release_target_t) {
      .name = rel->targets[it].name,
      .linkages = rel->targets[it].linkages,
    }));
  }

  si_om_for(rel->options, it) {
    spn_option_info_t* option = si_om_at(rel->options, it);
    spn_cg_release_options_entry_t entry = {
      .key = option->name.str,
      .value = {
        .type = option->type,
        .values = sp_da_new(mem, sp_str_t),
        .defaults = option->defaults,
      },
    };
    si_da_for(option->values, vt) {
      sp_da_push(entry.value.values, option->values[vt]);
    }
    if (option->additive) {
      sp_opt_set(entry.value.additive, true);
    }
    sp_da_push(release.options, entry);
  }

  return spn_release_write_compact(mem, &release);
}
