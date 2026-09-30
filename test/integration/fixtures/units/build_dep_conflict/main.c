#include "foo.h"

#if FOO_VERSION != 20
  #error "the root must compile against foo 2.0.0"
#endif

int main() {
  return 0;
}
