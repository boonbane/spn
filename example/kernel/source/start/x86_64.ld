ENTRY(_start)
SECTIONS {
  . = 0x100000;
  .text : { *(.head) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .note : { *(.note .note.*) }
  .eh_frame_hdr : { *(.eh_frame_hdr) }
  .eh_frame : { *(.eh_frame) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) }
}
