---
title: Metaprograms
---

You need to run code in your build. Most people solve this by writing brittle scripts in Bash or Powershell, or, at best, a language like Python. In `spn`, these scripts (*metaprograms*) are regular C programs. A *build* script runs inside your build, alongside things like compilation and linking:

```c
#include "spn.h"
#include "sqlite3.h"

SPN_EXPORT s32 seed_database(spn_t* spn) {
  const c8* schema = spn_io_read_file(spn, "/source/schema.sql");

  sqlite3* db;
  c8* error;
  sqlite3_open("/work/seed.db", &db);
  if (sqlite3_exec(db, schema, NULL, NULL, &error)) {
    spn_log(spn, error);
    return 1;
  }

  sqlite3_close(db);
  return 0;
}
```

A *configure* script runs before your build, and can mutate the build itself:

```c
// configure.c
#include "spn.h"

SPN_EXPORT spn_err_t configure(spn_t* spn, spn_config_t* config) {
  // Add a library on Windows
  if (spn_profile_get_os(spn_get_profile(spn)) == SPN_OS_WINDOWS) {
    spn_add_system_dep(config, "ws2_32");
  }

  // Run a piece of code in the build which outputs version.h
  spn_node_t* node = spn_add_node(config, "version");
  spn_node_set_fn(node, "gen_version");
  spn_node_add_output(node, SPN_DIR_WORK, "version.h");

  // Add an include directory
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));
  return SPN_OK;
}
```

These are regular C programs, not a DSL or some subset of C. Metaprograms are regular code in the same language as your project, with access to the same dependencies and ecosystem. That's how it ought to be! In the [words of the great Matklad](https://matklad.github.io/2024/03/22/basic-things.html#Build-CI):

> A common anti-pattern is to write these sorts of automations in bash and Python, but that’s almost pure technical debt. These ecosystems are extremely finnicky in and of themselves, and, crucially (unless your project itself is written in bash or Python), they are a second ecosystem to what you already have in your project for “normal” code.
>
> But releasing software is also just code, which you can write in your primarly language. The right tool for the job is often the tool you are already using. It pays off to explicitly attack the problem of glue from the start, and to pick/write a library that makes writing subprocess wrangling logic easy.

## Sandboxing

Despite being arbitrary C code, `spn` does *not* allow metaprograms to run arbitrary, untrusted native code. Instead, it embeds a WebAssembly[^1] runtime, and will automatically compile arbitrary C programs to WASM modules that run in the build graph.

Metaprograms have access to a guest API (as opposed to the [host API](/docs/embedding) you'd use when you use `spn` as a library).

## Dependencies

Build scripts can have arbitrary dependencies. They can link to SQLite, parse JSON, read and write files, and so on. They're linked to wasi-libc, a version of musl libc.

Packages must be compilable as WASM to be linked into a metaprogram. In practice

## Phases

### configure

### build

## Writing a script

## The sandbox

## Script dependencies

## API

If you're porting over an existing build, you can continue to use what you have.

### Targets

### Files and directories

### Custom nodes

### Embedding files

### Logging

## Example: code generation

[^1]: WebAssembly is a platform agnostic binary target; instead of compiling code for x86_64 or ARM64 machine code and running it with your CPU, you compile it to WASM bytecode and run it inside a regular program.
