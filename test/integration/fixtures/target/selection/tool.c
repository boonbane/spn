#include <stdio.h>

int main() {
  FILE* file = fopen("ran.txt", "wb");
  if (!file) {
    return 1;
  }
  fputs("tool\n", file);
  fclose(file);
  return 0;
}
