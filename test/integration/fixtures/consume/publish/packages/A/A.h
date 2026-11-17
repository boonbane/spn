#include <math.h>
#ifdef _WIN32
  #include <windows.h>
#endif

static inline int a_value() {
  volatile double x = A_VALUE;
  volatile double y = 0.5;
#ifdef _WIN32
  if (GetSystemMetrics(SM_CXSCREEN) < 0) {
    return 0;
  }
#endif
  return (int)(pow(x, y) + 0.5);
}
