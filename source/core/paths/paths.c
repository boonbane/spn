#include "paths/paths.h"

#include "sp.h"
#include "macro/macro.h"
#include "spn/core.h"

sp_path_t spn_path_at(const spn_path_roots_t* roots, spn_path_t path) {
  if (path.root == SPN_PATH_ROOT_NONE) {
    return sp_path_resolve(path.sub);
  }
  sp_assert(roots->opened & spn_path_root_mask(path.root));
  return sp_path_at(roots->fds[path.root], sp_str_empty(path.sub) ? sp_str_lit(".") : path.sub);
}

spn_err_t spn_path_roots_set(spn_path_roots_t* roots, sp_mem_t mem, spn_path_root_t kind, sp_path_t dir) {
  sp_assert(!(roots->opened & spn_path_root_mask(kind)));
  sp_str_t canonical = sp_fs_canonicalize_path_at(mem, dir);
  if (sp_str_empty(canonical)) {
    return SPN_ERROR;
  }
  sp_try_as(sp_fs_open_dir_at(dir, &roots->fds[kind]), SPN_ERROR);
  roots->dirs[kind] = canonical;
  roots->opened |= spn_path_root_mask(kind);
  return SPN_OK;
}

void spn_path_roots_close(spn_path_roots_t* roots) {
  sp_for(it, SPN_PATH_ROOT_COUNT) {
    if (roots->opened & spn_path_root_mask((spn_path_root_t)it)) {
      sp_sys_close(roots->fds[it]);
    }
    roots->dirs[it] = sp_str_lit("");
  }
  roots->opened = 0;
}

static bool root_match(sp_str_t dir, sp_str_t path) {
  if (!sp_str_starts_with(path, dir)) {
    return false;
  }
  return path.len == dir.len || path.data[dir.len] == '/';
}

static spn_path_root_t root_longest(const spn_path_roots_t* roots, sp_str_t str) {
  spn_path_root_t best = SPN_PATH_ROOT_NONE;
  u32 longest = 0;
  sp_for(it, SPN_PATH_ROOT_COUNT) {
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
  if (path.len && path.data[0] == '/') {
    it = 1;
  }
  if (it == path.len) {
    return true;
  }

  u32 start = it;
  for (; it < path.len; it++) {
    if (path.data[it] != '/') {
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
  spn_path_t result = spn_path_copy(mem, spn_path_make(roots, spn_path_str(roots, s.mem, path)));
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

typedef struct {
  sp_str_t canonical;
  sp_str_t tail;
} canonical_split_t;

static canonical_split_t canonical_split(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  sp_path_t at = spn_path_at(roots, path);
  sp_str_t sub = sp_fs_trim_path(at.sub);
  canonical_split_t split = { .tail = sp_str_lit("") };
  while (true) {
    split.canonical = sp_fs_canonicalize_path_at(mem, sp_path_at(at.dir, sub));
    if (!sp_str_empty(split.canonical)) {
      break;
    }
    sp_str_t parent = sp_fs_parent_path(sub);
    if (sp_str_equal(parent, sub)) {
      break;
    }
    split.tail = sp_fs_join_path(mem, sp_fs_get_name(sub), split.tail);
    sub = parent;
  }
  return split;
}

static sp_str_t head_segment(sp_str_t tail) {
  s32 sep = sp_str_find_c8(tail, '/');
  sp_str_t head = sep == SP_STR_NO_MATCH ? tail : sp_str_prefix(tail, sep);
  return seg_normal(head) ? head : sp_str_lit("");
}

spn_path_t spn_path_canonicalize(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t result = path;
  sp_str_t canonical = sp_fs_canonicalize_path_at(s.mem, spn_path_at(roots, path));
  if (sp_str_empty(canonical)) {
    canonical = spn_path_str(roots, s.mem, path);
  }
  if (spn_path_normal(canonical)) {
    result = spn_path_make(roots, canonical);
  }
  result = spn_path_copy(mem, result);
  sp_mem_end_scratch(s);
  return result;
}

spn_path_t spn_path_canonicalize_head(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t result = path;
  canonical_split_t split = canonical_split(s.mem, roots, path);
  if (!sp_str_empty(split.canonical)) {
    sp_str_t full = sp_fs_join_path(s.mem, split.canonical, head_segment(split.tail));
    if (spn_path_normal(full)) {
      result = spn_path_make(roots, full);
    }
  }
  result = spn_path_copy(mem, result);
  sp_mem_end_scratch(s);
  return result;
}

spn_path_t spn_path_from_cwd(sp_mem_t mem, const spn_path_roots_t* roots, sp_str_t str) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  spn_path_t cwd = { .sub = sp_fs_get_cwd_path(s.mem) };
  spn_path_t result = spn_path_canonicalize(mem, roots, spn_path_resolve(s.mem, cwd, str));
  sp_mem_end_scratch(s);
  return result;
}

spn_path_t spn_path_make(const spn_path_roots_t* roots, sp_str_t path) {
  sp_assert(sp_fs_is_absolute(path));
  sp_assert(spn_path_normal(path));
  spn_path_root_t root = root_longest(roots, path);
  if (root == SPN_PATH_ROOT_NONE) {
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
  sp_assert(!sp_fs_is_absolute(sub));
  return (spn_path_t) {
    .root = base.root,
    .sub = sp_str_empty(base.sub) ? sp_str_copy(mem, sub) : sp_fs_join_path(mem, base.sub, sub),
  };
}

spn_path_t spn_path_resolve(sp_mem_t mem, spn_path_t base, sp_str_t str) {
  if (sp_fs_is_absolute(str)) {
    return (spn_path_t) { .root = SPN_PATH_ROOT_NONE, .sub = sp_str_copy(mem, str) };
  }
  return spn_path_join(mem, base, str);
}

spn_path_t spn_path_parent(spn_path_t path) {
  s32 sep = sp_str_find_c8_reverse(path.sub, '/');
  u32 len = sep == SP_STR_NO_MATCH ? 0 : sep == 0 ? 1 : (u32)sep;
  return (spn_path_t) { .root = path.root, .sub = sp_str_prefix(path.sub, len) };
}

spn_path_t spn_path_suffix(sp_mem_t mem, spn_path_t path, sp_str_t suffix) {
  return (spn_path_t) {
    .root = path.root,
    .sub = sp_str_concat(mem, path.sub, suffix),
  };
}

static sp_str_t staging_name(sp_mem_t mem, sp_str_t path, sp_str_t extension) {
  static sp_atomic_s32_t sequence;
  sp_tm_epoch_t now = sp_tm_now_epoch();
  u64 stamp = (((u64)now.s << 20) ^ (u64)now.ns) ^ ((u64)(u32)sp_atomic_s32_add(&sequence, 1, SP_ATOMIC_SEQ_CST) << 48);
  return sp_fmt(mem, "{}.{}.{}", sp_fmt_str(path), sp_fmt_uint(stamp), sp_fmt_str(extension)).value;
}

spn_path_t spn_path_staging(sp_mem_t mem, spn_path_t path, sp_str_t extension) {
  return (spn_path_t) { .root = path.root, .sub = staging_name(mem, path.sub, extension) };
}

spn_err_t spn_path_stage_dir(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t path, sp_str_t extension, spn_path_t* dir) {
  sp_try_as(sp_fs_create_dir_at(spn_path_at(roots, spn_path_parent(path))), SPN_ERROR);
  sp_for(attempt, 16) {
    spn_path_t candidate = spn_path_staging(mem, path, extension);
    sp_path_t at = spn_path_at(roots, candidate);
    if (sp_sys_mkdir_s(at.dir, at.sub, sp_sys_default_dir_perms) == SP_OK) {
      *dir = candidate;
      return SPN_OK;
    }
    if (!sp_fs_exists_at(at)) {
      return SPN_ERROR;
    }
  }
  return SPN_ERROR;
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

s32 spn_path_compare(spn_path_t a, spn_path_t b) {
  if (a.root != b.root) {
    return a.root < b.root ? -1 : 1;
  }
  return sp_str_compare_alphabetical(a.sub, b.sub);
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
    case SPN_PATH_ROOT_STORAGE:   return sp_str_lit("storage");
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
  sp_assert(!sp_fs_is_absolute(str));
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
