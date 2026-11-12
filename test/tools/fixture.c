#include "fixture.h"
#include TEST_EMBED

static sp_test_once_t root_once;
static sp_str_t root;

static sp_err_t find_root(void* user) {
  sp_mem_t mem = sp_mem_os_new();
  sp_str_t exe = sp_zero;
  sp_fs_get_exe_path(mem, &exe);
  for (sp_str_t dir = sp_fs_parent_path(exe); !sp_str_equal(dir, sp_fs_parent_path(dir)); dir = sp_fs_parent_path(dir)) {
    if (sp_fs_exists(sp_fs_join_path(mem, dir, strl("spn.toml")))) {
      root = dir;
      return SP_OK;
    }
  }

  sp_str_t cwd = sp_zero;
  sp_try(sp_fs_get_cwd_path(mem, &cwd));
  root = sp_fs_join_path(mem, sp_fs_join_path(mem, cwd, strl(".sp/root")), sp_fs_get_stem(exe));
  sp_fs_remove_dir(root);
  sp_carr_for(spn_embed_manifest, it) {
    spn_embed_entry_t entry = spn_embed_manifest[it];
    sp_str_t content = sp_str((const c8*)entry.data, entry.size);
    sp_path_t path = sp_path_from_str(sp_fs_join_path(mem, root, sp_cstr_as_str(entry.path)));
    sp_try(sp_fs_create_parent_at(path));
    sp_try(sp_fs_create_file_str_at(path, content));
    if (sp_str_starts_with(content, strl("#!"))) {
      sp_sys_file_meta_t meta = sp_zero;
      sp_try(sp_sys_get_path_metadata_s(path.dir, path.sub, &meta));
      sp_sys_set_executable(&meta.perms, true);
      sp_try(sp_sys_set_file_perms_s(path.dir, path.sub, meta.perms));
    }
  }
  return SP_OK;
}

sp_str_t test_repo_root(void) {
  sp_assert(!sp_test_once(&root_once, find_root, SP_NULLPTR));
  return root;
}

sp_str_t test_repo_path(sp_mem_t mem, sp_str_t rel) {
  if (sp_fs_is_absolute(rel)) {
    return sp_str_copy(mem, rel);
  }
  return sp_fs_join_path(mem, test_repo_root(), rel);
}

sp_ps_output_t git_repo_run(sp_str_t repo, const sp_str_t* args, u32 count) {
  sp_ps_config_t config = {
    .command = sp_str_lit("git"),
    .args = { sp_str_lit("-C"), repo },
  };
  SP_ASSERT(count <= SP_PS_MAX_ARGS - 2);
  sp_for(it, count) {
    config.args[2 + it] = args[it];
  }

  sp_ps_output_t output = sp_ps_run(sp_mem_os_new(), config);
  SP_ASSERT(output.status.exit_code == 0);
  return output;
}

sp_ps_output_t git_repo_run_cstr(sp_str_t repo, const c8** args, u32 count) {
  sp_str_t strs [SP_PS_MAX_ARGS];
  SP_ASSERT(count <= SP_PS_MAX_ARGS - 2);
  sp_for(it, count) {
    strs[it] = sp_cstr_as_str(args[it]);
  }
  return git_repo_run(repo, strs, count);
}

void git_repo_init(sp_str_t repo) {
  sp_fs_create_dir(repo);

  git(repo, "init", "--quiet");
  git(repo, "config", "user.name", "spn-test");
  git(repo, "config", "user.email", "spn-test@local");
}

void git_repo_stage_all(sp_str_t repo) {
  git(repo, "add", ".");
}

void git_repo_commit(sp_str_t repo, sp_str_t message) {
  git_repo_git(repo, sp_str_lit("commit"), sp_str_lit("-m"), message, sp_str_lit("--quiet"), sp_str_lit("--allow-empty"));
}

void git_repo_push(sp_str_t repo, sp_str_t remote, const c8* refspec) {
  git_repo_git(repo, sp_str_lit("push"), sp_str_lit("--quiet"), remote, sp_cstr_as_str(refspec));
}

void git_repo_pull(sp_str_t repo, sp_str_t remote, const c8* branch) {
  git_repo_git(repo, sp_str_lit("pull"), sp_str_lit("--quiet"), remote, sp_cstr_as_str(branch));
}

void git_repo_commit_from_dir(sp_str_t source, sp_str_t repo, sp_str_t message) {
  SP_ASSERT(sp_fs_exists(source));
  SP_ASSERT(sp_fs_is_dir(source));
  SP_ASSERT(sp_fs_exists(repo));
  SP_ASSERT(sp_fs_is_dir(repo));

  git(repo, "rm", "-r", "--quiet", "--ignore-unmatch", ".");

  SP_ASSERT(!sp_fs_copy_tree_at(sp_path_from_str(source), sp_path_from_str(repo), SP_FS_ATOMIC_REPLACE));
  git_repo_stage_all(repo);
  git_repo_commit(repo, message);
}

sp_str_t git_repo_head(sp_str_t repo) {
  sp_ps_output_t output = git(repo, "rev-parse", "--short=12", "HEAD");
  return sp_str_trim_right(output.out);
}

static void git_repo_write_file(sp_str_t repo, const c8* path, const c8* content) {
  sp_str_t full = sp_fs_join_path(sp_mem_os_new(), repo, sp_cstr_as_str(path));
  sp_fs_create_dir(sp_fs_parent_path(full));
  sp_fs_create_file_str(full, content ? sp_cstr_as_str(content) : sp_str_lit(""));
}

git_repo_result_t git_repo_build_at(sp_str_t dir, const c8* name, git_repo_fixture_t* fixture) {
  git_repo_result_t result = sp_zero;
  result.path = sp_fs_join_path(sp_mem_os_new(), dir, sp_cstr_as_str(name));

  git_repo_init(result.path);

  sp_carr_for(fixture->commits, c) {
    git_repo_commit_t* commit = &fixture->commits[c];
    if (!commit->message) break;

    git(result.path, "rm", "-r", "--quiet", "--ignore-unmatch", ".");

    sp_carr_for(commit->files, f) {
      git_repo_file_t* file = &commit->files[f];
      if (!file->path) break;

      git_repo_write_file(result.path, file->path, file->content);
    }

    git_repo_stage_all(result.path);
    git_repo_commit(result.path, sp_cstr_as_str(commit->message));

    result.commits[c] = git_repo_head(result.path);
    result.commit_count++;
  }

  return result;
}

sp_str_t git_repo_file_at(sp_str_t repo, sp_str_t commit, sp_str_t path) {
  sp_str_t spec = sp_fmt(sp_mem_os_new(), "{}:{}", sp_fmt_str(commit), sp_fmt_str(path)).value;
  sp_ps_output_t output = git_repo_git(repo, sp_str_lit("show"), spec);
  return output.out;
}

bool git_repo_has_file(sp_str_t repo, sp_str_t commit, sp_str_t path) {
  sp_str_t spec = sp_fmt(sp_mem_os_new(), "{}:{}", sp_fmt_str(commit), sp_fmt_str(path)).value;
  sp_ps_output_t output = sp_ps_run(sp_mem_os_new(), (sp_ps_config_t) {
    .command = sp_str_lit("git"),
    .args = { sp_str_lit("-C"), repo, sp_str_lit("cat-file"), sp_str_lit("-e"), spec },
  });
  return output.status.exit_code == 0;
}
