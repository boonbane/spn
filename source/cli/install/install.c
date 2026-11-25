#include "install/install.h"

#include "install/plan.h"
#include "fs/fs.h"
#include "str/str.h"

static spn_install_os_t os_host() {
#if defined(SP_WIN32)
  return SPN_INSTALL_OS_WINDOWS;
#else
  return SPN_INSTALL_OS_UNIX;
#endif
}

#if defined(SP_WIN32)
static spn_install_reg_t probe_registry_value(sp_mem_t mem, HKEY key, DWORD type, DWORD size, sp_str_t* out) {
  spn_install_reg_t kind = SPN_INSTALL_REG_NONE;
  switch (type) {
    case REG_SZ: kind = SPN_INSTALL_REG_SZ; break;
    case REG_EXPAND_SZ: kind = SPN_INSTALL_REG_EXPAND; break;
    default: return SPN_INSTALL_REG_OTHER;
  }
  if (size < sizeof(u16)) {
    return kind;
  }

  u16* buffer = (u16*)sp_mem_allocator_alloc(mem, size);
  if (RegQueryValueExW(key, L"Path", SP_NULLPTR, &type, (BYTE*)buffer, &size) != ERROR_SUCCESS) {
    return SPN_INSTALL_REG_OTHER;
  }
  u32 len = (u32)(size / sizeof(u16));
  while (len && !buffer[len - 1]) {
    len--;
  }
  sp_str_t value = sp_zero;
  if (sp_wtf16_to_wtf8(mem, (sp_wide_str_t) { .data = buffer, .len = len }, &value)) {
    return SPN_INSTALL_REG_OTHER;
  }
  *out = value;
  return kind;
}

static void probe_registry(sp_mem_t mem, spn_install_facts_t* facts) {
  HKEY key = SP_NULLPTR;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &key)) {
    return;
  }

  DWORD type = 0;
  DWORD size = 0;
  if (RegQueryValueExW(key, L"Path", SP_NULLPTR, &type, SP_NULLPTR, &size) == ERROR_SUCCESS) {
    facts->registry.kind = probe_registry_value(mem, key, type, size, &facts->registry.path);
  }
  RegCloseKey(key);
}

static sp_err_t set_user_path(spn_install_action_t* action) {
  sp_err_t err = SP_ERR_SYS;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_wide_str_t value = sp_zero;
  sp_try_goto(sp_wtf8_to_wtf16(scratch.mem, action->text, &value), err, done);

  HKEY key = SP_NULLPTR;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &key)) {
    goto done;
  }
  DWORD kind = action->reg == SPN_INSTALL_REG_EXPAND ? REG_EXPAND_SZ : REG_SZ;
  LSTATUS status = sp_str_empty(action->text)
    ? RegDeleteValueW(key, L"Path")
    : RegSetValueExW(key, L"Path", 0, kind, (const BYTE*)value.data, (DWORD)((value.len + 1) * sizeof(u16)));
  if (!status) {
    err = SP_OK;
  }
  RegCloseKey(key);
  if (!err) {
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, SP_NULLPTR);
  }

done:
  sp_mem_end_scratch(scratch);
  return err;
}
#else
static sp_err_t set_user_path(spn_install_action_t* action) {
  SP_UNREACHABLE_RETURN(SP_ERR);
}
#endif

static bool file_matches(sp_mem_t mem, sp_str_t path, sp_str_t text) {
  sp_str_t content = sp_zero;
  if (sp_fs_read(mem, path, &content)) {
    return false;
  }
  return sp_str_equal(content, text);
}

static bool exists(sp_str_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  return !sp_str_empty(path) && !sp_fs_get_link_metadata(path, &meta);
}

static bool holds_only(sp_str_t path, const sp_str_t* names, u32 num_names) {
  sp_sys_dir_t dir = sp_zero;
  if (sp_fs_open_dir_at(sp_path_from_str(path), &dir)) {
    return false;
  }

  bool only = true;
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF];
  sp_fs_dir_it_t it = sp_fs_dir_it(dir);
  sp_fs_dir_entry_t entry = sp_zero;
  while (only && sp_fs_dir_it_next(&it, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    only = false;
    sp_for(at, num_names) {
      only = only || sp_str_equal(entry.name, names[at]);
    }
  }
  sp_fs_dir_it_close(&it);
  sp_fs_close_dir(dir);
  return only;
}

static spn_install_facts_t probe_facts(sp_mem_t mem, spn_install_layout_t* layout) {
  spn_install_facts_t facts = sp_zero;
  sp_fs_get_exe_path(mem, &facts.exe);

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_str_t canonical = sp_zero;
  sp_fs_canonicalize_path(scratch.mem, layout->exe, &canonical);
  if (sp_str_equal(canonical, facts.exe)) {
    facts.exe = layout->exe;
  }
  sp_sys_file_meta_t meta = sp_zero;
  sp_for(it, layout->num_rc) {
    facts.rc[it].exists = !sp_fs_get_path_metadata(layout->rc[it].path, &meta) && meta.kind == SP_FS_KIND_FILE;
    if (facts.rc[it].exists) {
      sp_str_t content = sp_zero;
      if (!sp_fs_read(scratch.mem, layout->rc[it].path, &content)) {
        facts.rc[it].has_line = sp_str_contains(content, sp_str_lit(SPN_INSTALL_RC_LINE));
      }
    }
  }
  facts.env_current = file_matches(scratch.mem, layout->env_file, sp_str_lit(SPN_INSTALL_ENV_SH));
  facts.fish_current = file_matches(scratch.mem, layout->fish_conf, sp_str_lit(SPN_INSTALL_FISH_SH));
  sp_mem_end_scratch(scratch);

  facts.fish = !sp_fs_get_path_metadata(layout->fish_dir, &meta) && meta.kind == SP_FS_KIND_DIR;

  sp_str_t exe_name = sp_fs_get_name(layout->exe);
  sp_str_t root_names [] = { sp_fs_get_name(layout->bin), sp_fs_get_name(layout->env_file) };
  facts.owned = (spn_install_owned_t) {
    .exe = exists(layout->exe),
    .env = exists(layout->env_file),
    .fish_conf = exists(layout->fish_conf),
    .bin = holds_only(layout->bin, &exe_name, 1),
    .storage = exists(layout->storage),
  };
  facts.owned.root = holds_only(layout->root, root_names, sp_carr_len(root_names)) && (facts.owned.bin || !exists(layout->bin));

  sp_da_for(layout->shadows, it) {
    if (!sp_fs_get_path_metadata(layout->shadows[it], &meta) && meta.kind == SP_FS_KIND_FILE) {
      facts.shadow = layout->shadows[it];
      break;
    }
  }

#if defined(SP_WIN32)
  probe_registry(mem, &facts);
#endif
  return facts;
}

static sp_err_t remove_line(spn_install_action_t* action) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t content = sp_zero;
  sp_err_t err = sp_fs_read(scratch.mem, action->path, &content);
  if (err) {
    goto done;
  }

  c8* kept = sp_alloc_n(scratch.mem, c8, content.len);
  u32 len = 0;
  u32 cursor = 0;
  sp_str_for_line(content, it) {
    if (!sp_str_equal(it.line, action->line)) {
      continue;
    }
    u32 start = it.index;
    bool blank = start == 1 || (start > 1 && content.data[start - 2] == '\n');
    if (start > cursor && (blank || it.cursor == content.len)) {
      start--;
    }
    sp_mem_copy(kept + len, content.data + cursor, start - cursor);
    len += start - cursor;
    cursor = it.cursor;
  }
  sp_mem_copy(kept + len, content.data + cursor, content.len - cursor);
  len += content.len - cursor;

  if (len != content.len) {
    err = sp_fs_write(action->path, sp_str(kept, len), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
  }

done:
  sp_mem_end_scratch(scratch);
  return err;
}

static sp_err_t apply(spn_install_action_t* action) {
  switch (action->kind) {
    case SPN_INSTALL_ACTION_NONE: return SP_ERR;
    case SPN_INSTALL_ACTION_INSTALL_EXE: {
      sp_path_t exe = sp_path_from_str(action->path);
      sp_try(sp_fs_create_parent_at(exe));
      return sp_fs_copy_file_at(sp_path_from_str(action->src), exe, SP_FS_WRITE_REPLACE);
    }
    case SPN_INSTALL_ACTION_WRITE_FILE: {
      sp_path_t file = sp_path_from_str(action->path);
      sp_try(sp_fs_create_parent_at(file));
      return sp_fs_write_at(file, action->text, SP_FS_WRITE_REPLACE, sp_sys_default_file_perms);
    }
    case SPN_INSTALL_ACTION_APPEND_LINE: return sp_fs_append(action->path, action->text, sp_sys_default_file_perms);
    case SPN_INSTALL_ACTION_SET_USER_PATH: return set_user_path(action);
    case SPN_INSTALL_ACTION_MOVE_FILE: {
      sp_path_t from = sp_path_from_str(action->src);
      sp_path_t to = sp_path_from_str(action->path);
      return sp_sys_rename_s(from.dir, from.sub, to.dir, to.sub, 0);
    }
    case SPN_INSTALL_ACTION_REMOVE_FILE: return sp_fs_remove_file(action->path);
    case SPN_INSTALL_ACTION_REMOVE_LINE: return remove_line(action);
    case SPN_INSTALL_ACTION_REMOVE_DIR: {
      sp_path_t dir = sp_path_from_str(action->path);
      return sp_sys_rmdir_s(dir.dir, dir.sub);
    }
    case SPN_INSTALL_ACTION_REMOVE_TREE: return sp_fs_remove_dir(action->path);
  }
  return SP_ERR;
}

static spn_install_result_t execute(spn_install_plan_t* plan) {
  spn_install_result_t result = sp_zero;

  sp_for(it, plan->count) {
    sp_err_t err = apply(&plan->actions[it]);
    if (!err) {
      continue;
    }
    if (plan->actions[it].role == SPN_INSTALL_ROLE_EXE) {
      result.err = err;
      result.failed = plan->actions[it];
      return result;
    }
    result.stuck[result.num_stuck++] = (u32)it;
  }
  return result;
}

spn_install_probe_t spn_install_probe(sp_mem_t mem) {
  spn_install_probe_t probe = sp_zero;
  sp_env_t env = sp_env_capture(mem);
  probe.layout = si_install_resolve(mem, os_host(), &env);
  if (probe.layout.err) {
    return probe;
  }
  probe.facts = probe_facts(mem, &probe.layout);
  return probe;
}

spn_install_t spn_install_execute(spn_install_probe_t* probe, spn_install_plan_t* plan) {
  spn_install_result_t result = execute(plan);
  if (result.err) {
    return (spn_install_t) { .err = SPN_INSTALL_ERR_EXE, .failed = result.failed };
  }
  return (spn_install_t) {
    .err = result.num_stuck ? SPN_INSTALL_ERR_STUCK : SPN_INSTALL_OK,
    .exe = probe->layout.exe,
    .changes = plan->count,
    .stuck = result.num_stuck,
    .msgs = si_install_report(&probe->layout, &probe->facts, plan, &result),
  };
}

spn_install_t spn_install(sp_mem_t mem) {
  spn_install_probe_t probe = spn_install_probe(mem);
  if (probe.layout.err) {
    return (spn_install_t) { .err = probe.layout.err };
  }

  spn_install_choices_t choices = spn_install_choices(&probe.layout, &probe.facts);
  spn_install_plan_t plan = spn_install_plan(mem, &probe.layout, &probe.facts, &choices);
  return spn_install_execute(&probe, &plan);
}

spn_install_t si_uninstall_execute(spn_install_probe_t* probe, spn_install_plan_t* plan) {
  spn_install_result_t result = execute(plan);
  if (result.err) {
    return (spn_install_t) { .err = SPN_INSTALL_ERR_EXE, .failed = result.failed };
  }
  return (spn_install_t) {
    .err = result.num_stuck ? SPN_INSTALL_ERR_STUCK : SPN_INSTALL_OK,
    .changes = plan->count,
    .stuck = result.num_stuck,
    .msgs = si_uninstall_report(&probe->layout, &probe->facts, plan, &result),
  };
}
