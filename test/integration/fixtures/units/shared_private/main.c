#include "foo.h"
#include "gfx.h"

#if FOO_VERSION != 20
  #error "the root must compile against foo 2.0.0"
#endif

int main() {
  return gfx_foo_version();
}
