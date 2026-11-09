#include "A.embed.h"

int main() {
  return G_H_txt_size == 1 && G_H_txt[0] == 'H' ? 0 : 1;
}
