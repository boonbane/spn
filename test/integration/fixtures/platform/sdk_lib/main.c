#include <iconv.h>

int main() {
  iconv_t converter = iconv_open("UTF-8", "UTF-16");
  if (converter == (iconv_t)-1) {
    return 1;
  }
  return iconv_close(converter);
}
