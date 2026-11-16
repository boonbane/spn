#include "git.h"

typedef struct {
  const c8* url;
  const c8* rev;
  const c8* dir;
  sp_hash_t patches;
} request_t;

typedef struct {
  const c8* name;
  request_t a;
  request_t b;
} test_t;

static const test_t tests [] = {
  {
    .name = "different_dir",
    .a = { .url = "https://A/B", .rev = "C" },
    .b = { .url = "https://A/B", .rev = "C", .dir = "D" },
  },
  {
    .name = "different_url",
    .a = { .url = "https://A/B", .rev = "C" },
    .b = { .url = "https://D/B", .rev = "C" },
  },
  {
    .name = "different_patches",
    .a = { .url = "https://A/B", .rev = "C", .patches = 0xdead },
    .b = { .url = "https://A/B", .rev = "C", .patches = 0xbeef },
  },
};

static spn_git_checkout_id_t checkout_id(request_t request) {
  return (spn_git_checkout_id_t) {
    .url = sp_cstr_as_str(request.url),
    .rev = sp_cstr_as_str(request.rev),
    .dir = sp_cstr_as_str(request.dir),
    .patches.hash = request.patches,
  };
}

sp_test_each(git_key, checkout_key, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t ka = spn_git_checkout_key(mem, checkout_id(it->a));
  sp_str_t kb = spn_git_checkout_key(mem, checkout_id(it->b));
  sp_expect(t, !sp_str_equal(ka, kb));
  return SP_OK;
}

// checkout key: golden. Existing global caches depend on unpatched keys never
// changing; an implementation that folds a zero patch part into the hash (or
// otherwise perturbs the formula) breaks every cache dir on disk.
sp_test(git_key, checkout_key_golden) {
  sp_str_t key = spn_git_checkout_key(sp_test_arena(t), checkout_id((request_t) {
    .url = "https://A/B", .rev = "C"
  }));
  sp_expect_str_eq_c(t, key, "B-c6e0966bfdb21106");
  return SP_OK;
}
