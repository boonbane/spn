#if defined(__wasm__) && !defined(__wasi__)
void _start() {
}
#else
int main(void) {
  return 0;
}
#endif
