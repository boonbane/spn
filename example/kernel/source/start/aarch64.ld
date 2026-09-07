ENTRY(_start)
SECTIONS {
  . = 0x40100000;
  .text : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .eh_frame_hdr : { *(.eh_frame_hdr) }
  .eh_frame : { *(.eh_frame) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) }
}
