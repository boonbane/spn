#ifndef SPN_TEST_H
#define SPN_TEST_H

// The spn-flavoured half of the harness: everything here needs spn's global
// context to exist. Tests that only exercise a leaf module should include
// "unit.h" directly instead, so their target does not have to link ctx,
// intern and the event buffer.

#include "unit.h"
#include "fixture.h"

#include "ctx/types.h"
#include "event/types.h"
#include "toolchain/types.h"
#include "codegen/types.h"
#include "paths/types.h"
#include "hash/digest/digest.h"
#include "hash/sha256/sha256.h"

sp_err_t spn_test_ctx_setup(sp_test_t* t);
sp_da(spn_event_t) spn_test_drain_errs(sp_mem_t mem);
void     spn_test_lower_toolchains(sp_test_t* t, sp_str_t toml, spn_path_root_t base, sp_da(spn_toolchain_decl_t)* decls, sp_da(spn_toml_issue_t)* issues);
sp_err_t spn_test_builtin_toml(sp_test_t* t, sp_str_t* toml);
sp_err_t spn_test_builtin_catalog(sp_test_t* t, spn_toolchain_catalog_t* catalog, spn_triple_t host);

static inline sp_str_t test_sha256_hex(sp_mem_t mem, sp_str_t data) {
  spn_sha256_ctx_t ctx = sp_zero;
  spn_sha256_init(&ctx);
  spn_sha256_update(&ctx, (const u8*)data.data, data.len);
  u8 digest [32] = sp_zero;
  spn_sha256_final(&ctx, digest);
  return spn_digest_hex(mem, digest);
}

#endif
