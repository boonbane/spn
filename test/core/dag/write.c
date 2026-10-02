#include "dag/dag_test.h"

typedef struct {
  spn_dag_obs_kind_t kind;
  const c8* path;
} obs_t;

typedef struct {
  const c8* path;
  bool tree;
} write_t;

typedef struct {
  bool changes;
} expect_t;

typedef struct {
  const c8* name;
  obs_t obs;
  write_t write;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "file_at",                    .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A/B" },             .expect = { .changes = true } },
  { .name = "file_under",                 .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A/B/C" } },
  { .name = "file_above",                 .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A" } },
  { .name = "file_beside",                .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A/C" } },
  { .name = "file_shared_prefix",         .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A/BC" } },
  { .name = "file_dir_above",             .obs = { SPN_DAG_OBS_FILE, "A/B" },               .write = { "A", .tree = true }, .expect = { .changes = true } },
  { .name = "absent_at",                  .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A/B" },             .expect = { .changes = true } },
  { .name = "absent_under",               .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A/B/C" },           .expect = { .changes = true } },
  { .name = "absent_above",               .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A" } },
  { .name = "absent_beside",              .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A/C" } },
  { .name = "absent_shared_prefix",       .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A/BC" } },
  { .name = "absent_dir_above",           .obs = { SPN_DAG_OBS_ABSENT, "A/B" },             .write = { "A", .tree = true }, .expect = { .changes = true } },
  { .name = "listing_at",                 .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/B" } },
  { .name = "listing_under",              .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/B/C" } },
  { .name = "listing_above",              .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A" } },
  { .name = "listing_beside",             .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/C" } },
  { .name = "listing_shared_prefix",      .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/BC" } },
  { .name = "listing_dir_above",          .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A", .tree = true }, .expect = { .changes = true } },
};

sp_test_each(dag_write, changes, test_t, tests) {
  spn_dag_obs_t obs = {
    .kind = it->obs.kind,
    .path = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_cstr_as_str(it->obs.path) },
  };
  spn_path_t path = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_cstr_as_str(it->write.path) };
  spn_dag_artifact_kind_t kind = it->write.tree ? SPN_DAG_ARTIFACT_KIND_TREE : SPN_DAG_ARTIFACT_KIND_FILE;
  sp_expect_eq(t, it->expect.changes, spn_dag_write_changes(path, kind, &obs));
  return SP_OK;
}
