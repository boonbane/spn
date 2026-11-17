#include "A.h"

#if defined(A_X)
  #error "the tool dep's option request reached the package dep"
#endif

int main() {
  return 0;
}
