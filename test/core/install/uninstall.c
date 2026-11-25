#include "harness.h"

#define RC_LINE ". \"$HOME/.spn/env\""

#define OWNED_UNIX \
  .owned = { .exe = true, .env = true, .fish_conf = true, .bin = true, .root = true, .storage = true }, \
  .rc = { \
    [INSTALL_RC_PROFILE] = { .exists = true, .has_line = true }, \
    [INSTALL_RC_BASHRC] = { .exists = true, .has_line = true }, \
  }

#define REMOVE_UNIX \
  { SPN_INSTALL_ACTION_REMOVE_FILE, SPN_INSTALL_ROLE_EXE, "/h/.spn/bin/spn" }, \
  { SPN_INSTALL_ACTION_REMOVE_FILE, SPN_INSTALL_ROLE_ENV, "/h/.spn/env" }, \
  { SPN_INSTALL_ACTION_REMOVE_LINE, SPN_INSTALL_ROLE_HOOK, "/h/.profile", .line = RC_LINE }, \
  { SPN_INSTALL_ACTION_REMOVE_LINE, SPN_INSTALL_ROLE_HOOK, "/h/.bashrc", .line = RC_LINE }, \
  { SPN_INSTALL_ACTION_REMOVE_FILE, SPN_INSTALL_ROLE_HOOK, "/h/.config/fish/conf.d/spn.fish" }, \
  { SPN_INSTALL_ACTION_REMOVE_DIR, SPN_INSTALL_ROLE_DIR, "/h/.spn/bin" }, \
  { SPN_INSTALL_ACTION_REMOVE_DIR, SPN_INSTALL_ROLE_DIR, "/h/.spn" }

typedef struct {
  const c8* name;
  install_world_t world;
  bool purge;
  struct {
    install_action_spec_t actions [SPN_INSTALL_MAX_ACTIONS + 1];
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "nothing_installed",
    .world = { INSTALL_WORLD_UNIX },
  },
  {
    .name = "keeps_storage",
    .world = { INSTALL_WORLD_UNIX, OWNED_UNIX },
    .expect = {
      .actions = { REMOVE_UNIX },
    },
  },
  {
    .name = "purge",
    .world = { INSTALL_WORLD_UNIX, OWNED_UNIX },
    .purge = true,
    .expect = {
      .actions = {
        REMOVE_UNIX,
        { SPN_INSTALL_ACTION_REMOVE_TREE, SPN_INSTALL_ROLE_DIR, "/h/.local/share/spn" },
      },
    },
  },
  {
    .name = "windows_running",
    .world = {
      .vars = { { "USERPROFILE", "C:\\u" }, { "PATH", "C:\\p" }, { "TEMP", "C:\\t" } },
      .os = SPN_INSTALL_OS_WINDOWS,
      .exe = "c:/U/.spn/bin/SPN.EXE",
      .owned = { .exe = true, .bin = true, .root = true },
      .registry = { .path = "C:\\a;c:\\U\\.spn\\BIN;;C:\\b;", .kind = SPN_INSTALL_REG_EXPAND },
    },
    .expect = {
      .actions = {
        { SPN_INSTALL_ACTION_MOVE_FILE, SPN_INSTALL_ROLE_EXE, "C:/t/spn-uninstall.exe", .src = "C:/u/.spn/bin/spn.exe" },
        { SPN_INSTALL_ACTION_SET_USER_PATH, SPN_INSTALL_ROLE_HOOK, .text = "C:\\a;;C:\\b;", .reg = SPN_INSTALL_REG_EXPAND },
        { SPN_INSTALL_ACTION_REMOVE_DIR, SPN_INSTALL_ROLE_DIR, "C:/u/.spn/bin" },
        { SPN_INSTALL_ACTION_REMOVE_DIR, SPN_INSTALL_ROLE_DIR, "C:/u/.spn" },
      },
    },
  },
};

sp_test_each(install_uninstall, actions, test_t, tests) {
  spn_install_layout_t layout = sp_zero;
  spn_install_facts_t facts = sp_zero;
  install_world_t world = it->world;
  sp_try(install_build(t, &world, &layout, &facts));

  spn_install_plan_t plan = si_uninstall_plan(sp_test_arena(t), &layout, &facts, it->purge);
  install_expect_action_arr(t, plan.actions, plan.count, it->expect.actions);
  return SP_OK;
}
