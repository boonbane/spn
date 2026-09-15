#include "spn.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPN_CODEGEN_MAX_UNITS 8
#define SPN_CODEGEN_MAX_DIRS 2
#define SPN_CODEGEN_MAX_FILES 1
#define SPN_CODEGEN_MAX_OUTPUTS 4
#define SPN_CODEGEN_PATH_MAX 512

#define countof(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
  const c8* target;
  const c8* units [SPN_CODEGEN_MAX_UNITS];
} consumer_t;

typedef struct {
  const c8* tag;
  const c8* schema;
  const c8* out;
  const c8* dirs [SPN_CODEGEN_MAX_DIRS];
  const c8* files [SPN_CODEGEN_MAX_FILES];
  const c8* outputs [SPN_CODEGEN_MAX_OUTPUTS];
  const consumer_t* consumers;
  u32 num_consumers;
} codegen_t;

// Targets that compile units rendered from source/core/codegen/schema. A target
// with no units only sees the generated include directories.
static const consumer_t core_consumers [] = {
  { "spn",           { "abi", "config", "errors", "events", "manifest", "release", "source_deps" } },
  { "smoke" },
  { "winvm" },
  { "core",          { "config", "errors", "events", "manifest", "release", "source_deps" } },
  { "resolver",      { "config", "errors", "events", "manifest", "release" } },
  { "integration",   { "config", "errors", "events", "source_deps" } },
  { "render",        { "config", "errors", "events", "source_deps" } },
  { "fuzz_dag" },
  { "fuzz_resolver", { "config", "errors", "events", "manifest", "release" } },
  { "toolchain",     { "config", "errors", "events" } },
  { "unit",          { "config", "errors", "events", "source_deps" } },
};

// Targets that compile units rendered from test/tools/schema.
static const consumer_t test_consumers [] = {
  { "winvm",       { "probe" } },
  { "resolver",    { "resolve" } },
  { "integration", { "probe" } },
  { "fuzz_dag",    { "fuzz" } },
};

static const codegen_t codegens [] = {
  {
    .tag = "codegen",
    .schema = "/source/source/core/codegen/schema",
    .out = "codegen/gen",
    .dirs = { "/source/source/core/codegen/schema", "/source/tools/gen/templates" },
    .outputs = {
      "gen/codegen/gen/common.gen.h",
      "gen/codegen/gen/abi.gen.h",
      "gen/codegen/gen/abi.gen.c",
      "gen/include/spn/err.h",
    },
    .consumers = core_consumers,
    .num_consumers = countof(core_consumers),
  },
  {
    .tag = "codegen_test",
    .schema = "/source/test/tools/schema",
    .out = "codegen/test",
    .dirs = { "/source/test/tools/schema", "/source/tools/gen/templates" },
    .files = { "/source/source/core/codegen/schema/common.jtd.json" },
    .consumers = test_consumers,
    .num_consumers = countof(test_consumers),
  },
};

static s32 visible(const struct dirent* entry) {
  return entry->d_name[0] != '.';
}

static const c8* host_source(spn_t* spn, const c8* guest) {
  return spn_get_subdir(spn, SPN_DIR_SOURCE, guest + strlen("/source/"));
}

static void add_inputs(spn_t* spn, spn_node_t* node, const c8* dir) {
  struct dirent** entries = NULL;
  s32 count = scandir(dir, &entries, visible, alphasort);
  for (s32 it = 0; it < count; it++) {
    c8 path [SPN_CODEGEN_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, entries[it]->d_name);
    if (entries[it]->d_type == DT_DIR) {
      add_inputs(spn, node, path);
    } else {
      spn_node_add_input(node, host_source(spn, path));
    }
    free(entries[it]);
  }
  free(entries);
}

static void add_output(spn_node_t* node, const c8* dir, const c8* name, const c8* suffix) {
  c8 path [SPN_CODEGEN_PATH_MAX];
  snprintf(path, sizeof(path), "gen/%s/%s%s", dir, name, suffix);
  spn_node_add_output(node, SPN_DIR_WORK, path);
}

// Must match the union formats dispatched in tools/gen/run.c; union schemas
// render their public header into gen/include/spn/ instead of the node's gen dir.
static void add_schema_outputs(spn_node_t* node, const c8* schema, const c8* out) {
  const c8* suffix = ".jtd.json";
  struct dirent** entries = NULL;
  s32 count = scandir(schema, &entries, visible, alphasort);
  for (s32 it = 0; it < count; it++) {
    const c8* file = entries[it]->d_name;
    u32 len = strlen(file);
    if (len <= strlen(suffix) || strcmp(file + len - strlen(suffix), suffix) || !strcmp(file, "common.jtd.json")) {
      free(entries[it]);
      continue;
    }
    c8 name [SPN_CODEGEN_PATH_MAX];
    snprintf(name, sizeof(name), "%.*s", (s32)(len - strlen(suffix)), file);

    add_output(node, out, name, ".gen.c");
    if (!strcmp(name, "errors") || !strcmp(name, "events")) {
      add_output(node, "include/spn", name, ".h");
    } else {
      add_output(node, out, name, ".gen.h");
      add_output(node, out, name, ".jtd.json");
    }
    free(entries[it]);
  }
  free(entries);
}

static void add_consumers(spn_t* spn, const c8* out, const consumer_t* consumers, u32 count) {
  c8 path [SPN_CODEGEN_PATH_MAX];
  snprintf(path, sizeof(path), "gen/%s", out);
  const c8* include = spn_get_subdir(spn, SPN_DIR_WORK, path);

  for (u32 it = 0; it < count; it++) {
    const consumer_t* consumer = &consumers[it];
    spn_target_t* target = spn_get_target(spn, consumer->target);
    spn_target_add_include(target, include);
    for (u32 ut = 0; ut < SPN_CODEGEN_MAX_UNITS && consumer->units[ut]; ut++) {
      snprintf(path, sizeof(path), "gen/%s/%s.gen.c", out, consumer->units[ut]);
      spn_target_add_source(target, spn_get_subdir(spn, SPN_DIR_WORK, path));
    }
  }
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_target_t* target = spn_get_target(spn, "spn");
  spn_target_embed_file_ex(target, "include/spn.h", "include/spn.h", "u8", "u64");
  spn_target_embed_file_ex(target, "include/spn/core.h", "include/spn/core.h", "u8", "u64");
  spn_target_embed_file_ex(target, spn_get_subdir(spn, SPN_DIR_WORK, "gen/include/spn/err.h"), "include/spn/err.h", "u8", "u64");
  spn_target_embed_file_ex(target, "source/core/toolchain/toolchains.toml", "toolchains.toml", "u8", "u64");
  spn_target_embed_dir_ex(target, "assets/init", "init", "u8", "u64");

  spn_add_include(config, spn_get_subdir(spn, SPN_DIR_WORK, "gen/include"));

  for (u32 it = 0; it < countof(codegens); it++) {
    const codegen_t* codegen = &codegens[it];
    spn_node_t* node = spn_add_node(config, codegen->tag);
    spn_node_set_fn(node, codegen->tag);
    for (u32 dt = 0; dt < SPN_CODEGEN_MAX_DIRS && codegen->dirs[dt]; dt++) {
      add_inputs(spn, node, codegen->dirs[dt]);
    }
    for (u32 ft = 0; ft < SPN_CODEGEN_MAX_FILES && codegen->files[ft]; ft++) {
      spn_node_add_input(node, host_source(spn, codegen->files[ft]));
    }
    for (u32 ot = 0; ot < SPN_CODEGEN_MAX_OUTPUTS && codegen->outputs[ot]; ot++) {
      spn_node_add_output(node, SPN_DIR_WORK, codegen->outputs[ot]);
    }
    add_schema_outputs(node, codegen->schema, codegen->out);
    add_consumers(spn, codegen->out, codegen->consumers, codegen->num_consumers);
  }

  const c8* gen = spn_get_subdir(spn, SPN_DIR_WORK, "gen/codegen/gen");
  c8 flag [SPN_CODEGEN_PATH_MAX];
  snprintf(flag, sizeof(flag), "-DSCHEMA_GEN_DIR=\"%s\"", gen);
  spn_target_add_flag(spn_get_target(spn, "core"), flag);

  return SPN_OK;
}
