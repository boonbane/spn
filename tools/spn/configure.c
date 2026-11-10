#include "spn.h"

#define SPN_CODEGEN_MAX_OUTPUTS 20

#define countof(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
  const c8* tag;
  const c8* outputs [SPN_CODEGEN_MAX_OUTPUTS];
} codegen_t;

static const codegen_t codegens [] = {
  {
    .tag = "codegen",
    .outputs = {
      "/work/gen/codegen/gen/abi.gen.c",
      "/work/gen/codegen/gen/common.gen.h",
      "/work/gen/codegen/gen/config.gen.c",
      "/work/gen/codegen/gen/config.gen.h",
      "/work/gen/codegen/gen/config.jtd.json",
      "/work/gen/codegen/gen/errors.gen.c",
      "/work/gen/codegen/gen/errors.guest.h",
      "/work/gen/codegen/gen/events.gen.c",
      "/work/gen/codegen/gen/manifest.gen.c",
      "/work/gen/codegen/gen/manifest.gen.h",
      "/work/gen/codegen/gen/manifest.jtd.json",
      "/work/gen/codegen/gen/release.gen.c",
      "/work/gen/codegen/gen/release.gen.h",
      "/work/gen/codegen/gen/release.jtd.json",
      "/work/gen/codegen/gen/source_deps.gen.c",
      "/work/gen/codegen/gen/source_deps.gen.h",
      "/work/gen/codegen/gen/source_deps.jtd.json",
      "/work/gen/include/spn/err.h",
      "/work/gen/include/spn/errors.h",
      "/work/gen/include/spn/events.h",
    },
  },
  {
    .tag = "codegen_test",
    .outputs = {
      "/work/gen/codegen/test/fuzz.gen.c",
      "/work/gen/codegen/test/fuzz.gen.h",
      "/work/gen/codegen/test/fuzz.jtd.json",
      "/work/gen/codegen/test/probe.gen.c",
      "/work/gen/codegen/test/probe.gen.h",
      "/work/gen/codegen/test/probe.jtd.json",
      "/work/gen/codegen/test/resolve.gen.c",
      "/work/gen/codegen/test/resolve.gen.h",
      "/work/gen/codegen/test/resolve.jtd.json",
    },
  },
};

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  for (u32 it = 0; it < countof(codegens); it++) {
    const codegen_t* codegen = &codegens[it];
    spn_node_t* node = spn_add_node(config, codegen->tag);
    spn_try(spn_node_set_fn(node, codegen->tag));
    for (u32 ot = 0; ot < SPN_CODEGEN_MAX_OUTPUTS && codegen->outputs[ot]; ot++) {
      spn_try(spn_node_add_output(node, codegen->outputs[ot]));
    }
  }

  return SPN_OK;
}
