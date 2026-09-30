#include "foo.h"
#include "gfx.h"

#if FOO_VERSION != 10
  #error "the private dep must compile against foo 1.0.0"
#endif

int gfx_foo_version(void) {
  return foo_version();
}
