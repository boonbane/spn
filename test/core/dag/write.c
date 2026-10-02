#include "dag/dag_test.h"

typedef struct {
  spn_dag_obs_kind_t kind;
  const c8* path;
  const c8* filter;
} obs_t;

typedef struct {
  const c8* path;
  bool tree;
} write_t;

typedef struct {
  spn_err_t err;
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
  { .name = "listing_child_unfiltered",   .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/B/C" },           .expect = { .changes = true } },
  { .name = "listing_above",              .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A" } },
  { .name = "listing_beside",             .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/C" } },
  { .name = "listing_shared_prefix",      .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/BC" } },
  { .name = "listing_dir_above",          .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A", .tree = true }, .expect = { .changes = true } },
  { .name = "listing_child_admitted",     .obs = { SPN_DAG_OBS_ENUMERATION, "A/B", "*.h" }, .write = { "A/B/C.h" },         .expect = { .changes = true } },
  { .name = "listing_child_rejected",     .obs = { SPN_DAG_OBS_ENUMERATION, "A/B", "*.h" }, .write = { "A/B/C.c" } },
  { .name = "listing_grandchild",         .obs = { SPN_DAG_OBS_ENUMERATION, "A/B" },        .write = { "A/B/C/D" } },
  { .name = "listing_dir_child_rejected", .obs = { SPN_DAG_OBS_ENUMERATION, "A/B", "*.h" }, .write = { "A/B/C", .tree = true }, .expect = { .changes = true } },
  { .name = "listing_child_bad_filter",   .obs = { SPN_DAG_OBS_ENUMERATION, "A/B", "[" },   .write = { "A/B/C" },           .expect = { .err = SPN_ERR_DAG_GLOB } },
};

sp_test_each(dag_write, changes, test_t, tests) {
  spn_dag_obs_t obs = {
    .kind = it->obs.kind,
    .path = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_cstr_as_str(it->obs.path) },
    .filter = sp_cstr_as_str(it->obs.filter),
  };
  spn_path_t path = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_cstr_as_str(it->write.path) };
  spn_dag_artifact_kind_t kind = it->write.tree ? SPN_DAG_ARTIFACT_KIND_TREE : SPN_DAG_ARTIFACT_KIND_FILE;
  bool changes = false;
  sp_expect_eq(t, it->expect.err, spn_dag_write_changes(path, kind, &obs, &changes));
  sp_expect_eq(t, it->expect.changes, changes);
  return SP_OK;
}
