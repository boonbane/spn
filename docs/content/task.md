---
title: Task runner
---

## Tool targets

## Tool dependencies

## spn path

## Vendoring


## Running tests

- `spn test` builds every `[[test]]` target and runs each one. `spn test NAME...` runs only the ones you name.
- A test passes when it exits `0`.
- Each test runs from the package's source directory, with no arguments and no stdin.
- Output is shown only for a test that fails.
- Every test runs even after one fails, and `spn test` exits `1` if any did.
- To pass arguments to a test, run its binary yourself from `test/` in the build directory.
- `spn test` takes `-p`, `-m`, `--opt`, `--sanitize`, and `--toolchain`, but not `--target`: it can't run a cross compiled test.
- `spn build` compiles tests without running them.
