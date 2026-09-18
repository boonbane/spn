#include "paths/paths.h"

#include "sp.h"
#include "macro/macro.h"
#include "spn/core.h"

spn_path_at_t spn_path_at(const spn_path_roots_t* roots, spn_path_t path) {
  if (path.root == SPN_PATH_ROOT_NONE) {
    return (spn_path_at_t) { .fd = sp_sys_get_root(0), .sub = path.sub };
  }
  sp_assert(roots->opened & spn_path_root_mask(path.root));
  return (spn_path_at_t) {
    .fd = roots->fds[path.root],
    .sub = sp_str_empty(path.sub) ? sp_str_lit(".") : path.sub,
  };
}

sp_err_t spn_get_path_metadata(const spn_path_roots_t* roots, spn_path_t path, sp_sys_file_meta_t* meta) {
  spn_path_at_t at = spn_path_at(roots, path);
  return sp_sys_get_path_metadata_s(at.fd, at.sub, meta);
}

static sp_err_t mkdir_at(sp_sys_fd_t fd, sp_str_t path) {
  sp_err_t err = sp_sys_mkdir_s(fd, path, sp_sys_default_dir_perms);
  if (!err || err == SP_ERR_SYS_NOT_FOUND) {
    return err;
  }
  sp_sys_file_meta_t meta = sp_zero;
  if (sp_sys_get_path_metadata_s(fd, path, &meta)) {
    return err;
  }
  return meta.kind == SP_FS_KIND_DIR ? SP_OK : err;
}

static sp_err_t create_dir_at(sp_sys_fd_t fd, sp_str_t path) {
  sp_err_t err = mkdir_at(fd, path);
  if (err != SP_ERR_SYS_NOT_FOUND) {
    return err;
  }
  sp_str_t parent = sp_fs_parent_path(path);
  if (sp_str_empty(parent)) {
    return err;
  }
  err = create_dir_at(fd, parent);
  if (err) {
    return err == SP_ERR_SYS_EXISTS ? SP_ERR_SYS_NOT_DIR : err;
  }
  return mkdir_at(fd, path);
}

sp_err_t spn_path_create_dir(const spn_path_roots_t* roots, spn_path_t path) {
  spn_path_at_t at = spn_path_at(roots, path);
  return create_dir_at(at.fd, at.sub);
}

sp_err_t spn_path_open_reader(const spn_path_roots_t* roots, spn_path_t path, sp_io_file_reader_t* reader) {
  spn_path_at_t at = spn_path_at(roots, path);
  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_try(sp_sys_open_s(at.fd, at.sub, SP_SYS_OPEN_MODE_RO, 0, &fd));
  return sp_io_file_reader_from_file(reader, fd, SP_IO_CLOSE_MODE_AUTO);
}

sp_err_t spn_path_open_writer(const spn_path_roots_t* roots, spn_path_t path, sp_io_file_writer_t* writer) {
  spn_path_at_t at = spn_path_at(roots, path);
  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_try(sp_sys_open_s(at.fd, at.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_TRUNCATE, &fd));
  sp_try(sp_io_file_writer_from_fd(writer, fd, SP_IO_CLOSE_MODE_AUTO));
  writer->size_known = true;
  return SP_OK;
}

sp_err_t spn_path_read(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t path, sp_str_t* content) {
  sp_io_file_reader_t reader = sp_zero;
  sp_try(spn_path_open_reader(roots, path, &reader));

  u64 size = 0;
  sp_err_t err = sp_io_file_reader_size(&reader, &size);
  if (!err && size) {
    c8* buffer = sp_alloc_n(mem, c8, size);
    u64 read = 0;
    err = sp_io_read_all(&reader.base, buffer, size, &read);
    if (err == SP_ERR_IO_EOF) {
      err = SP_OK;
    }
    *content = sp_str(buffer, (u32)read);
  }
  sp_io_file_reader_close(&reader);
  return err;
}

static sp_str_t canonical_dir(sp_mem_t mem, sp_str_t dir) {
  sp_fs_create_dir(dir);
  sp_str_t canonical = sp_fs_canonicalize_path(mem, dir);
  return sp_str_empty(canonical) ? dir : canonical;
}

sp_str_t spn_path_roots_init(spn_path_roots_t* roots, sp_mem_t mem, sp_str_t storage) {
  roots->storage = canonical_dir(mem, storage);
  return roots->storage;
}

sp_str_t spn_path_roots_set(spn_path_roots_t* roots, sp_mem_t mem, spn_path_root_t kind, sp_str_t dir) {
  spn_path_root_set_t mask = spn_path_root_mask(kind);
  sp_assert(!(roots->opened & mask));
  roots->dirs[kind] = canonical_dir(mem, dir);
  roots->fds[kind] = SP_SYS_INVALID_FD;
  if (!sp_sys_open_dir_s(sp_sys_get_root(0), roots->dirs[kind], &roots->fds[kind])) {
    roots->opened |= mask;
  }
  return roots->dirs[kind];
}

void spn_path_roots_close(spn_path_roots_t* roots) {
  for (u32 it = SPN_PATH_ROOT_NONE + 1; it < SPN_PATH_ROOT_COUNT; it++) {
    if (roots->opened & spn_path_root_mask((spn_path_root_t)it)) {
      sp_sys_close(roots->fds[it]);
    }
  }
  roots->opened = 0;
}

static bool root_match(sp_str_t dir, sp_str_t path) {
  if (!sp_str_starts_with(path, dir)) {
    return false;
  }
  return path.len == dir.len || path.data[dir.len] == '/';
}

spn_path_root_t spn_path_root_longest(const spn_path_roots_t* roots, sp_str_t str) {
  spn_path_root_t best = SPN_PATH_ROOT_NONE;
  u32 longest = 0;
  for (u32 it = SPN_PATH_ROOT_NONE + 1; it < SPN_PATH_ROOT_COUNT; it++) {
    sp_str_t dir = roots->dirs[it];
    if (dir.len > longest && root_match(dir, str)) {
      longest = dir.len;
      best = (spn_path_root_t)it;
    }
  }
  return best;
}

bool spn_path_roots_intersect(const spn_path_roots_t* roots, sp_str_t dir) {
  for (u32 it = SPN_PATH_ROOT_NONE + 1; it < SPN_PATH_ROOT_COUNT; it++) {
    if (!sp_str_empty(roots->dirs[it]) && root_match(dir, roots->dirs[it])) {
      return true;
    }
  }
  return false;
}

static bool seg_normal(sp_str_t seg) {
  if (sp_str_empty(seg)) {
    return false;
  }
  if (seg.len <= 2 && seg.data[0] == '.') {
    return !(seg.len == 1 || seg.data[1] == '.');
  }
  return true;
}

bool spn_path_normal(sp_str_t path) {
  u32 it = 0;
  if (path.len && (path.data[0] == '/' || path.data[0] == '\\')) {
    it = 1;
  }
  else if (path.len >= 3 && path.data[1] == ':' && (path.data[2] == '/' || path.data[2] == '\\')) {
    it = 3;
  }
  if (it == path.len) {
    return true;
  }

  u32 start = it;
  for (; it < path.len; it++) {
    c8 c = path.data[it];
    if (c != '/' && c != '\\') {
      continue;
    }
    if (!seg_normal(sp_str(path.data + start, it - start))) {
      return false;
    }
    start = it + 1;
  }
  return seg_normal(sp_str(path.data + start, it - start));
}

static spn_path_t classify(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  if (path.root != SPN_PATH_ROOT_NONE && sp_str_empty(roots->dirs[path.root])) {
    return spn_path_copy(mem, path);
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t result = path;
  sp_str_t full = spn_path_str(roots, s.mem, path);
  if (sp_fs_is_absolute(full)) {
    result = spn_path_make(roots, full);
  }
  result = spn_path_copy(mem, result);
  sp_mem_end_scratch(s);
  return result;
}

spn_path_t spn_path_anchor(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  path = classify(mem, roots, path);
  if (path.root == SPN_PATH_ROOT_NONE || !sp_str_empty(roots->dirs[path.root])) {
    sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
    sp_assert(!spn_path_roots_intersect(roots, spn_path_str(roots, s.mem, path)));
    sp_mem_end_scratch(s);
  }
  return path;
}

spn_path_t spn_path_canonicalize(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t result = path;
  sp_str_t full = spn_path_str(roots, s.mem, path);
  sp_str_t canonical = sp_fs_canonicalize_path(s.mem, full);
  if (sp_str_empty(canonical)) {
    canonical = full;
  }
  if (sp_fs_is_absolute(canonical) && spn_path_normal(canonical)) {
    result = spn_path_copy(mem, spn_path_make(roots, canonical));
  }
  sp_mem_end_scratch(s);
  return result;
}

spn_path_t spn_path_make(const spn_path_roots_t* roots, sp_str_t path) {
  sp_assert(sp_fs_is_absolute(path));
  sp_assert(spn_path_normal(path));
  spn_path_root_t root = spn_path_root_longest(roots, path);
  if (root == SPN_PATH_ROOT_NONE) {
    sp_assert(sp_str_empty(roots->storage) || !root_match(roots->storage, path));
    return (spn_path_t) { .root = SPN_PATH_ROOT_NONE, .sub = path };
  }
  u32 len = roots->dirs[root].len;
  u32 skip = len < path.len ? len + 1 : len;
  return (spn_path_t) { .root = root, .sub = sp_str(path.data + skip, path.len - skip) };
}

spn_path_t spn_path_from_root(spn_path_root_t root) {
  return (spn_path_t) { .root = root };
}

spn_path_t spn_path_copy(sp_mem_t mem, spn_path_t path) {
  return (spn_path_t) { .root = path.root, .sub = sp_str_copy(mem, path.sub) };
}

spn_path_t spn_path_join(sp_mem_t mem, spn_path_t base, sp_str_t sub) {
  return (spn_path_t) {
    .root = base.root,
    .sub = sp_str_empty(base.sub) ? sp_str_copy(mem, sub) : sp_fs_join_path(mem, base.sub, sub),
  };
}

spn_path_t spn_path_parent(spn_path_t path) {
  return (spn_path_t) { .root = path.root, .sub = sp_fs_parent_path(path.sub) };
}

spn_path_t spn_path_suffix(sp_mem_t mem, spn_path_t path, sp_str_t suffix) {
  return (spn_path_t) {
    .root = path.root,
    .sub = sp_str_concat(mem, path.sub, suffix),
  };
}

sp_str_t spn_path_str(const spn_path_roots_t* roots, sp_mem_t mem, spn_path_t path) {
  if (path.root == SPN_PATH_ROOT_NONE) {
    return sp_str_copy(mem, path.sub);
  }
  sp_str_t dir = roots->dirs[path.root];
  sp_assert(!sp_str_empty(dir));
  return sp_str_empty(path.sub) ? sp_str_copy(mem, dir) : sp_fs_join_path(mem, dir, path.sub);
}

bool spn_path_empty(spn_path_t path) {
  return path.root == SPN_PATH_ROOT_NONE && sp_str_empty(path.sub);
}

bool spn_path_equal(spn_path_t a, spn_path_t b) {
  return a.root == b.root && sp_str_equal(a.sub, b.sub);
}

spn_path_rel_t spn_path_within(spn_path_t base, spn_path_t path) {
  if (base.root != path.root) {
    return sp_zero_struct(spn_path_rel_t);
  }
  if (sp_str_empty(base.sub)) {
    return (spn_path_rel_t) { .within = base.root != SPN_PATH_ROOT_NONE, .sub = path.sub };
  }
  if (!sp_str_starts_with(path.sub, base.sub)) {
    return sp_zero_struct(spn_path_rel_t);
  }
  if (path.sub.len == base.sub.len) {
    return (spn_path_rel_t) { .within = true };
  }
  if (path.sub.data[base.sub.len] != '/') {
    return sp_zero_struct(spn_path_rel_t);
  }
  return (spn_path_rel_t) {
    .within = true,
    .sub = sp_str_sub(path.sub, (s32)base.sub.len + 1, (s32)(path.sub.len - base.sub.len - 1)),
  };
}

sp_hash_t spn_path_hash(spn_path_t path) {
  return sp_hash_bytes(path.sub.data, path.sub.len, (u64)path.root);
}

sp_static_assert(SPN_PATH_ROOT_COUNT <= 32, spn_path_root_set_must_fit_u32);

spn_path_root_set_t spn_path_root_mask(spn_path_root_t root) {
  return 1u << root;
}

spn_path_root_set_t spn_path_pinned_roots() {
  return spn_path_root_mask(SPN_PATH_ROOT_CHECKOUT)
    | spn_path_root_mask(SPN_PATH_ROOT_TOOLCHAIN);
}

sp_hash_t spn_path_on_hash(void* key, u64 size) {
  return spn_path_hash(*(spn_path_t*)key);
}

bool spn_path_on_compare(void* a, void* b, u64 size) {
  return spn_path_equal(*(spn_path_t*)a, *(spn_path_t*)b);
}

spn_arg_t spn_arg_lit(sp_str_t value) {
  return (spn_arg_t) { .prefix = value };
}

spn_arg_t spn_arg_path(spn_path_t path) {
  return (spn_arg_t) { .path = path };
}

spn_arg_t spn_arg_glue(sp_str_t prefix, spn_path_t path) {
  return (spn_arg_t) { .prefix = prefix, .path = path };
}

spn_arg_t spn_arg_prepend(sp_mem_t mem, sp_str_t prefix, spn_arg_t arg) {
  return (spn_arg_t) { .prefix = sp_str_concat(mem, prefix, arg.prefix), .path = arg.path };
}

bool spn_arg_empty(spn_arg_t arg) {
  return sp_str_empty(arg.prefix) && spn_path_empty(arg.path);
}

sp_str_t spn_arg_str(const spn_path_roots_t* roots, sp_mem_t mem, spn_arg_t arg) {
  return sp_str_concat(mem, arg.prefix, spn_path_str(roots, mem, arg.path));
}

sp_str_t spn_path_root_label(spn_path_root_t root) {
  switch (root) {
    case SPN_PATH_ROOT_NONE:      return sp_str_lit("absolute");
    case SPN_PATH_ROOT_PROJECT:   return sp_str_lit("project");
    case SPN_PATH_ROOT_STORE:     return sp_str_lit("store");
    case SPN_PATH_ROOT_BUILD:     return sp_str_lit("build");
    case SPN_PATH_ROOT_CHECKOUT:  return sp_str_lit("checkout");
    case SPN_PATH_ROOT_TOOLCHAIN: return sp_str_lit("toolchain");
    case SPN_PATH_ROOT_INDEX:     return sp_str_lit("index");
    case SPN_PATH_ROOT_RUNTIME:   return sp_str_lit("runtime");
    case SPN_PATH_ROOT_CACHE:     return sp_str_lit("cache");
    case SPN_PATH_ROOT_COUNT:     break;
  }
  sp_unreachable_return(sp_str_lit(""));
}

spn_path_t spn_tree_root(spn_tree_roots_t roots, spn_tree_t tree) {
  switch (tree) {
    case SPN_TREE_MANIFEST: return roots.recipe;
    case SPN_TREE_SOURCE:   return roots.source;
    case SPN_TREE_NONE:     break;
  }
  sp_unreachable_return(sp_zero_struct(spn_path_t));
}

spn_path_t spn_tree_path(sp_mem_t mem, const spn_path_roots_t* roots, spn_tree_roots_t tree, spn_tree_t decl, sp_str_t str) {
  if (sp_fs_is_absolute(str)) {
    return spn_path_copy(mem, spn_path_make(roots, str));
  }
  sp_assert(spn_path_normal(str));
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t result = classify(mem, roots, spn_path_join(s.mem, spn_tree_root(tree, decl), str));
  sp_mem_end_scratch(s);
  return result;
}

spn_tree_rel_t spn_tree_rel(spn_tree_roots_t roots, spn_path_t path) {
  spn_path_rel_t recipe = spn_path_within(roots.recipe, path);
  spn_path_rel_t source = spn_path_within(roots.source, path);

  if (recipe.within && (!source.within || roots.recipe.sub.len >= roots.source.sub.len)) {
    return (spn_tree_rel_t) { .tree = SPN_TREE_MANIFEST, .sub = recipe.sub };
  }
  if (source.within) {
    return (spn_tree_rel_t) { .tree = SPN_TREE_SOURCE, .sub = source.sub };
  }
  return (spn_tree_rel_t) { .tree = SPN_TREE_NONE, .sub = sp_str_strip_left(path.sub, sp_str_lit("/")) };
}
