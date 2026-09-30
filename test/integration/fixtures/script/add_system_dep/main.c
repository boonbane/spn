#include <math.h>
#ifdef _WIN32
  #include <windows.h>
#endif

int main(void) {
  volatile double x = 1.0;
#ifdef _WIN32
  return GetSystemMetrics(SM_CXSCREEN) < 0;
#else
  return sin(x) > 2.0;
#endif
}
