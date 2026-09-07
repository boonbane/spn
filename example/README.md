# examples

Every directory is a standalone example. Run it like this:

```sh
cd third_party/lua
spn build
./build/debug/main
```

The top level examples show off `spn` itself:

| Directory | What it shows |
|---|---|
| `codegen` | A build script that generates a source file and adds it to a target |
| `test` | A static library, a binary that links it, and a `[[test]]` target |
| `kernel` | A bare metal image for `freestanding` targets that boots under qemu, prints to the serial port, and powers off |
| `nolibc` | A Linux binary with `abi = "none"`: no libc, raw syscalls |

`third_party/` has one example per library (`flecs`, `fmt`, `imgui`, `lua`, `qrcode`, `raylib`, `sdl3`, `stb`). Each is the smallest program that pulls the package from the index and uses it.

The headless examples cross compile from any host:

```sh
spn build --target x86_64-windows-gnu
spn build --target aarch64-macos
```

The bare metal examples (`kernel`, `nolibc`) pick their target in the manifest, so a plain `spn build` cross compiles. `kernel` runs under qemu (see its README); `nolibc` runs on a Linux host:

```sh
cd nolibc
spn build && ./build/x86_64-linux-none/debug/main
```
