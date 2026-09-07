---
title: Metaprograms
---

You need to run code in your build. Most people solve this by writing brittle scripts in Bash or Powershell, or, at best, a language like Python. In `spn`, these scripts (*metaprograms*) are regular C programs. They come in two forms:
- *Configure*, which modify the parameters of your build before any compilation happens
- *Build*, which do actual work inside your build's execution, alongside compilation

A configure metaprogram might be used to programmatically add an include directory when some conditions are true, whereas a build metaprogram might link to a JSON parser and generate C code from schemas on disk during your build.

On this page, we'll walk through a basic example and then look at the finer points of how `spn` sandboxes metaprogram execution and what that means for you, the programmer.

## Declaring a metaprogram

Metaprograms are defined using the same fields as any other target, under the keys `package.build` and `package.configure`. Build metaprograms can have arbitrary dependencies, listed under `deps.build`, which are linked into the metaprogram only.

```toml
[package.configure]
source = ["tools/configure.c"]

[package.build]
source = ["tools/build.c"]

[deps.build]
yyjson = "0.14.0"
```

## Writing a metaprogram

A metaprogram is just a regular C program which exports certain functions. The most basic configure metaprogram includes `spn.h` and exports a function named `configure` with the correct signature:

```c
#include "spn.h"

SPN_EXPORT s32 configure(spn_t* spn, spn_config_t* config) {
  return SPN_OK;
}
```

`spn.h` is the *guest API*; it declares types and functions to be used from metaprograms run inside the host `spn`[^2]. `spn` embeds all the headers it needs to compile metaprograms inside the binary itself, so you never need to worry about the availability or compatibility of `spn.h`. It'll automatically point to the correct header.

`spn_t` and `spn_config_t` are opaque handles used by the API; the former for APIs that are callable at both configure time and build time.

`SPN_EXPORT` is a preprocessor definition which ensures `configure` is exported in the binary. You'll also use this to export functions from your build metaprogram. Let's write a build metaprogram that generates a header, and then we'll fill in `configure` to tell the build to call it.

## Build time

A *build* metaprogram runs inside your build, alongside things like compilation and linking. You might generate some C code from JSON schemas, or seed a database for your tests. Build metaprograms may be complex or perform a lot of compute (but are usually still simple).

Here's an example of a build-time metaprogram that links to `yyjson` and `libc` and generates a header, `config.h`, based on the value of `config.json`:

```c
#include "spn.h"

#include "yyjson.h"
#include <stdio.h>

SPN_EXPORT s32 generate_config(spn_t* spn) {
  yyjson_doc* doc = yyjson_read_file("/source/config.json", 0, NULL, NULL);
  if (!doc) return 1;

  yyjson_val* foo = yyjson_obj_get(yyjson_doc_get_root(doc), "foo");

  c8 header [64];
  snprintf(header, sizeof(header), "#define CONFIG_FOO %d\n", yyjson_get_int(foo));
  spn_io_write("/work/config.h", header);

  return SPN_OK;
}
```

Take note of the paths. Metaprograms are sandboxed, and are given access to the following paths:
- `/source`, the project (or, for a dependency, a clean checkout of the project)
- `/work`, a scratch directory
- `/manifest`, the project that contains the manifest (e.g. if writing a `sqlite` package that obviously doesn't live in the actual SQLite repository, this points at the checkout of the manifest's repo)

Every call to one of these functions is run in its own sandbox. `static` data isn't shared. There's no need to free memory, either.

## Configure time

Now that we have `generate_config`, we need to tell `spn` to call it. Add a node to your build, and give it the function by name.

```c
SPN_EXPORT spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "config");
  spn_node_set_fn(gen, "generate_config");
  spn_node_add_output(gen, SPN_DIR_WORK, "config.h");
  return SPN_OK;
}
```

Now, there's the question of ordering. If `foo.c` can't compile until `config.h` exists,

Here's how you could run your function inside the build:

```c
#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "config");
  spn_node_set_fn(gen, "generate_config");
  spn_node_add_input(gen, spn_get_subdir(spn, SPN_DIR_SOURCE, "config.json"));
  spn_node_add_output(gen, SPN_DIR_WORK, "config.h");

  // Make the generated header visible to the compiler
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));
  return SPN_OK;
}
```

## Sandboxing

Despite being arbitrary C code, `spn` does *not* allow metaprograms to run arbitrary, untrusted native code. Instead, it embeds a WebAssembly[^1] runtime, and will automatically compile arbitrary C programs to WASM modules that run in the build graph.

If you're not sure what that means or how to use it, that's OK! The important thing is that you write build scripts as regular C code, `spn` automatically compiles them for you and runs them in a sandbox, and WebAssembly is what provides the sandbox.

Metaprograms have access to a guest API (as opposed to the [host API](/docs/embedding) you'd use when you use `spn` as a library).

## Dependencies and libc

Build metaprograms can have arbitrary dependencies. They can link to SQLite, parse JSON, read and write files, and so on. They're linked to `wasi-libc`, which is based on musl libc. These dependencies are listed in `deps.build`, and a package should be declared in `deps.build` and `deps.package`, for instance, if it's needed by both the program and metaprogram.

Metaprograms are linked to `wasi-libc`, which is a version of musl libc adapted to the sandboxed WASM environment. Code that unconditionally expects to run natively on a proper OS (e.g. code that spawns threads, opens DLLs, or uses sockets) won't work. Nearly all libraries that one would expect to run inside a build work without a hitch.

If the units of work in your build simply cannot function without networking or OS threads, you can write such code however you prefer, and simply run `spn` once you have C code ready to be compiled.

## Writing build scripts in C is awesome

These are regular C programs, not a DSL or some subset of C. Metaprograms are regular code in the same language as your project, with access to the same dependencies and ecosystem. That's how it ought to be! In the [words of the great Matklad](https://matklad.github.io/2024/03/22/basic-things.html#Build-CI):

> A common anti-pattern is to write these sorts of automations in bash and Python, but that’s almost pure technical debt. These ecosystems are extremely finnicky in and of themselves, and, crucially (unless your project itself is written in bash or Python), they are a second ecosystem to what you already have in your project for “normal” code.
>
> But releasing software is also just code, which you can write in your primarly language. The right tool for the job is often the tool you are already using. It pays off to explicitly attack the problem of glue from the start, and to pick/write a library that makes writing subprocess wrangling logic easy.

Writing build scripts in C has traditionally been painful almost entirely because libc is not a suitable interface for any program, let alone high level programs like build scripts. Even if you spurn [modern, portable, ergonomic alternatives to the C standard library](https://github.com/tspader/sp) in favor of this self-flagellation, I urge the thoughtful reader to consider the benefits of throwing away the quirks of Bash, the nonportability of Powershell, the deployment nightmare of Python, and the fuzzy type systems of all of the above in favor of something simple, close to the OS, and statically typed!

## API

### Targets

### Files and directories

### Custom nodes

### Embedding files

### Logging

[^1]: WebAssembly is a platform agnostic binary target; instead of compiling code for x86_64 or ARM64 machine code and running it with your CPU, you compile it to WASM bytecode and run it inside a regular program.

[^2]: If you'd like to link to `libspn` and drive the core yourself, that's a [separate *host* API](/docs/embedding).
