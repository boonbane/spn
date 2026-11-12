#include "A.embed.h"

int main() {
  return G_txt_size == 1 && G_txt[0] == 'G' ? 0 : 1;
}
