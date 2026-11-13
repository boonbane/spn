#include "sp.h"
#include "macro/macro.h"
#include "git.h"

#include <git2.h>
#include <git2/sys/errors.h>

static const c8* ca_bundles [] = {
  "/etc/ssl/certs/ca-certificates.crt",
  "/etc/pki/tls/certs/ca-bundle.crt",
  "/etc/ssl/ca-bundle.pem",
  "/etc/pki/tls/cacert.pem",
  "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
  "/etc/ssl/cert.pem",
};

static c8* mirror_refspecs [] = {
  "+refs/heads/*:refs/heads/*",
  "+refs/tags/*:refs/tags/*",
};

#define SPN_GIT_TIMEOUT_MS 30000
#define SPN_GIT_ATTEMPTS 4

typedef enum {
  FETCH_OK,
  FETCH_FAILED,
  FETCH_DROPPED,
} fetch_t;

static struct {
  bool no_ca_bundle;
} git;

static bool is_file(sp_mem_t mem, sp_str_t path) {
  sp_str_t resolved = sp_zero;
  return !sp_str_empty(path) && !sp_fs_canonicalize_path(mem, path, &resolved) && sp_fs_is_file(resolved);
}

static sp_str_t find_ca_bundle(sp_mem_t mem, sp_env_t* env) {
  sp_str_t file = sp_env_get(env, sp_str_lit("SSL_CERT_FILE"));
  if (is_file(mem, file)) {
    return file;
  }
  sp_carr_for(ca_bundles, it) {
    sp_str_t bundle = sp_cstr_as_str(ca_bundles[it]);
    if (is_file(mem, bundle)) {
      return bundle;
    }
  }
  return sp_zero_s(sp_str_t);
}

static sp_str_t last_error(sp_mem_t mem) {
  const git_error* err = git_error_last();
  if (!err || !err->message) {
    return sp_str_lit("unknown git error");
  }
  if (err->klass == GIT_ERROR_SSL && git.no_ca_bundle) {
    return sp_fmt(mem, "{} (no CA bundle was found; set SSL_CERT_FILE)", sp_fmt_cstr(err->message)).value;
  }
  return sp_str_copy(mem, sp_cstr_as_str(err->message));
}

static bool is_missing_path(sp_str_t url) {
  if (!sp_fs_is_absolute(url) || sp_fs_is_dir(url)) {
    return false;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_error_set_str(GIT_ERROR_NET, sp_str_to_cstr(scratch.mem, sp_fmt(scratch.mem, "{} does not exist", sp_fmt_str(url)).value));
  sp_mem_end_scratch(scratch);
  return true;
}

static git_repository* open_repo(sp_str_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* repo = SP_NULLPTR;
  git_repository_open(&repo, sp_str_to_cstr(scratch.mem, path));
  sp_mem_end_scratch(scratch);
  return repo;
}

static git_fetch_options fetch_options() {
  git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
  options.proxy_opts.type = GIT_PROXY_AUTO;
  return options;
}

static s32 create_repo(git_repository** repo, const c8* path, s32 bare, void* user) {
  git_config* config = SP_NULLPTR;
  s32 err = git_repository_init(repo, path, (u32)bare);
  if (!err) {
    err = git_repository_config(&config, *repo);
  }
  if (!err) {
    err = git_config_set_bool(config, "core.autocrlf", 0);
  }
  if (!err) {
    err = git_config_set_bool(config, "core.longpaths", 1);
  }
  git_config_free(config);
  return err;
}

static bool is_dropped(s32 err) {
  const git_error* last = git_error_last();
  if (!err || err == GIT_ECERTIFICATE || !last) {
    return false;
  }
  return err == GIT_TIMEOUT || last->klass == GIT_ERROR_SSL || last->klass == GIT_ERROR_OS;
}

static fetch_t fetch_origin(sp_mem_t mem, sp_str_t path, c8** refspecs, u32 count, git_fetch_options* options, sp_str_t* error) {
  git_strarray specs = { .strings = refspecs, .count = count };
  git_repository* repo = open_repo(path);
  git_remote* remote = SP_NULLPTR;
  fetch_t result = FETCH_FAILED;
  if (repo && !git_remote_lookup(&remote, repo, "origin") && !is_missing_path(sp_cstr_as_str(git_remote_url(remote)))) {
    sp_for(attempt, SPN_GIT_ATTEMPTS) {
      s32 err = git_remote_fetch(remote, count ? &specs : SP_NULLPTR, options, SP_NULLPTR);
      result = !err ? FETCH_OK : is_dropped(err) ? FETCH_DROPPED : FETCH_FAILED;
      if (result != FETCH_DROPPED) {
        break;
      }
    }
  }
  if (result != FETCH_OK && error) {
    *error = last_error(mem);
  }
  git_remote_free(remote);
  git_repository_free(repo);
  return result;
}

static bool relative_path(sp_mem_t mem, sp_str_t root, sp_str_t path, sp_str_t* relative) {
  sp_str_t from = sp_zero;
  sp_str_t to = sp_zero;
  if (sp_fs_canonicalize_path(mem, root, &from) || sp_fs_canonicalize_path(mem, path, &to)) {
    return false;
  }
  if (!sp_str_starts_with(to, from)) {
    return false;
  }
  sp_str_t rest = sp_str_replace_c8(mem, sp_str_suffix(to, to.len - from.len), '\\', '/');
  if (!sp_str_empty(rest) && !sp_str_starts_with(rest, sp_str_lit("/"))) {
    return false;
  }
  *relative = sp_str_strip_left(rest, sp_str_lit("/"));
  return true;
}

static bool is_repo_root(sp_str_t repo) {
  if (sp_str_empty(repo) || !sp_fs_is_dir(repo)) {
    return false;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t root = sp_zero;
  sp_str_t lhs = sp_zero;
  sp_str_t rhs = sp_zero;
  bool ok = !spn_git_get_root(scratch.mem, repo, &root) &&
    !sp_fs_canonicalize_path(scratch.mem, root, &lhs) &&
    !sp_fs_canonicalize_path(scratch.mem, repo, &rhs) &&
    sp_str_equal(lhs, rhs);
  sp_mem_end_scratch(scratch);
  return ok;
}

spn_err_t spn_git_init(sp_env_t* env) {
  if (git_libgit2_init() < 0) {
    return SPN_ERROR;
  }
  git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
  git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, SPN_GIT_TIMEOUT_MS);
  git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, SPN_GIT_TIMEOUT_MS);
  if (!sp_str_equal_cstr(sp_cstr_as_str(git_libgit2_feature_backend(GIT_FEATURE_HTTPS)), "mbedtls")) {
    return SPN_OK;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t bundle = find_ca_bundle(scratch.mem, env);
  git.no_ca_bundle = sp_str_empty(bundle) ||
    git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS, sp_str_to_cstr(scratch.mem, bundle), SP_NULLPTR);
  sp_mem_end_scratch(scratch);
  return SPN_OK;
}

spn_err_t spn_git_clone(sp_str_t url, sp_str_t path) {
  if (is_missing_path(url)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_clone_options options = GIT_CLONE_OPTIONS_INIT;
  options.fetch_opts = fetch_options();
  options.repository_cb = create_repo;
  git_repository* repo = SP_NULLPTR;
  s32 err = 0;
  sp_for(attempt, SPN_GIT_ATTEMPTS) {
    err = git_clone(&repo, sp_str_to_cstr(scratch.mem, url), sp_str_to_cstr(scratch.mem, path), &options);
    if (!is_dropped(err)) {
      break;
    }
  }
  git_repository_free(repo);
  sp_mem_end_scratch(scratch);

  if (err || !sp_fs_is_dir(path)) {
    return SPN_ERROR;
  }
  return SPN_OK;
}

spn_err_t spn_git_init_bare(sp_mem_t mem, sp_str_t url, sp_str_t path, sp_str_t* error) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* repo = SP_NULLPTR;
  git_remote* remote = SP_NULLPTR;
  bool ok =
    !create_repo(&repo, sp_str_to_cstr(scratch.mem, path), 1, SP_NULLPTR) &&
    !git_remote_create_with_fetchspec(&remote, repo, "origin", sp_str_to_cstr(scratch.mem, url), mirror_refspecs[0]);
  if (!ok) {
    *error = last_error(mem);
  }
  git_remote_free(remote);
  git_repository_free(repo);
  sp_mem_end_scratch(scratch);

  if (!ok) {
    sp_fs_remove_dir(path);
    return SPN_ERROR;
  }
  return SPN_OK;
}

spn_err_t spn_git_clone_shared(sp_mem_t mem, sp_str_t source, sp_str_t path, sp_str_t* error) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* repo = SP_NULLPTR;
  sp_str_t objects = sp_zero;
  bool ok = !create_repo(&repo, sp_str_to_cstr(scratch.mem, path), 0, SP_NULLPTR);
  if (!ok) {
    *error = last_error(mem);
  }
  git_repository_free(repo);

  if (ok) {
    ok = !sp_fs_canonicalize_path(scratch.mem, sp_fs_join_path(scratch.mem, source, sp_str_lit("objects")), &objects) &&
      !sp_fs_create_file_str(
        sp_fs_join_path(scratch.mem, path, sp_str_lit(".git/objects/info/alternates")),
        sp_fmt(scratch.mem, "{}\n", sp_fmt_str(sp_str_replace_c8(scratch.mem, objects, '\\', '/'))).value);

    // Borrowed objects stop at the source's shallow roots, so the roots have
    // to come along or git walks into parents that were never fetched
    sp_str_t shallow = sp_zero;
    if (ok && !sp_io_read_file(scratch.mem, sp_fs_join_path(scratch.mem, source, sp_str_lit("shallow")), &shallow)) {
      ok = !sp_fs_create_file_str(sp_fs_join_path(scratch.mem, path, sp_str_lit(".git/shallow")), shallow);
    }
    if (!ok) {
      *error = sp_fmt(mem, "failed to share objects from {}", sp_fmt_str(source)).value;
    }
  }
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_fetch(sp_str_t repo) {
  git_fetch_options options = fetch_options();
  return fetch_origin(sp_zero_s(sp_mem_t), repo, SP_NULLPTR, 0, &options, SP_NULLPTR) ? SPN_ERROR : SPN_OK;
}

static spn_err_t fetch_mirror(sp_mem_t mem, sp_str_t repo, sp_str_t* error) {
  git_repository* r = open_repo(repo);
  git_fetch_options options = fetch_options();
  if (r && git_repository_is_shallow(r)) {
    options.depth = GIT_FETCH_DEPTH_UNSHALLOW;
  }
  git_repository_free(r);
  return fetch_origin(mem, repo, mirror_refspecs, sp_carr_len(mirror_refspecs), &options, error) ? SPN_ERROR : SPN_OK;
}

spn_err_t spn_git_fetch_rev(sp_mem_t mem, sp_str_t repo, sp_str_t rev, sp_str_t* error) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  c8* spec = sp_str_to_cstr(scratch.mem, rev);
  git_fetch_options options = fetch_options();
  options.depth = 1;
  options.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
  sp_str_t dropped = sp_zero;
  fetch_t shallow = fetch_origin(mem, repo, &spec, 1, &options, &dropped);
  sp_mem_end_scratch(scratch);

  if (shallow == FETCH_DROPPED) {
    *error = dropped;
    return SPN_ERROR;
  }
  if (spn_git_has_rev(repo, rev)) {
    return SPN_OK;
  }
  return fetch_mirror(mem, repo, error);
}

bool spn_git_has_rev(sp_str_t repo, sp_str_t rev) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* r = open_repo(repo);
  git_object* object = SP_NULLPTR;
  bool found = r && !git_revparse_single(&object, r, sp_str_to_cstr(scratch.mem, rev));
  git_object_free(object);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return found;
}

spn_err_t spn_git_get_remote_url(sp_mem_t mem, sp_str_t repo, sp_str_t* url) {
  git_repository* r = open_repo(repo);
  git_remote* remote = SP_NULLPTR;
  bool ok = r && !git_remote_lookup(&remote, r, "origin") && git_remote_url(remote);
  if (ok) {
    *url = sp_str_copy(mem, sp_cstr_as_str(git_remote_url(remote)));
  }
  git_remote_free(remote);
  git_repository_free(r);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_get_root(sp_mem_t mem, sp_str_t cwd, sp_str_t* root) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* r = SP_NULLPTR;
  bool ok = !git_repository_open_ext(&r, sp_str_to_cstr(scratch.mem, cwd), 0, SP_NULLPTR) && git_repository_workdir(r);
  if (ok) {
    *root = sp_str_copy(mem, sp_str_strip_right(sp_cstr_as_str(git_repository_workdir(r)), sp_str_lit("/")));
  }
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_get_commit_full(sp_mem_t mem, sp_str_t repo, sp_str_t id, sp_str_t* sha) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* r = open_repo(repo);
  git_object* object = SP_NULLPTR;
  bool ok = r && !git_revparse_single(&object, r, sp_str_to_cstr(scratch.mem, id));
  if (ok) {
    *sha = sp_str_copy(mem, sp_cstr_as_str(git_oid_tostr_s(git_object_id(object))));
  }
  git_object_free(object);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_default_branch(sp_mem_t mem, sp_str_t repo, sp_str_t* branch) {
  git_repository* r = open_repo(repo);
  git_reference* head = SP_NULLPTR;
  bool ok = r &&
    !git_reference_lookup(&head, r, "refs/remotes/origin/HEAD") &&
    git_reference_type(head) == GIT_REFERENCE_SYMBOLIC;
  if (ok) {
    *branch = sp_str_copy(mem, sp_str_strip_left(sp_cstr_as_str(git_reference_symbolic_target(head)), sp_str_lit("refs/remotes/origin/")));
    ok = !sp_str_empty(*branch);
  }
  git_reference_free(head);
  git_repository_free(r);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_current_branch(sp_mem_t mem, sp_str_t repo, sp_str_t* branch) {
  git_repository* r = open_repo(repo);
  git_reference* head = SP_NULLPTR;
  bool ok = r &&
    !git_reference_lookup(&head, r, "HEAD") &&
    git_reference_type(head) == GIT_REFERENCE_SYMBOLIC;
  if (ok) {
    *branch = sp_str_copy(mem, sp_str_strip_left(sp_cstr_as_str(git_reference_symbolic_target(head)), sp_str_lit("refs/heads/")));
  }
  git_reference_free(head);
  git_repository_free(r);
  return ok ? SPN_OK : SPN_ERROR;
}

bool spn_git_has_remote_branches(sp_str_t repo) {
  git_repository* r = open_repo(repo);
  git_branch_iterator* branches = SP_NULLPTR;
  git_reference* branch = SP_NULLPTR;
  git_branch_t type = GIT_BRANCH_REMOTE;
  bool any = r &&
    !git_branch_iterator_new(&branches, r, GIT_BRANCH_REMOTE) &&
    !git_branch_next(&branch, &type, branches);
  git_reference_free(branch);
  git_branch_iterator_free(branches);
  git_repository_free(r);
  return any;
}

spn_err_t spn_git_checkout(sp_str_t repo, sp_str_t id) {
  if (sp_str_empty(id) || !is_repo_root(repo)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
  options.checkout_strategy = GIT_CHECKOUT_SAFE;
  git_repository* r = open_repo(repo);
  git_object* object = SP_NULLPTR;
  git_object* commit = SP_NULLPTR;
  git_reference* ref = SP_NULLPTR;
  bool ok = r &&
    !git_revparse_ext(&object, &ref, r, sp_str_to_cstr(scratch.mem, id)) &&
    !git_object_peel(&commit, object, GIT_OBJECT_COMMIT) &&
    !git_checkout_tree(r, commit, &options);
  if (ok && ref && git_reference_is_branch(ref)) {
    ok = !git_repository_set_head(r, git_reference_name(ref));
  }
  else if (ok) {
    ok = !git_repository_set_head_detached(r, git_object_id(commit));
  }
  git_reference_free(ref);
  git_object_free(commit);
  git_object_free(object);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_checkout_branch(sp_str_t repo, sp_str_t branch) {
  if (sp_str_empty(branch) || !is_repo_root(repo)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  const c8* upstream = sp_str_to_cstr(scratch.mem, sp_fmt(scratch.mem, "refs/remotes/origin/{}", sp_fmt_str(branch)).value);
  const c8* local = sp_str_to_cstr(scratch.mem, sp_fmt(scratch.mem, "refs/heads/{}", sp_fmt_str(branch)).value);
  git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
  options.checkout_strategy = GIT_CHECKOUT_FORCE;
  git_repository* r = open_repo(repo);
  git_object* commit = SP_NULLPTR;
  git_reference* ref = SP_NULLPTR;
  bool ok = r &&
    !git_revparse_single(&commit, r, upstream) &&
    !git_checkout_tree(r, commit, &options) &&
    !git_reference_create(&ref, r, local, git_object_id(commit), 1, SP_NULLPTR) &&
    !git_repository_set_head(r, local);
  git_reference_free(ref);
  git_object_free(commit);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_clean(sp_str_t repo) {
  if (!is_repo_root(repo)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_status_options options = GIT_STATUS_OPTIONS_INIT;
  options.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
  options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;
  git_repository* r = open_repo(repo);
  git_status_list* status = SP_NULLPTR;
  bool ok = r && !git_status_list_new(&status, r, &options);
  sp_for(it, ok ? git_status_list_entrycount(status) : 0) {
    const git_status_entry* entry = git_status_byindex(status, it);
    if (!(entry->status & GIT_STATUS_WT_NEW)) {
      continue;
    }
    sp_str_t path = sp_fs_join_path(scratch.mem, repo, sp_str_strip_right(sp_cstr_as_str(entry->index_to_workdir->new_file.path), sp_str_lit("/")));
    sp_err_t removed = sp_fs_is_dir(path) ? sp_fs_remove_dir(path) : sp_fs_remove_file(path);
    ok = ok && !removed;
  }
  git_status_list_free(status);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_add(sp_str_t repo, sp_str_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t relative = sp_zero;
  git_repository* r = open_repo(repo);
  git_index* index = SP_NULLPTR;
  bool ok = r &&
    relative_path(scratch.mem, repo, path, &relative) &&
    !git_repository_index(&index, r) &&
    !git_index_add_bypath(index, sp_str_to_cstr(scratch.mem, relative)) &&
    !git_index_write(index);
  git_index_free(index);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_commit(sp_str_t repo, sp_str_t message) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* r = open_repo(repo);
  git_index* index = SP_NULLPTR;
  git_tree* tree = SP_NULLPTR;
  git_signature* author = SP_NULLPTR;
  git_commit* parent = SP_NULLPTR;
  git_oid tree_id = sp_zero;
  git_oid parent_id = sp_zero;
  git_oid commit_id = sp_zero;
  bool ok = r &&
    !git_repository_index(&index, r) &&
    !git_index_write_tree(&tree_id, index) &&
    !git_tree_lookup(&tree, r, &tree_id) &&
    !git_signature_now(&author, "spn", "spn");
  if (ok && !git_reference_name_to_id(&parent_id, r, "HEAD")) {
    ok = !git_commit_lookup(&parent, r, &parent_id) && !git_oid_equal(git_commit_tree_id(parent), &tree_id);
  }
  if (ok) {
    const git_commit* parents [] = { parent };
    ok = !git_commit_create(
      &commit_id, r, "HEAD", author, author, SP_NULLPTR,
      sp_str_to_cstr(scratch.mem, sp_fmt(scratch.mem, "{}\n", sp_fmt_str(message)).value),
      tree, parent ? 1 : 0, parents);
  }
  git_commit_free(parent);
  git_signature_free(author);
  git_tree_free(tree);
  git_index_free(index);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}

spn_err_t spn_git_push(sp_mem_t mem, sp_str_t repo, sp_str_t url, sp_str_t refspec, sp_str_t* output) {
  sp_ps_output_t result = sp_ps_run(mem, (sp_ps_config_t) {
    .command = SP_LIT("git"),
    .args = {
      SP_LIT("-C"), repo,
      SP_LIT("push"), SP_LIT("--quiet"),
      url,
      refspec
    },
  });

  if (result.status.exit_code) {
    if (output) {
      *output = sp_str_trim_right(sp_str_empty(result.err) ? result.out : result.err);
    }
    return SPN_ERROR;
  }
  return SPN_OK;
}

bool spn_git_is_dirty(sp_str_t repo, sp_str_t dir) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t relative = sp_zero;
  c8* spec = SP_NULLPTR;
  git_status_options options = GIT_STATUS_OPTIONS_INIT;
  options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;
  git_repository* r = open_repo(repo);
  git_status_list* status = SP_NULLPTR;
  bool ok = r && relative_path(scratch.mem, repo, dir, &relative);
  if (ok && !sp_str_empty(relative)) {
    spec = sp_str_to_cstr(scratch.mem, relative);
    options.pathspec = (git_strarray) { .strings = &spec, .count = 1 };
  }
  ok = ok && !git_status_list_new(&status, r, &options);
  bool dirty = !ok || git_status_list_entrycount(status) > 0;
  git_status_list_free(status);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return dirty;
}

bool spn_git_rev_on_remote(sp_str_t repo, sp_str_t rev) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  git_repository* r = open_repo(repo);
  git_object* object = SP_NULLPTR;
  git_object* commit = SP_NULLPTR;
  git_branch_iterator* branches = SP_NULLPTR;
  git_reference* branch = SP_NULLPTR;
  git_branch_t type = GIT_BRANCH_REMOTE;
  git_oid tip = sp_zero;
  bool contained = false;
  bool ok = r &&
    !git_revparse_single(&object, r, sp_str_to_cstr(scratch.mem, rev)) &&
    !git_object_peel(&commit, object, GIT_OBJECT_COMMIT) &&
    !git_branch_iterator_new(&branches, r, GIT_BRANCH_REMOTE);
  while (ok && !contained && !git_branch_next(&branch, &type, branches)) {
    if (!git_reference_name_to_id(&tip, r, git_reference_name(branch))) {
      contained = git_oid_equal(&tip, git_object_id(commit)) ||
        git_graph_descendant_of(r, &tip, git_object_id(commit)) == 1;
    }
    git_reference_free(branch);
  }
  git_branch_iterator_free(branches);
  git_object_free(commit);
  git_object_free(object);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return contained;
}

spn_err_t spn_git_apply(sp_mem_t mem, sp_str_t repo, sp_str_t patch, sp_str_t* error) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t content = sp_zero;
  if (sp_io_read_file(scratch.mem, patch, &content)) {
    *error = sp_fmt(mem, "could not read {}", sp_fmt_str(patch)).value;
    sp_mem_end_scratch(scratch);
    return SPN_ERROR;
  }

  git_repository* r = open_repo(repo);
  git_diff* diff = SP_NULLPTR;
  bool ok = r &&
    !git_diff_from_buffer(&diff, content.data, content.len) &&
    !git_apply(r, diff, GIT_APPLY_LOCATION_WORKDIR, SP_NULLPTR);
  if (!ok) {
    *error = last_error(mem);
  }
  git_diff_free(diff);
  git_repository_free(r);
  sp_mem_end_scratch(scratch);
  return ok ? SPN_OK : SPN_ERROR;
}
