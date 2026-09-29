#include "../compiler.h"

#define wasm_syms_max 4

typedef struct {
  const c8* rendered;
} wasm_expect_t;

typedef struct {
  const c8* name;
  spn_rsp_style_t style;
  const c8* symbols [wasm_syms_max];
  wasm_expect_t expect;
} wasm_test_t;

static const wasm_test_t tests [] = {
  {
    .name = "symbols",
    .style = SPN_RSP_STYLE_WINDOWS,
    .symbols = { "A", "B" },
    .expect = {
      .rendered = "-Wl,--export=A\n-Wl,--export=B\n",
    },
  },
  {
    .name = "no_symbols",
    .style = SPN_RSP_STYLE_WINDOWS,
    .expect = {
      .rendered = "",
    },
  },
  {
    .name = "quoted_gnu",
    .style = SPN_RSP_STYLE_GNU,
    .symbols = { "a b" },
    .expect = {
      .rendered = "\"-Wl,--export=a b\"\n",
    },
  },
};

sp_test_each(exports_wasm, render, wasm_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  sp_da(sp_str_t) symbols = sp_da_new(mem, sp_str_t);
  sp_carr_for(it->symbols, s) {
    if (!it->symbols[s]) break;
    sp_da_push(symbols, sp_cstr_as_str(it->symbols[s]));
  }

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(mem, &buf);
  spn_rsp_render(&buf.base, &spn.roots, it->style, spn_gnu_render_exports(mem, symbols));
  sp_str_t result = sp_io_dyn_mem_writer_as_str(&buf);

  sp_test_kv(t, "rendered", result);
  sp_expect_str_eq_c(t, result, it->expect.rendered);

  return SP_OK;
}
