This example produces a bare metal image. Whereas the [nolibc](../nolibc) example produces a binary that's ABI agnostic, this example produces a binary that's ABI *and* OS agnostic. The OS signifier in the triple is `freestanding` (e.g. `aarch64-freestanding-none` or `x86_64-freestanding-none`), and the binary is called `main.elf` instead of `main`.

In addition to providing an entry point like `libc` would normally do for you, this example must also place the image at the correct place in memory like the kernel would normally do for you[^1]. It does this using a linker script, which `spn` supports natively and simply.

# Build

As always, build with one command to target the host triple, or cross compile with `--target`:

```sh
spn build
spn build --target aarch64-freestanding-none
```

# Run

Both images run under qemu; all they do is write a line to the serial port and then power the machine off. These programs, and the command lines below to run them, are taken verbatim from the qemu repository.
- `source/start/x86_64.S` is qemu's [`tests/tcg/x86_64/system/boot.S`](https://gitlab.com/qemu-project/qemu/-/blob/master/tests/tcg/x86_64/system/boot.S) (I guess this is GPL 2.0 or later, but I figured that's all right)
- `source/start/aarch64.S` follows the OSDev wiki's [QEMU AArch64 Virt Bare Bones](https://wiki.osdev.org/QEMU_AArch64_Virt_Bare_Bones)

```sh
qemu-system-x86_64 -display none -serial stdio -kernel build/x86_64-freestanding-none/debug/main.elf
qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -kernel build/aarch64-freestanding-none/debug/main.elf
```

[^1]: Which makes sense, because this example is literally a kernel.
