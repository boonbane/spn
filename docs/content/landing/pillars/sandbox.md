---
title: Reproducible, sandboxed builds
head: build.c
code: sandbox.c
order: 2
---
Build scripts are written in the same language as your project. `spn` sandboxes them by compiling them to WASM modules. Or, if you prefer, write your build scripts in any language that compiles to WASM.

Build scripts are first class programs. They can have their own dependencies, and are only rerun when the files they read change.
