# From any host
spn build --target x86_64-windows-gnu
spn build --target aarch64-macos
spn build --target aarch64-linux-musl

# On Windows, from any shell
spn build --toolchain msvc
