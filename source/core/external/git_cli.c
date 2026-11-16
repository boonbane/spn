#include "sp.h"
#include "macro/macro.h"
#include "git_cli.h"

static const c8* inherited [] = {
  "GIT_DIR",
  "GIT_WORK_TREE",
  "GIT_INDEX_FILE",
  "GIT_OBJECT_DIRECTORY",
  "GIT_ALTERNATE_OBJECT_DIRECTORIES",
};

static sp_da(sp_str_t) command(sp_mem_t mem) {
  sp_da(sp_str_t) args = sp_da_new(mem, sp_str_t);
  sp_da_push(args, sp_str_lit("-c"));
  sp_da_push(args, sp_str_lit("gc.auto=0"));
  sp_da_push(args, sp_str_lit("-c"));
  sp_da_push(args, sp_str_lit("maintenance.auto=false"));
  return args;
}

static const struct {
  const c8* name;
  const c8* value;
} unattended [] = {
  { "GIT_TERMINAL_PROMPT", "0" },
  { "GCM_INTERACTIVE", "never" },
};

static sp_ps_output_t spawn(sp_mem_t mem, sp_da(sp_str_t) args) {
  sp_env_t env = sp_env_capture(mem);
  sp_carr_for(inherited, it) {
    sp_env_erase_c(&env, inherited[it]);
  }
  sp_carr_for(unattended, it) {
    if (!sp_env_contains_c(&env, unattended[it].name)) {
      sp_env_insert_c(&env, unattended[it].name, unattended[it].value);
    }
  }

  return sp_ps_run(mem, (sp_ps_config_t) {
    .command = sp_str_lit("git"),
    .dyn_args = args,
    .env = { .env = env, .mode = SP_PS_ENV_EXISTING },
    .io = {
      .in = { .mode = SP_PS_IO_MODE_NULL },
      .err = { .mode = SP_PS_IO_MODE_CREATE },
    },
  });
}

static sp_str_t complaint(sp_mem_t mem, sp_ps_output_t result) {
  sp_str_t said = sp_str_trim(sp_str_empty(result.err) ? result.out : result.err);
  if (!sp_str_empty(said)) {
    return said;
  }
  return result.status.exit_code == -1
    ? sp_str_lit("git could not be run; it has to be installed and on the PATH")
    : sp_fmt(mem, "git exited with {}", sp_fmt_int(result.status.exit_code)).value;
}

static spn_err_t run(sp_mem_t mem, sp_da(sp_str_t) args, spn_git_failure_t* failure) {
  sp_ps_output_t result = spawn(mem, args);

  *failure = sp_zero_s(spn_git_failure_t);
  if (!result.status.exit_code) {
    return SPN_OK;
  }

  failure->remote = true;
  sp_str_copy_to(complaint(mem, result), failure->message, spn_git_message_max - 1);
  return SPN_ERROR;
}

spn_err_t spn_git_cli_clone(sp_str_t url, sp_str_t path, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(sp_str_t) args = command(scratch.mem);
  sp_da_push(args, sp_str_lit("clone"));
  sp_da_push(args, sp_str_lit("--quiet"));
  sp_da_push(args, sp_str_lit("-c"));
  sp_da_push(args, sp_str_lit("core.autocrlf=false"));
  sp_da_push(args, sp_str_lit("-c"));
  sp_da_push(args, sp_str_lit("core.longpaths=true"));
  sp_da_push(args, sp_str_lit("--"));
  sp_da_push(args, url);
  sp_da_push(args, path);
  spn_err_t err = run(scratch.mem, args, failure);
  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_git_cli_fetch(sp_str_t repo, spn_git_fetch_t fetch, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t dotgit = sp_fs_join_path(scratch.mem, repo, sp_str_lit(".git"));
  sp_sys_file_meta_t meta = sp_zero;
  bool worktree = !sp_fs_get_link_metadata(dotgit, &meta) && meta.kind == SP_FS_KIND_DIR;
  sp_da(sp_str_t) args = command(scratch.mem);
  sp_da_push(args, sp_str_lit("--git-dir"));
  sp_da_push(args, worktree ? dotgit : repo);
  sp_da_push(args, sp_str_lit("fetch"));
  sp_da_push(args, sp_str_lit("--quiet"));
  switch (fetch.depth) {
    case SPN_GIT_DEPTH_FULL: {
      break;
    }
    case SPN_GIT_DEPTH_TIP: {
      sp_da_push(args, sp_str_lit("--depth=1"));
      break;
    }
    case SPN_GIT_DEPTH_UNSHALLOW: {
      sp_da_push(args, sp_str_lit("--unshallow"));
      break;
    }
  }
  if (fetch.no_tags) {
    sp_da_push(args, sp_str_lit("--no-tags"));
  }
  sp_da_push(args, sp_str_lit("--"));
  sp_da_push(args, sp_str_lit("origin"));
  sp_for(it, spn_git_max_refspecs) {
    if (fetch.refspecs[it]) {
      sp_da_push(args, sp_cstr_as_str(fetch.refspecs[it]));
    }
  }
  spn_err_t err = run(scratch.mem, args, failure);
  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_git_cli_push(sp_mem_t mem, sp_str_t repo, sp_str_t url, sp_str_t refspec, sp_str_t* output) {
  sp_da(sp_str_t) args = command(mem);
  sp_da_push(args, sp_str_lit("-C"));
  sp_da_push(args, repo);
  sp_da_push(args, sp_str_lit("push"));
  sp_da_push(args, sp_str_lit("--quiet"));
  sp_da_push(args, sp_str_lit("--"));
  sp_da_push(args, url);
  sp_da_push(args, refspec);
  sp_ps_output_t result = spawn(mem, args);

  if (result.status.exit_code) {
    *output = complaint(mem, result);
    return SPN_ERROR;
  }
  return SPN_OK;
}
