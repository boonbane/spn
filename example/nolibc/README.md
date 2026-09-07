This example produces a Linux binary that doesn't link to `libc`. In `spn` terms, the ABI part of the target triple is `none`. Pass `--target x86_64-linux-none` or `--target aarch64-linux-none` to cross compile, and notice how the source codeuses `SPN_TARGET_ARCH` to produce the correct syscall.

```sh
spn build
./build/x86_64-linux-none/debug/main
```
