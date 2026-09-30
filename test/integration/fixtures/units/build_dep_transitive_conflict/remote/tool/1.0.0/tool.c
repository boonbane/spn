#include "foo.h"
#include "tool.h"

#if FOO_VERSION != 10
  #error "the build dep must compile against foo 1.0.0"
#endif

int tool_value(void) {
  return foo_version() + 100;
}
