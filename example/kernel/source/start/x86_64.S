/*
 * x86_64 boot and support code
 *
 * Copyright 2019, 2024 Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Unlike the i386 version we instead use Xen's PVHVM booting header
 * which should drop us automatically into 32 bit mode ready to go. I've
 * nabbed bits of the Linux kernel setup to achieve this.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Vendored from qemu tests/tcg/x86_64/system/boot.S at 73294d096cc5 (11.1.50).
 * Trimmed to the boot path: the isa-debug-exit exit code, the port 0xE9 serial
 * helper, and the unused IDT are gone. Output and exit live in main.c and the
 * ACPI poweroff below. The notes are allocated ("a") so lld emits a PT_NOTE.
 */

        .section .head

#define ELFNOTE_START(name, type, flags)	\
.pushsection .note.name, flags,@note	;	\
  .balign 4				;	\
  .long 2f - 1f		/* namesz */	;	\
  .long 4484f - 3f	/* descsz */	;	\
  .long type				;	\
1:.asciz #name				;	\
2:.balign 4				;	\
3:

#define ELFNOTE_END				\
4484:.balign 4				;	\
.popsection				;

#define ELFNOTE(name, type, desc)		\
	ELFNOTE_START(name, type, "a")		\
		desc			;	\
	ELFNOTE_END

#define XEN_ELFNOTE_ENTRY          1
#define XEN_ELFNOTE_HYPERCALL_PAGE 2
#define XEN_ELFNOTE_VIRT_BASE      3
#define XEN_ELFNOTE_PADDR_OFFSET   4
#define XEN_ELFNOTE_PHYS32_ENTRY  18

#define __ASM_FORM(x)	x
#define __ASM_SEL(a,b)  __ASM_FORM(b)
#define _ASM_PTR	__ASM_SEL(.long, .quad)

	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR 0x100000)
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR _start)
	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY,   _ASM_PTR _start)    /* entry == virtbase */
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)

       /*
	* Entry point for PVH guests.
	*
	* Xen ABI specifies the following register state when we come here:
	*
	* - `ebx`: contains the physical memory address where the loader has placed
	*          the boot start info structure.
	* - `cr0`: bit 0 (PE) must be set. All the other writable bits are cleared.
	* - `cr4`: all bits are cleared.
	* - `cs `: must be a 32-bit read/execute code segment with a base of ‘0’
	*          and a limit of ‘0xFFFFFFFF’. The selector value is unspecified.
	* - `ds`, `es`: must be a 32-bit read/write data segment with a base of
	*               ‘0’ and a limit of ‘0xFFFFFFFF’. The selector values are all
	*               unspecified.
	* - `tr`: must be a 32-bit TSS (active) with a base of '0' and a limit
	*         of '0x67'.
	* - `eflags`: bit 17 (VM) must be cleared. Bit 9 (IF) must be cleared.
	*             Bit 8 (TF) must be cleared. Other bits are all unspecified.
	*
	* All other processor registers and flag bits are unspecified. The OS is in
	* charge of setting up it's own stack, GDT and IDT.
	*/
        .code32
        .section .text

.global _start
_start:
	cld
        lgdt gdtr

        ljmp $0x8,$.Lloadcs
.Lloadcs:
        mov $0x10,%eax
        mov %eax,%ds
        mov %eax,%es
        mov %eax,%fs
        mov %eax,%gs
        mov %eax,%ss

	/* Enable PAE mode (bit 5). */
	mov %cr4, %eax
	btsl $5, %eax
	mov %eax, %cr4

#define MSR_EFER		0xc0000080 /* extended feature register */

	/* Enable Long mode. */
	mov $MSR_EFER, %ecx
	rdmsr
	btsl $8, %eax
	wrmsr

	/* Enable paging */
	mov $.Lpml4, %ecx
	mov %ecx, %cr3

	mov %cr0, %eax
	btsl $31, %eax
	mov %eax, %cr0

	/* Jump to 64-bit mode. */
        lgdt gdtr64
        ljmp $0x8,$.Lenter64

        .code64
        .section .text
.Lenter64:


	// Setup stack ASAP
	movq $stack_end,%rsp

        /* don't worry about stack frame, assume everything is garbage when we return */
	call main

_exit:
1:      /* QEMU ACPI poweroff */
	mov $0x604,%edx
	mov $0x2000,%eax
	out %ax,%dx
	hlt
	jmp 1b

	/*
	 * Global Descriptor Table (GDT)
	 *
	 * This describes various memory areas (segments) through
	 * segment descriptors. In 32 bit mode each segment each
	 * segment is associated with segment registers which are
	 * implicitly (or explicitly) referenced depending on the
	 * instruction. However in 64 bit mode selectors are flat and
	 * segmented addressing isn't used.
	 */
        .section .data
        .align 16

gdt:
        .short 0
gdtr:
        .short gdt_en - gdt - 1
        .int gdt

        // Code cs:
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x9b
        .byte 0xCF
        .byte 0

        // Data  ds:, ss:, es:, fs:, and gs:
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x93
        .byte 0xCF
        .byte 0
gdt_en:

gdt64:
        .short 0
gdtr64:
        .short gdt64_en - gdt64 - 1
        .int gdt64

        // Code
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x9b
        .byte 0xAF
        .byte 0

        // Data
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x93
        .byte 0xCF
        .byte 0
gdt64_en:

	.section .bss
        .align 16

stack: .space 65536
stack_end:

	.section .data

.align 4096
.Lpd:
i = 0
        .rept 512 * 4
        .quad 0x1e7 | (i << 21)
        i = i + 1
        .endr

.align 4096
.Lpdp:
        .quad .Lpd + 7 + 0 * 4096 /* 0-1 GB */
        .quad .Lpd + 7 + 1 * 4096 /* 1-2 GB */
        .quad .Lpd + 7 + 2 * 4096 /* 2-3 GB */
        .quad .Lpd + 7 + 3 * 4096 /* 3-4 GB */

.align 4096
.Lpml4:
        .quad .Lpdp + 7 /* 0-512 GB */
