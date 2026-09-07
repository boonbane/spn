#if defined(__x86_64__)
static void serial_putc(char c) {
  __asm__ volatile ("outb %0, %1" : : "a"(c), "Nd"((unsigned short)0x3f8));
}
#elif defined(__aarch64__)
static void serial_putc(char c) {
  *(volatile unsigned char*)0x09000000 = c;
}
#endif

static void serial_puts(const char* s) {
  while (*s) {
    serial_putc(*s++);
  }
}

int main() {
  serial_puts("hello from kernel\n");
  return 0;
}
