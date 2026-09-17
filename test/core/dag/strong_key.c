#include "dag/dag_test.h"

typedef struct {
  const c8* prelim;
  const c8* pinned;
  dag_test_obs_t obs [DAG_TEST_MAX_INPUTS];
} action_t;

typedef struct {
  bool equal;
} expect_t;

typedef struct {
  const c8* name;
  action_t a;
  action_t b;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "identical_folds_match",
    .a = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" }, { "io.h", "IO" } } },
    .b = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" }, { "io.h", "IO" } } },
    .expect = { .equal = true }
  },
  {
    .name = "discovered_content_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "sp.h", "v1" } } },
    .b = { .prelim = "cc main.c", .obs = { { "sp.h", "v2" } } },
  },
  {
    .name = "root_kind_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "H", "SP", .root = SPN_PATH_ROOT_PROJECT } } },
    .b = { .prelim = "cc main.c", .obs = { { "H", "SP", .root = SPN_PATH_ROOT_STORE } } },
  },
  {
    .name = "discovered_path_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "inc1/sp.h", "SP" } } },
    .b = { .prelim = "cc main.c", .obs = { { "inc2/sp.h", "SP" } } },
  },
  {
    .name = "prelim_changes_key",
    .a = { .prelim = "cc -O0 main.c", .obs = { { "sp.h", "SP" } } },
    .b = { .prelim = "cc -O2 main.c", .obs = { { "sp.h", "SP" } } },
  },
  {
    .name = "discovered_order_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" }, { "io.h", "IO" } } },
    .b = { .prelim = "cc main.c", .obs = { { "io.h", "IO" }, { "sp.h", "SP" } } },
  },
  {
    .name = "extra_discovered_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" } } },
    .b = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" }, { "io.h", "IO" } } },
  },
  {
    .name = "empty_differs_from_folded",
    .a = { .prelim = "cc main.c" },
    .b = { .prelim = "cc main.c", .obs = { { "sp.h", "SP" } } },
  },
  {
    .name = "obs_kind_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "inc1/sp.h", SP_NULLPTR, SPN_DAG_OBS_ABSENT } } },
    .b = { .prelim = "cc main.c", .obs = { { "inc1/sp.h", SP_NULLPTR, SPN_DAG_OBS_FILE } } },
  },
  {
    .name = "probe_now_present_changes_key",
    .a = { .prelim = "cc main.c", .obs = { { "inc1/sp.h", SP_NULLPTR, SPN_DAG_OBS_ABSENT }, { "inc2/sp.h", "SP" } } },
    .b = { .prelim = "cc main.c", .obs = { { "inc1/sp.h", "SP", SPN_DAG_OBS_ABSENT }, { "inc2/sp.h", "SP" } } },
  },
  {
    .name = "enumeration_filter_changes_key",
    .a = { .prelim = "publish", .obs = { { "inc", "M", SPN_DAG_OBS_ENUMERATION, "*.h" } } },
    .b = { .prelim = "publish", .obs = { { "inc", "M", SPN_DAG_OBS_ENUMERATION, "*.c" } } },
  },
  {
    .name = "enumeration_membership_changes_key",
    .a = { .prelim = "publish", .obs = { { "inc", "M1", SPN_DAG_OBS_ENUMERATION, "*.h" } } },
    .b = { .prelim = "publish", .obs = { { "inc", "M2", SPN_DAG_OBS_ENUMERATION, "*.h" } } },
  },
  {
    .name = "pinned_digest_changes_key",
    .a = { .prelim = "cc main.c", .pinned = "P1", .obs = { { "sp.h", "SP" } } },
    .b = { .prelim = "cc main.c", .pinned = "P2", .obs = { { "sp.h", "SP" } } },
  },
  {
    .name = "pinned_digest_matches",
    .a = { .prelim = "cc main.c", .pinned = "P1", .obs = { { "sp.h", "SP" } } },
    .b = { .prelim = "cc main.c", .pinned = "P1", .obs = { { "sp.h", "SP" } } },
    .expect = { .equal = true }
  },
};

static spn_dag_digest_t build_key(const action_t* spec) {
  spn_dag_obs_t obs [DAG_TEST_MAX_INPUTS] = sp_zero;
  spn_dag_digest_t digests [DAG_TEST_MAX_INPUTS] = sp_zero;
  u32 count = dag_test_obs_build(spec->obs, DAG_TEST_MAX_INPUTS, obs, digests);
  return spn_dag_strong_key(dag_test_digest(spec->prelim), dag_test_digest(spec->pinned), obs, digests, count);
}

sp_test_each(dag_strong_key, fold, test_t, tests) {
  spn_dag_digest_t a = build_key(&it->a);
  spn_dag_digest_t b = build_key(&it->b);
  sp_expect_eq(t, it->expect.equal, spn_dag_digest_equal(a, b));
  return SP_OK;
}
