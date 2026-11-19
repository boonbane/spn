#include "lock/lock.h"

#include "sp.h"
#include "intern/intern.h"
#include "lock.gen.h"
#include "paths/paths.h"
#include "semver/compare.h"
#include "semver/convert.h"
#include "semver/parser.h"
#include "toml/edit.h"
#include "toml/loader.h"

static s32 order(const spn_lock_entry_t* a, const spn_lock_entry_t* b) {
  s32 by_name = sp_str_compare_alphabetical(a->name.str, b->name.str);
  if (by_name) {
    return by_name;
  }
  return spn_semver_cmp(a->version, b->version);
}

static bool root_eq(spn_lock_root_t a, spn_lock_root_t b) {
  return sp_str_equal(a.url, b.url) && sp_str_equal(a.rev, b.rev) && sp_str_equal(a.dir, b.dir);
}

static spn_lock_root_t root_from_cg(spn_cg_lock_root_t cg) {
  return (spn_lock_root_t) { .url = cg.url, .rev = cg.rev, .dir = cg.dir };
}

static void write_root(sp_io_writer_t* io, const c8* key, spn_lock_root_t root) {
  if (sp_str_empty(root.url)) {
    return;
  }
  sp_io_write_cstr(io, key, SP_NULLPTR);
  sp_io_write_cstr(io, " = { url = ", SP_NULLPTR);
  spn_toml_write_str(io, root.url);
  sp_io_write_cstr(io, ", rev = ", SP_NULLPTR);
  spn_toml_write_str(io, root.rev);
  if (!sp_str_empty(root.dir)) {
    sp_io_write_cstr(io, ", dir = ", SP_NULLPTR);
    spn_toml_write_str(io, root.dir);
  }
  sp_io_write_cstr(io, " }\n", SP_NULLPTR);
}

spn_lock_t spn_lock_new(sp_mem_t mem) {
  return (spn_lock_t) {
    .version = spn_lock_version,
    .indexes = sp_da_new(mem, spn_lock_index_t),
    .entries = sp_da_new(mem, spn_lock_entry_t),
  };
}

spn_lock_index_t* spn_lock_find_index(const spn_lock_t* lock, sp_intern_id_t name) {
  sp_da_for(lock->indexes, it) {
    if (lock->indexes[it].name.id == name) {
      return &lock->indexes[it];
    }
  }
  return SP_NULLPTR;
}

void spn_lock_pin_index(spn_lock_t* lock, spn_lock_index_t index) {
  spn_lock_index_t* pinned = spn_lock_find_index(lock, index.name.id);
  if (pinned) {
    *pinned = index;
    return;
  }

  u64 at = 0;
  sp_da_for(lock->indexes, it) {
    if (sp_str_compare_alphabetical(lock->indexes[it].name.str, index.name.str) < 0) {
      at = it + 1;
    }
  }

  sp_da_push(lock->indexes, index);
  sp_da_rfor(lock->indexes, it) {
    if (it == at) {
      break;
    }
    lock->indexes[it] = lock->indexes[it - 1];
  }
  lock->indexes[at] = index;
}

static const spn_lock_index_t* unpinned_in(const spn_lock_t* from, const spn_lock_t* other) {
  sp_da_for(from->indexes, it) {
    const spn_lock_index_t* index = &from->indexes[it];
    const spn_lock_index_t* pinned = spn_lock_find_index(other, index->name.id);
    if (!pinned || !sp_str_equal(pinned->url, index->url) || !sp_str_equal(pinned->rev, index->rev)) {
      return index;
    }
  }
  return SP_NULLPTR;
}

const spn_lock_index_t* spn_lock_index_drift(const spn_lock_t* held, const spn_lock_t* next) {
  const spn_lock_index_t* moved = unpinned_in(next, held);
  return moved ? moved : unpinned_in(held, next);
}

spn_lock_entry_t* spn_lock_find(const spn_lock_t* lock, sp_intern_id_t name, spn_semver_t version) {
  sp_da_for(lock->entries, it) {
    if (lock->entries[it].name.id == name && spn_semver_eq(lock->entries[it].version, version)) {
      return &lock->entries[it];
    }
  }
  return SP_NULLPTR;
}

bool spn_lock_insert(spn_lock_t* lock, spn_lock_entry_t entry) {
  u64 at = 0;
  sp_da_for(lock->entries, it) {
    s32 side = order(&lock->entries[it], &entry);
    if (!side) {
      return false;
    }
    if (side < 0) {
      at = it + 1;
    }
  }

  sp_da_push(lock->entries, entry);
  sp_da_rfor(lock->entries, it) {
    if (it == at) {
      break;
    }
    lock->entries[it] = lock->entries[it - 1];
  }
  lock->entries[at] = entry;
  return true;
}

spn_err_t spn_lock_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t path, spn_lock_t* lock, spn_err_union_t* err) {
  *lock = (spn_lock_t) {
    .indexes = sp_da_new(mem, spn_lock_index_t),
    .entries = sp_da_new(mem, spn_lock_entry_t),
  };

  sp_sys_file_meta_t meta = sp_zero;
  if (sp_fs_get_path_metadata_at(spn_path_at(roots, path), &meta) || meta.kind == SP_FS_KIND_UNKNOWN) {
    return SPN_OK;
  }

  spn_toml_loader_t t = spn_toml_loader(mem, intern, roots);
  spn_cg_lock_t cg = sp_zero;
  spn_toml_load_lock(&t, path, &cg);
  if (sp_da_empty(t.issues) && cg.version != spn_lock_version) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_LOCK_VERSION,
      .lock_version = { .path = spn_path_str(roots, mem, path), .version = cg.version, .supported = spn_lock_version },
    };
    return err->kind;
  }

  lock->version = cg.version;
  spn_toml_loader_push_key(&t, "index");
  sp_da_for(cg.index, it) {
    spn_lock_index_t index = {
      .name = sp_intern(intern, cg.index[it].name),
      .url = cg.index[it].url,
      .rev = cg.index[it].rev,
    };
    spn_toml_loader_push_index(&t, (u32)it);
    if (spn_lock_find_index(lock, index.name.id)) {
      spn_toml_loader_issue_at(&t, SPN_ERR_CODEGEN_DUPLICATE_KEY, index.name.str);
    }
    else {
      spn_lock_pin_index(lock, index);
    }
    spn_toml_loader_pop(&t);
  }
  spn_toml_loader_pop(&t);
  spn_toml_loader_push_key(&t, "package");
  sp_da_for(cg.package, it) {
    spn_lock_entry_t entry = {
      .name = sp_intern(intern, cg.package[it].name),
      .source = root_from_cg(cg.package[it].source),
      .manifest = root_from_cg(cg.package[it].manifest),
    };
    spn_toml_loader_push_index(&t, (u32)it);
    if (spn_semver_parse(cg.package[it].version, &entry.version)) {
      spn_toml_loader_issue(&t, SPN_ERR_CODEGEN_INVALID, "version");
    }
    else if (!spn_lock_insert(lock, entry)) {
      spn_toml_loader_issue_at(&t, SPN_ERR_CODEGEN_DUPLICATE_KEY, entry.name.str);
    }
    spn_toml_loader_pop(&t);
  }
  spn_toml_loader_pop(&t);
  if (sp_da_empty(t.issues)) {
    return SPN_OK;
  }

  *err = spn_toml_loader_err(&t, path);
  err->kind = SPN_ERR_LOCK_INVALID;
  return err->kind;
}

spn_err_t spn_lock_save(const spn_path_roots_t* roots, spn_path_t path, const spn_lock_t* lock, spn_err_union_t* err) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_io_dyn_mem_writer_t out = sp_zero;
  sp_io_dyn_mem_writer_init(scratch.mem, &out);

  sp_fmt_io(&out.base, "version = {}\n", sp_fmt_uint(lock->version));
  sp_da_for(lock->indexes, it) {
    const spn_lock_index_t* index = &lock->indexes[it];
    sp_io_write_cstr(&out.base, "\n[[index]]\nname = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, index->name.str);
    sp_io_write_cstr(&out.base, "\nurl = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, index->url);
    sp_io_write_cstr(&out.base, "\nrev = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, index->rev);
    sp_io_write_c8(&out.base, '\n');
  }
  sp_da_for(lock->entries, it) {
    const spn_lock_entry_t* entry = &lock->entries[it];
    sp_io_write_cstr(&out.base, "\n[[package]]\nname = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, entry->name.str);
    sp_io_write_cstr(&out.base, "\nversion = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, spn_semver_to_str(scratch.mem, entry->version));
    sp_io_write_c8(&out.base, '\n');
    write_root(&out.base, "source", entry->source);
    write_root(&out.base, "manifest", entry->manifest);
  }

  sp_err_t written = sp_fs_write_at(spn_path_at(roots, path), sp_io_dyn_mem_writer_as_str(&out), SP_FS_WRITE_REPLACE, sp_sys_default_file_perms);
  sp_mem_end_scratch(scratch);
  if (written) {
    *err = (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = path } };
    return err->kind;
  }
  return SPN_OK;
}

spn_lock_diff_t spn_lock_diff(const spn_lock_t* held, const spn_lock_t* next) {
  return (spn_lock_diff_t) { .held = held, .next = next };
}

bool spn_lock_diff_next(spn_lock_diff_t* diff) {
  u32 num_held = (u32)sp_da_size(diff->held->entries);
  u32 num_next = (u32)sp_da_size(diff->next->entries);
  while (diff->at.held < num_held || diff->at.next < num_next) {
    const spn_lock_entry_t* was = diff->at.held < num_held ? &diff->held->entries[diff->at.held] : SP_NULLPTR;
    const spn_lock_entry_t* now = diff->at.next < num_next ? &diff->next->entries[diff->at.next] : SP_NULLPTR;
    s32 side = !was ? 1 : !now ? -1 : order(was, now);
    if (side < 0) {
      diff->at.held++;
      diff->was = was;
      diff->now = SP_NULLPTR;
      return true;
    }
    if (side > 0) {
      diff->at.next++;
      diff->was = SP_NULLPTR;
      diff->now = now;
      return true;
    }

    diff->at.held++;
    diff->at.next++;
    if (!root_eq(was->source, now->source) || !root_eq(was->manifest, now->manifest)) {
      diff->was = was;
      diff->now = now;
      return true;
    }
  }
  return false;
}

spn_err_union_t spn_lock_mismatch(const spn_lock_entry_t* was, const spn_lock_entry_t* now) {
  bool source = !root_eq(was->source, now->source);
  spn_lock_root_t locked = source ? was->source : was->manifest;
  spn_lock_root_t found = source ? now->source : now->manifest;
  return (spn_err_union_t) {
    .kind = SPN_ERR_LOCK_MISMATCH,
    .subject = was->name.str,
    .lock_mismatch = {
      .version = was->version,
      .locked = { .url = locked.url, .rev = locked.rev, .dir = locked.dir },
      .found = { .url = found.url, .rev = found.rev, .dir = found.dir },
    },
  };
}
