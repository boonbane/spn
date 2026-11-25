#include "toolchain.h"

#include "toml/loader.h"

#define max_hosts 2
#define max_targets 2
#define max_flags 2

typedef struct {
  spn_triple_t triple;
  const c8* url;
  const c8* sha256;
  const c8* mirrors;
  const c8* snapshot;
} host_t;

typedef struct {
  spn_triple_t triple;
  const c8* spelling;
  struct {
    const c8* flags [max_flags + 1];
    const c8* link_flags [max_flags + 1];
  } exceptions;
} target_t;

typedef struct {
  const c8* version;
  spn_cc_driver_t driver;
  spn_toolchain_source_t source;
  spn_toolchain_detect_t detect;
  bool host_row;
  fixture_launcher_t compiler;
  fixture_launcher_t cxx;
  fixture_launcher_t archiver;
  bool lld;
  const c8* link_args [FIXTURE_MAX_ARGS];
  host_t hosts [max_hosts];
  target_t targets [max_targets];
} decl_expect_t;

typedef struct {
  const c8* name;
  const c8* file;
  decl_expect_t expect;
} decl_test_t;

typedef struct {
  const c8* name;
  const c8* file;
  target_t expect [max_targets];
} target_test_t;

typedef struct {
  spn_err_t code;
  const c8* path;
} issue_t;

typedef struct {
  const c8* name;
  const c8* file;
  issue_t expect;
} issue_test_t;

static const decl_test_t decl_tests [] = {
  {
    .name = "distribution",
    .file = "distribution.toml",
    .expect = {
      .version = "1.0.0",
      .driver = SPN_CC_DRIVER_CLANG,
      .source = SPN_TOOLCHAIN_SOURCE_DISTRIBUTION,
      .compiler = { .name = "A", .args = { "cc" } },
      .cxx = { .name = "A", .args = { "c++" } },
      .archiver = { .name = "A", .args = { "ar" } },
      .lld = true,
      .link_args = { "-fuse-ld=lld" },
      .hosts = {
        {
          .triple = { SPN_ARCH_X64, SPN_OS_LINUX },
          .url = "https://example.com/linux.tar.xz",
          .sha256 = "aa",
          .mirrors = "https://example.com/mirrors.txt",
          .snapshot = "https://A.example.com\nhttps://B.example.com\n",
        },
        {
          .triple = { SPN_ARCH_ARM64, SPN_OS_MACOS },
          .url = "https://example.com/macos.tar.xz",
          .sha256 = "bb",
          .mirrors = "https://example.com/mirrors.txt",
          .snapshot = "https://A.example.com\nhttps://B.example.com\n",
        },
      },
      .targets = {
        { TARGET_WASM },
        { HOST_X64_LINUX_MUSL },
      },
    },
  },
  {
    .name = "detected",
    .file = "detected.toml",
    .expect = {
      .driver = SPN_CC_DRIVER_MSVC,
      .source = SPN_TOOLCHAIN_SOURCE_DETECTED,
      .detect = SPN_TOOLCHAIN_DETECT_MSVC,
      .host_row = true,
      .compiler = { .name = "cl" },
      .cxx = { .name = "cl" },
      .archiver = { .name = "lib" },
    },
  },
};

static const target_test_t target_tests [] = {
  {
    .name = "spelling",
    .file = "spelling.toml",
    .expect = { { TARGET_WASM, .spelling = "wasm32-wasip1" } },
  },
  {
    .name = "exceptions",
    .file = "exceptions.toml",
    .expect = { { TARGET_WASM, .exceptions = { .flags = { "-fA" }, .link_flags = { "-fA", "-lB" } } } },
  },
};

static const issue_test_t issue_tests [] = {
  { .name = "malformed_toml",                      .file = "malformed.toml",      .expect = { SPN_ERR_CODEGEN_PARSE } },
  { .name = "detected_rejects_hosts",              .file = "detected_hosts.toml", .expect = { SPN_ERR_CODEGEN_INVALID, "host" } },
  { .name = "detect_unknown",                      .file = "detect_unknown.toml", .expect = { SPN_ERR_CODEGEN_INVALID, "toolchain[0].detect" } },
  { .name = "detected_rejects_sdk",                .file = "detected_sdk.toml",   .expect = { SPN_ERR_CODEGEN_INVALID, "target[0].sdk" } },
  { .name = "host_row_named_twice",                .file = "host_twice.toml",     .expect = { SPN_ERR_CODEGEN_DUPLICATE_KEY, "target[1].kind" } },
  { .name = "spelling_on_a_driver_without_target", .file = "spelling_fixed.toml", .expect = { SPN_ERR_CODEGEN_INVALID, "target[0].spelling" } },
};

static sp_err_t lower(sp_test_t* t, const c8* file, const spn_toolchain_decl_t** decl) {
  sp_da(spn_toolchain_decl_t) decls = SP_NULLPTR;
  sp_da(spn_toml_issue_t) issues = SP_NULLPTR;
  if (fixture_decls(t, file, &decls, &issues)) {
    return SP_ERR;
  }
  sp_must(t, sp_da_empty(issues));
  sp_must_eq(t, 1, (u32)sp_da_size(decls));
  *decl = &decls[0];
  return SP_OK;
}

static sp_err_t check_launcher(sp_test_t* t, spn_toolchain_launcher_t launcher, fixture_launcher_t expect) {
  if (test_check_arg(t, launcher.program, (test_arg_t) { .name = expect.name, .path = expect.path, .root = expect.root })) {
    return SP_ERR;
  }
  sp_must_strs_eq(t, launcher.args, sp_da_size(launcher.args), expect.args);
  return SP_OK;
}

static sp_err_t check_targets(sp_test_t* t, sp_da(spn_toolchain_target_t) targets, const target_t* expect) {
  u32 count = 0;
  while (count < max_targets && !fixture_triple_empty(expect[count].triple)) {
    count++;
  }
  sp_must_eq(t, count, (u32)sp_da_size(targets));
  sp_for(it, count) {
    sp_expect(t, spn_triple_equal(expect[it].triple, targets[it].triple));
    sp_expect_str_eq_c(t, targets[it].spelling, expect[it].spelling);
    sp_expect_eq(t, expect[it].exceptions.flags[0] || expect[it].exceptions.link_flags[0], targets[it].exceptions.declared);
    sp_must_strs_eq(t, targets[it].exceptions.flags, sp_da_size(targets[it].exceptions.flags), expect[it].exceptions.flags);
    sp_must_strs_eq(t, targets[it].exceptions.link_flags, sp_da_size(targets[it].exceptions.link_flags), expect[it].exceptions.link_flags);
  }
  return SP_OK;
}

sp_test_each(parse, decls, decl_test_t, decl_tests, .setup = spn_test_ctx_setup) {
  const spn_toolchain_decl_t* decl = SP_NULLPTR;
  if (lower(t, it->file, &decl)) {
    return SP_ERR;
  }

  sp_expect_str_eq_c(t, decl->name.str, "A");
  sp_expect_str_eq_c(t, decl->version, it->expect.version);
  sp_expect_eq(t, (u32)it->expect.driver, (u32)decl->driver);
  sp_must_eq(t, (u32)it->expect.source, (u32)decl->source);
  sp_expect_eq(t, it->expect.lld, decl->lld);
  sp_expect_eq(t, it->expect.host_row, decl->host_row);
  if (check_launcher(t, decl->compiler, it->expect.compiler) || check_launcher(t, decl->cxx, it->expect.cxx) || check_launcher(t, decl->archiver, it->expect.archiver)) {
    return SP_ERR;
  }
  sp_must_strs_eq(t, decl->link_args, sp_da_size(decl->link_args), it->expect.link_args);

  switch (decl->source) {
    case SPN_TOOLCHAIN_SOURCE_DETECTED: {
      sp_expect_eq(t, (u32)it->expect.detect, (u32)decl->detect);
      break;
    }
    case SPN_TOOLCHAIN_SOURCE_LOCAL:
    case SPN_TOOLCHAIN_SOURCE_DISTRIBUTION: {
      u32 hosts = 0;
      sp_carr_detect_len(it->expect.hosts, hosts, !fixture_triple_empty(it->expect.hosts[hosts].triple));
      sp_must_eq(t, hosts, (u32)sp_da_size(decl->hosts));
      sp_for(at, hosts) {
        host_t expect = it->expect.hosts[at];
        sp_expect(t, spn_triple_equal(decl->hosts[at].triple, expect.triple));
        sp_expect_str_eq_c(t, decl->hosts[at].artifact.url, expect.url);
        sp_expect_str_eq_c(t, decl->hosts[at].artifact.sha256, expect.sha256);
        sp_expect_str_eq_c(t, decl->hosts[at].artifact.mirror_list, expect.mirrors);
        sp_expect_str_eq_c(t, decl->hosts[at].artifact.mirror_snapshot, expect.snapshot);
      }
      break;
    }
  }

  return check_targets(t, decl->targets, it->expect.targets);
}

sp_test_each(parse, targets, target_test_t, target_tests, .setup = spn_test_ctx_setup) {
  const spn_toolchain_decl_t* decl = SP_NULLPTR;
  if (lower(t, it->file, &decl)) {
    return SP_ERR;
  }
  return check_targets(t, decl->targets, it->expect);
}

sp_test_each(parse, issues, issue_test_t, issue_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_da(spn_toolchain_decl_t) decls = SP_NULLPTR;
  sp_da(spn_toml_issue_t) issues = SP_NULLPTR;
  if (fixture_decls(t, it->file, &decls, &issues)) {
    return SP_ERR;
  }

  sp_must_eq(t, 1, (u32)sp_da_size(issues));
  sp_expect_eq(t, (u32)it->expect.code, (u32)issues[0].code);
  sp_expect_str_eq_c(t, spn_toml_path_str(mem, &issues[0].at), it->expect.path);
  return SP_OK;
}
