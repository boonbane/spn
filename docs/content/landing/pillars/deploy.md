---
title: Zero dependency, single file binary
head: sh
code: deploy.sh
order: 2
---
`spn` is a single file static executable. It runs on Linux, macOS, and Windows, and has zero dependencies.

It bundles the Zig toolchain, which means that it can cross compile from any host to nearly any other host, even when no compiler is installed.

It has first class support for nearly every common toolchain, including MSVC, GCC, Clang, Xcode, mingw64, wasi-sdk, ARM GCC, w64devkit, and more!
