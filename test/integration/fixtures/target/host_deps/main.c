#include <zlib.h>

int main(int num_args, const char** args) {
  return zlibVersion() == 0;
}
