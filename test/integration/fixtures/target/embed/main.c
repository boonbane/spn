#include "main.embed.h"

static int str_equal(const char* a, const char* b) {
  while (*a && *b) {
    if (*a != *b) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == *b;
}

int main() {
  if (spn_embed_count != 2) {
    return 1;
  }
  if (A_txt_size != 1 || A_txt[0] != 'A') {
    return 2;
  }
  if (B_A_txt_size != 1 || B_A_txt[0] != 'A') {
    return 3;
  }
  if (!str_equal(spn_embed_manifest[0].path, "A.txt")) {
    return 4;
  }
  if (!str_equal(spn_embed_manifest[1].path, "B/A.txt")) {
    return 5;
  }
  return 0;
}
