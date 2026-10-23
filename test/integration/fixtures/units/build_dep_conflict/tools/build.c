#include "spn.h"

#include "foo.h"

#if FOO_VERSION != 10
  #error "the build script must compile against foo 1.0.0"
#endif
