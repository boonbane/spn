#include "spn_test.h"

#include "index/index.h"
#include "intern/intern.h"
#include "lock/lock.h"

typedef struct {
  si_index_pin_kind_t kind;
  const c8* rev;
} expect_t;

typedef struct {
  const c8* name;
  spn_index_protocol_t protocol;
  spn_index_format_t format;
  const c8* rev;
  struct { const c8* url; const c8* rev; } held;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "releases_have_no_pin",
    .held = { .url = "U", .rev = "R" },
  },
  {
    .name = "local_manifests_have_no_pin",
    .protocol = SPN_INDEX_PROTOCOL_DIR,
    .format = SPN_INDEX_FORMAT_MANIFESTS,
  },
  {
    .name = "unlocked_manifests_ask_for_the_tip",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .expect = { .kind = SI_INDEX_PIN_TIP },
  },
  {
    .name = "lock_holds_the_pin",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .held = { .url = "U", .rev = "R" },
    .expect = { .kind = SI_INDEX_PIN_HELD, .rev = "R" },
  },
  {
    .name = "declared_rev_outranks_the_lock",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .rev = "S",
    .held = { .url = "U", .rev = "R" },
    .expect = { .kind = SI_INDEX_PIN_FROZEN, .rev = "S" },
  },
  {
    .name = "pin_for_another_url_is_ignored",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .held = { .url = "V", .rev = "R" },
    .expect = { .kind = SI_INDEX_PIN_TIP },
  },
};

sp_test_each(index, pin, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_intern_str_t name = spn_intern_lit("I");

  spn_index_info_t index = {
    .id = name.id,
    .name = name,
    .protocol = it->protocol,
    .format = it->format,
  };
  if (it->protocol == SPN_INDEX_PROTOCOL_GIT) {
    index.git.url = sp_str_lit("U");
    index.git.rev = sp_cstr_as_str(it->rev ? it->rev : "");
  }

  spn_lock_t kept = spn_lock_new(mem);
  if (it->held.url) {
    spn_lock_pin_index(&kept, (spn_lock_index_t) {
      .name = name,
      .url = sp_cstr_as_str(it->held.url),
      .rev = sp_cstr_as_str(it->held.rev),
    });
  }

  si_index_pin_t pin = si_index_pin(&index, &kept);

  sp_expect_eq(t, (u32)it->expect.kind, (u32)pin.kind);
  sp_expect_str_eq_c(t, pin.rev, it->expect.rev ? it->expect.rev : "");
  return SP_OK;
}
