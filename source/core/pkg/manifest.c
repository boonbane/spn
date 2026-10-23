#include "pkg/manifest.h"

#include "hash/digest/digest.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "profile/types.h"
#include "when/when.h"

spn_manifest_t spn_manifest_new(sp_str_t name) {
  spn_manifest_t manifest = sp_zero;
  manifest.name = spn_intern(name);
  return manifest;
}

spn_err_t spn_manifest_add_target(sp_mem_t mem, spn_manifest_t* manifest, sp_str_t name, spn_target_kind_t kind, spn_target_decl_t** decl) {
  sp_intern_str_t interned = spn_intern(name);
  spn_target_id_t id = { .name = interned.id, .kind = kind };
  if (si_om_has(manifest->targets, id)) {
    return SPN_ERR_TARGET_DUPLICATE;
  }

  si_om_insert(mem, manifest->targets, id, ((spn_target_decl_t) {
    .id = id,
    .name = interned,
    .kind = kind,
  }));
  *decl = si_om_back(manifest->targets);
  return SPN_OK;
}

spn_pkg_root_t spn_manifest_upstream(const spn_manifest_t* manifest) {
  if (sp_str_empty(manifest->upstream.url)) {
    return (spn_pkg_root_t) { .kind = SPN_PKG_ROOT_NONE };
  }
  return (spn_pkg_root_t) {
    .kind = SPN_PKG_ROOT_GIT,
    .git = { .url = manifest->upstream.url, .rev = manifest->upstream.commit },
  };
}

static sp_hash_t hash_push(sp_hash_t hash, sp_hash_t value) {
  sp_hash_t parts [] = { hash, value };
  return spn_digest_hash_combine(parts, sp_carr_len(parts));
}

static sp_hash_t hash_gated(sp_hash_t hash, spn_gated_list_t list) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  si_da_for_ptr(list, entry) {
    hash = hash_push(hash, spn_digest_hash_str(entry->value));
    hash = hash_push(hash, spn_digest_hash_str(spn_when_to_str(scratch.mem, &entry->when)));
  }
  sp_mem_end_scratch(scratch);
  return hash;
}

sp_hash_t spn_manifest_hash_platform(const spn_manifest_t* manifest, const spn_profile_info_t* profile) {
  sp_hash_t hash = 0;

  switch (profile->os) {
    case SPN_OS_MACOS: {
      hash = hash_push(hash, spn_digest_hash(&manifest->macos.min_os, sizeof(manifest->macos.min_os)));
      hash = hash_gated(hash, manifest->macos.frameworks);
      si_om_for(manifest->targets, it) {
        spn_target_decl_t* target = si_om_at(manifest->targets, it);
        hash = hash_push(hash, spn_digest_hash_str(target->name.str));
        hash = hash_push(hash, (sp_hash_t)target->kind);
        hash = hash_push(hash, spn_digest_hash(&target->macos.min_os, sizeof(target->macos.min_os)));
        hash = hash_gated(hash, target->macos.frameworks);
      }
      break;
    }
    case SPN_OS_WINDOWS: {
      si_om_for(manifest->targets, it) {
        spn_target_decl_t* target = si_om_at(manifest->targets, it);
        if (target->windows.subsystem == SPN_WIN_SUBSYSTEM_NONE) continue;
        hash = hash_push(hash, spn_digest_hash_str(target->name.str));
        hash = hash_push(hash, (sp_hash_t)target->kind);
        hash = hash_push(hash, (sp_hash_t)target->windows.subsystem);
      }
      break;
    }
    case SPN_OS_LINUX:
    case SPN_OS_WASI:
    case SPN_OS_FREESTANDING:
    case SPN_OS_NONE: {
      break;
    }
  }

  return hash;
}
