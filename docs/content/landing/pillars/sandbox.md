---
title: Reproducible, sandboxed builds
head: build.c
code: sandbox.c
order: 3
---
Build scripts are plain C, not a DSL, and use the same packages your project already uses. `spn` compiles them to WebAssembly and runs them in a sandbox inside your build.

The sandbox sees every file a script reads, so build scripts are cached just like compiles. And when you just need to run a Python script so you can build your damn code, it helps with that, too!
