#include "spn_test.h"

#include "manifest.h"

#include "external/http.h"
#include "intern/intern.h"
#include "model/sync.h"
#include "op/types.h"
#include "paths/paths.h"
#include "profile/profile.h"
#include "project/types.h"
#include "session/session.h"
#include "session/types.h"
#include "toolchain/catalog.h"
#include "toolchain/toolchain.h"

#define max_requests 4

typedef struct {
  const c8* name;
  spn_target_kind_t kind;
} target_t;

typedef struct {
  spn_err_t err;
  const c8* requests;
} expect_t;

typedef struct {
  const c8* name;
  target_t target;
  bool uncatalogued;
  expect_t expect;
} test_t;

typedef struct {
  sp_str_t tarball;
  c8 requests [max_requests + 1];
  u32 requested;
} remote_t;

sp_test_suite(metaprogram, .serial = true);

static const test_t tests [] = {
  {
    .name = "build_script_syncs_it",
    .target = { "B", SPN_TARGET_KIND_BUILD_METAPROGRAM },
    .expect = { .requests = "M" },
  },
  {
    .name = "configure_script_syncs_it",
    .target = { "C", SPN_TARGET_KIND_CONFIGURE_METAPROGRAM },
    .expect = { .requests = "M" },
  },
  {
    .name = "graph_without_a_script_does_not_need_it",
    .target = { "A", SPN_TARGET_KIND_EXE },
    .uncatalogued = true,
  },
  {
    .name = "script_without_it_fails_selection",
    .target = { "B", SPN_TARGET_KIND_BUILD_METAPROGRAM },
    .uncatalogued = true,
    .expect = { .err = SPN_ERR_TOOLCHAIN_NONE },
  },
};

static spn_err_t download(void* user, const spn_http_config_t* config, sp_str_t url, sp_path_t dest, spn_http_failure_t* failure) {
  remote_t* remote = (remote_t*)user;
  *failure = (spn_http_failure_t) { .kind = SPN_HTTP_FAILED, .message = "A" };
  sp_assert(remote->requested < max_requests);
  remote->requests[remote->requested++] = url.data[0];
  return sp_fs_write_at(dest, remote->tarball, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms) ? SPN_ERROR : SPN_OK;
}

static spn_toolchain_decl_t distribution(sp_mem_t mem, const c8* name, spn_artifact_t artifact, spn_triple_t target) {
  sp_intern_str_t interned = spn_intern_cstr(name);
  spn_toolchain_decl_t decl = {
    .id = interned.id,
    .name = interned,
    .driver = SPN_CC_DRIVER_GCC,
    .source = SPN_TOOLCHAIN_SOURCE_DISTRIBUTION,
  };
  sp_da_init(mem, decl.hosts);
  sp_da_push(decl.hosts, ((spn_toolchain_host_t) { .triple = spn.host, .artifact = artifact }));
  sp_da_init(mem, decl.targets);
  sp_da_push(decl.targets, ((spn_toolchain_target_t) { .triple = target }));
  return decl;
}

static void teardown(sp_test_t* t) {
  spn.caches.toolchains = sp_zero_s(spn_toolchain_store_t);
  spn.catalog = sp_zero_s(spn_toolchain_catalog_t);
  spn_path_roots_close(&spn.roots);
}

sp_test_each(metaprogram, toolchain, test_t, tests, .setup = spn_test_ctx_setup, .teardown = teardown) {
  sp_mem_t mem = sp_test_arena(t);

  remote_t remote = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_str_lit("test/core/toolchain/archives/file.tar.xz")), &remote.tarball));

  spn_path_roots_set(&spn.roots, mem, SPN_PATH_ROOT_PROJECT, sp_test_dir(t));
  spn_path_roots_set(&spn.roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn.caches.toolchains = (spn_toolchain_store_t) {
    .mem = mem,
    .roots = &spn.roots,
    .http = { .download = download, .user = &remote },
  };

  spn_artifact_t program = { .url = sp_str_lit("P"), .sha256 = sp_str_lit("P") };
  sp_must_ok(t, sp_fs_create_dir_at(spn_path_at(&spn.roots, spn_toolchain_artifact_root(mem, program))));

  spn_triple_t target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU };
  spn_toolchain_catalog_init(&spn.catalog, spn.host, sp_zero_s(spn_sdk_host_t), mem);
  spn_toolchain_catalog_add(&spn.catalog, distribution(mem, "P", program, target));
  if (!it->uncatalogued) {
    spn_artifact_t metaprogram = { .url = sp_str_lit("M"), .sha256 = test_sha256_hex(mem, remote.tarball) };
    spn_toolchain_catalog_add(&spn.catalog, distribution(mem, "M", metaprogram, spn_profile_metaprogram().target));
  }

  spn_path_t root = spn_path_from_root(SPN_PATH_ROOT_PROJECT);
  spn_project_t project = {
    .paths = {
      .manifest = spn_path_join(mem, root, sp_str_lit("spn.toml")),
      .lock = spn_path_join(mem, root, sp_str_lit("spn.lock")),
    },
    .package = test_manifest_make(sp_str_lit("A")),
  };
  test_manifest_target(mem, &project.package, sp_cstr_as_str(it->target.name), it->target.kind);

  spn_session_t session = sp_zero;
  sp_must_ok(t, spn_session_init(&session, &spn, mem, &project, (spn_session_config_t) { .profile.triple = target }));
  spn_op_t op = { .ctx = &spn, .session = &session, .mem = mem };
  sp_expect_eq(t, (u32)spn_resolve_and_sync(&op), (u32)it->expect.err);
  sp_expect_str_eq_c(t, sp_cstr_as_str(remote.requests), it->expect.requests);
  return SP_OK;
}
