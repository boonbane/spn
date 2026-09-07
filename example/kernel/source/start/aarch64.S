.section .note.GNU-stack,"",%progbits

.section .bss
.balign 16
stack:
.skip 0x4000
stack_top:

.section .text.start
.global _start
_start:
  adrp x0, stack_top
  add x0, x0, :lo12:stack_top
  mov sp, x0
  bl main
  movz x0, #0x0008
  movk x0, #0x8400, lsl #16
  hvc #0
1:
  wfi
  b 1b
