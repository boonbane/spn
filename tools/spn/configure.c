#include "spn.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPN_CODEGEN_MAX_UNITS 8
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

static s32 scan(spn_t* spn, const c8* dir, struct dirent*** entries) {
  s32 count = scandir(dir, entries, visible, alphasort);
  if (count < 0) {
    c8 message [SPN_CODEGEN_PATH_MAX + 64];
    snprintf(message, sizeof(message), "scandir(%s): %s", dir, strerror(errno));
    spn_log(spn, message);
  }
  return count;
}

static spn_err_t add_inputs(spn_t* spn, spn_node_t* node, const c8* dir) {
  struct dirent** entries = NULL;
  s32 count = scan(spn, dir, &entries);
  if (count < 0) return SPN_ERROR;

  spn_err_t err = SPN_OK;
  for (s32 it = 0; it < count; it++) {
    c8 path [SPN_CODEGEN_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, entries[it]->d_name);
    if (entries[it]->d_type == DT_DIR) {
      if (add_inputs(spn, node, path)) err = SPN_ERROR;
    } else {
      spn_node_add_input(node, host_source(spn, path));
    }
    free(entries[it]);
  }
  free(entries);
  return err;
}

static void add_output(spn_node_t* node, const c8* dir, const c8* name, const c8* suffix) {
  c8 path [SPN_CODEGEN_PATH_MAX];
  snprintf(path, sizeof(path), "gen/%s/%s%s", dir, name, suffix);
  spn_node_add_output(node, SPN_DIR_WORK, path);
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

    if (add_inputs(spn, node, codegen->schema)) return SPN_ERROR;
    if (add_inputs(spn, node, "/source/tools/gen/templates")) return SPN_ERROR;
    for (u32 ft = 0; ft < SPN_CODEGEN_MAX_FILES && codegen->files[ft]; ft++) {
      spn_node_add_input(node, host_source(spn, codegen->files[ft]));
    }
    for (u32 ot = 0; ot < SPN_CODEGEN_MAX_OUTPUTS && codegen->outputs[ot]; ot++) {
      spn_node_add_output(node, SPN_DIR_WORK, codegen->outputs[ot]);
    }

    // Must match the union formats dispatched in tools/gen/run.c; union schemas
    // render their public header into gen/include/spn/ instead of the node's gen dir.
    const c8* suffix = ".jtd.json";
    struct dirent** schemas = NULL;
    s32 count = scan(spn, codegen->schema, &schemas);
    if (count < 0) return SPN_ERROR;
    for (s32 st = 0; st < count; st++) {
      const c8* file = schemas[st]->d_name;
      u32 len = strlen(file);
      if (len <= strlen(suffix) || strcmp(file + len - strlen(suffix), suffix) || !strcmp(file, "common.jtd.json")) {
        free(schemas[st]);
        continue;
      }
      c8 name [SPN_CODEGEN_PATH_MAX];
      snprintf(name, sizeof(name), "%.*s", (s32)(len - strlen(suffix)), file);

      add_output(node, codegen->out, name, ".gen.c");
      if (!strcmp(name, "errors") || !strcmp(name, "events")) {
        add_output(node, "include/spn", name, ".h");
      } else {
        add_output(node, codegen->out, name, ".gen.h");
        add_output(node, codegen->out, name, ".jtd.json");
      }
      free(schemas[st]);
    }
    free(schemas);

    c8 path [SPN_CODEGEN_PATH_MAX];
    snprintf(path, sizeof(path), "gen/%s", codegen->out);
    const c8* include = spn_get_subdir(spn, SPN_DIR_WORK, path);
    for (u32 ct = 0; ct < codegen->num_consumers; ct++) {
      const consumer_t* consumer = &codegen->consumers[ct];
      spn_target_t* consumer_target = spn_get_target(spn, consumer->target);
      spn_target_add_include(consumer_target, include);
      for (u32 ut = 0; ut < SPN_CODEGEN_MAX_UNITS && consumer->units[ut]; ut++) {
        snprintf(path, sizeof(path), "gen/%s/%s.gen.c", codegen->out, consumer->units[ut]);
        spn_target_add_source(consumer_target, spn_get_subdir(spn, SPN_DIR_WORK, path));
      }
    }
  }

  spn_target_add_define_path(spn_get_target(spn, "core"), "SCHEMA_GEN_DIR", SPN_DIR_WORK, "gen/codegen/gen");

  return SPN_OK;
}
