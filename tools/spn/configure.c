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
      "gen/codegen/gen/abi.gen.c",
      "gen/codegen/gen/common.gen.h",
      "gen/codegen/gen/config.gen.c",
      "gen/codegen/gen/config.gen.h",
      "gen/codegen/gen/config.jtd.json",
      "gen/codegen/gen/errors.gen.c",
      "gen/codegen/gen/errors.guest.h",
      "gen/codegen/gen/events.gen.c",
      "gen/codegen/gen/manifest.gen.c",
      "gen/codegen/gen/manifest.gen.h",
      "gen/codegen/gen/manifest.jtd.json",
      "gen/codegen/gen/release.gen.c",
      "gen/codegen/gen/release.gen.h",
      "gen/codegen/gen/release.jtd.json",
      "gen/codegen/gen/source_deps.gen.c",
      "gen/codegen/gen/source_deps.gen.h",
      "gen/codegen/gen/source_deps.jtd.json",
      "gen/include/spn/err.h",
      "gen/include/spn/errors.h",
      "gen/include/spn/events.h",
    },
  },
  {
    .tag = "codegen_test",
    .outputs = {
      "gen/codegen/test/fuzz.gen.c",
      "gen/codegen/test/fuzz.gen.h",
      "gen/codegen/test/fuzz.jtd.json",
      "gen/codegen/test/probe.gen.c",
      "gen/codegen/test/probe.gen.h",
      "gen/codegen/test/probe.jtd.json",
      "gen/codegen/test/resolve.gen.c",
      "gen/codegen/test/resolve.gen.h",
      "gen/codegen/test/resolve.jtd.json",
    },
  },
};

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  for (u32 it = 0; it < countof(codegens); it++) {
    const codegen_t* codegen = &codegens[it];
    spn_node_t* node = spn_add_node(config, codegen->tag);
    spn_try(spn_node_set_fn(node, codegen->tag));
    for (u32 ot = 0; ot < SPN_CODEGEN_MAX_OUTPUTS && codegen->outputs[ot]; ot++) {
      spn_try(spn_node_add_output(node, work, codegen->outputs[ot]));
    }
  }

  return SPN_OK;
}
