<p align="center">
  <a href="https://spn.spader.zone">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="assets/logo/logo-dark.svg">
      <img src="assets/logo/logo-light.svg" alt="spn" width="180">
    </picture>
  </a>
</p>

<p align="center">
  <a href="https://discord.gg/7v4C5Kwsp7">
    <img src="https://img.shields.io/discord/957469344974143508?style=flat-square&label=discord" />
  </a>
  <a href="https://github.com/boonbane/spn/actions/workflows/ci.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/boonbane/spn/ci.yml?style=flat-square&branch=main" />
  </a>
</p>

# About

`spn` is the missing everything tool for writing and building C code:
- An extremely fast build system with a built-in object cache that's designed for CI
- TOML manifests for defining projects
- A package manager that supports everything from "pin the world to exact commits" to "treat C like JS"
- Sandboxed, hermetic builds that never run arbitrary native code

It ships as a single file static executable, or an embeddable library with a stable C ABI. It runs natively on Linux, macOS, and Windows and it supports most common toolchains (MSVC, GCC, Clang, MinGW, Zig) and can cross compile to any target your toolchain supports.

If any part of your build pipeline would benefit from fast, cached, reproducible compilation of C code backed by tools that are obsessed with making your life easier, then you're in the right place!

**spn is in alpha**. Join [our Discord](https://discord.gg/7v4C5Kwsp7) if you'd like to chat, need help, or want to scream into the void that I'm wasting my life.

# Quickstart

## Install

### macOS & Linux

```sh
curl -fsSL https://spn.spader.zone/install | sh
```

### Windows

```powershell
irm https://spn.spader.zone/install.ps1 | iex
```

## Create a project

Let's use the following program, which needs to link to Lua, as an example:

```c
#include <stdio.h>
#include "lauxlib.h"
#include "lualib.h"

int main(void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  if (luaL_dofile(L, NULL) != LUA_OK) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    return 1;
  }
  lua_close(L);
  return 0;
}
```

## Add a dependency

First, initialize your project. We'll pull Lua from [the default package index](https://github.com/tspader/spam) for now; later, we'll talk about making your own packages.

```sh
spn init
spn add lua
```

Those commands create a project manifest, `spn.toml`, which defines a single `[[bin]]` executable.

```toml
[package]
name = "demo"
version = "0.1.0"

[[bin]]
name = "demo"
source = ["main.c"]

[deps.package]
lua = "5.4.6"

```

## Build and run

Then, after copying the source code into `main.c`:

```sh
spn build
./build/debug/demo
```

## Cross compile

Cross compiling works out of the box. The following commands produce `build/x86_64-windows-gnu/debug/main.exe`, `build/aarch64-macos/debug/main`, and a bare metal `build/aarch64-freestanding-none/debug/main.elf` with no libc or startup files:

```sh
spn build --target x86_64-windows-gnu
spn build --target aarch64-macos
spn build --target aarch64-freestanding-none
```

Congratulations! You now have a C program which:
- Can be compiled to any OS, architecture, and ABI
- Builds extremely quickly and incrementally

## Editor setup

`spn` generates `compile_commands.json` out of the box, with no instrumentation needed.

# Overview

Building C code should be this easy, always. This demo is just a fraction of the functionality `spn` offers. It can replace nearly every tool in what is today a patchwork ecosystem of developer tooling:
- Build tools like CMake, Make, and Ninja
- Package managers like Conan, vcpkg, pkg-config, or "whatever's on my distro"
- Build caches like ccache or sccache
- Binary tools like xxd, patchelf, and objcopy
- Compilation database tooling like bear or compiledb
- Toolchain managers like crosstool-NG, dockcross, CMake toolchain files

Let's take a look at everything it can do in more detail!

# Packages

Packages are defined by TOML manifests.

## Targets

A package can define as many executables, libraries, tests, and examples as it would like. These are called *targets*. They have fields like `source`, or `include`, or `system_deps`, and most fields can be configured at the package level for all targets or for an individual target.

```toml
[package]
include = ['common']

[[lib]]
source = ['source/lib.c']
include = ['source/lib']

[[example]]
source = ['example/foo/main.c']

[[test]]
source = ['test/main.c', 'test/foo.c']
define = ['SOMETHING']
system_deps = ['m']
```

A `[[bin]]` and a `[[script]]`, for instance, are both executables, but their difference isn't strictly cosmetic. `[[bin]]` entries are taken to be *exports* of your package, able to be pulled in by consumers. `[[script]]` entries aren't. `[[test]]` entries are executed in `spn test`, and compiled to `test/` instead of `bin/`.

## Paths

Paths may not contain relative components like `.` and `..`, with the exception of the literal `"."`.

## Target fields

### source

### include and publish

### define and flags

### system_deps

### deps

### kinds

### cxx

## System dependencies



## Conditional configuration

Any field in your package can be keyed on any fact of the build (target OS, architecture, ABI, build mode, optimization level, sanitizer settings). Clauses are structured data, not a DSL, and an entry with multiple clauses gets them ANDed together. Here's some common examples:

```toml
source = [
  { path = "source/backend/win32.c", when = { os = "windows" } },
  { path = "source/backend/posix.c", when = { os = { not = "windows" } } },
]
flags = [
  { value = "-mfpu=neon", when = { os = "linux", arch = "aarch64" } },
]
define = [
  { value = "USE_DEBUG_ALLOC", when = { mode = "debug", sanitize_address = false } },
]
system_deps = [
  { value = "pthread", when = { os = { not = "windows" } } },
]
deps = [
  { pkg = "tracy", when = { mode = "debug" } },
]
```

### when clauses

### Fact keys

### Options as keys

Options declared in the manifest work as keys, too, so a feature flag can gate sources and dependencies together:

```toml
[options.freetype]
type = "bool"
default = true
define = "UI_FREETYPE"

[[lib]]
name = "ui"
source = [
  { path = "source/text_freetype.c", when = { freetype = true } },
]
deps = [
  { pkg = "freetype", when = { freetype = true } },
]
```

## Options

Packages provide options. Options can be enumerations or booleans; enums are mutually exclusive, and an unresolvable conflict is a build error. Booleans are additive. For example:

```toml
[options.tls]
type = "enum"
values = ["schannel", "openssl", "off"]
default = [
  { when = { os = "windows" }, value = "schannel" },
  { when = { os = { not = "wasi" } }, value = "openssl" },
  { value = "off" },
]

[options.zstd]
type = "bool"
default = false
```

Consumers can then [set these options](#options-1)

## Platforms

### macOS

```toml
[package.macos]
min_os = { major = 12 }

[lib.macos]
frameworks = ["Cocoa", "IOKit", "CoreVideo", "OpenGL"]
```

When linking, your binary's `min_os` is the max across everything in it. If a dependency needs macOS 12, your binary targets macOS 12.

### Windows

```toml
[[bin]]
# ...
windows = { subsystem = "windows" }
```

## Embedding files

`spn` can embed arbitrary files and bytes in your binary by creating an object file and header that anything can link to trivially. Each entry names a file (or, with `dir = true`, every file under a directory) and the path it is embedded as; the symbol is that path with `/`, `\\`, `.`, and `-` replaced by `_`. For example, this configuration:

```toml
[[bin]]
name = "whatever"
source = ["main.c"]
embed = [
  "asset/fonts/inconsolata.ttf",
  { path = "asset/data.json", dest = "data.json", data_type = "u8", size_type = "u64" },
  { path = "asset/shaders", dir = true, dest = "shaders" },
]
```

Produces this header:

```c
extern const unsigned char asset_fonts_inconsolata_ttf [109384];
extern const unsigned long long asset_fonts_inconsolata_ttf_size;

extern const u8 data_json [2213];
extern const u64 data_json_size;

extern const unsigned char shaders_quad_frag [412];
extern const unsigned long long shaders_quad_frag_size;

extern const unsigned char shaders_quad_vert [287];
extern const unsigned long long shaders_quad_vert_size;

typedef struct {
  const char* path;
  const void* data;
  unsigned long long size;
} spn_embed_entry_t;
static const unsigned int spn_embed_count = 4;
static const spn_embed_entry_t spn_embed_manifest[] = {
  { "asset/fonts/inconsolata.ttf", asset_fonts_inconsolata_ttf, 109384 },
  { "asset/data.json", data_json, 2213 },
  { "shaders/quad.frag", shaders_quad_frag, 412 },
  { "shaders/quad.vert", shaders_quad_vert, 287 },
};
```

# Dependencies

`spn` is a real package manager, built specifically for C and C++.

## Local dependencies

The simplest dependency is a path to a directory with an `spn.toml`:

```toml
[deps.package]
foo = { path = "packages/foo" }
```

And this is what the package might look like:

```toml
[package]
name = "foo"
version = "6.9.0"

[[lib]]
name = "foo"
source = ["foo.c"] # Relative to foo's manifest
```

This is just an old fashioned vendored dependency. You could download LLVM, check in the entire source tree, and have an `spn.toml` that builds this local copy. If you then regained your sanity, you can point the exact same manifest at Git and `spn` will manage the checkout for you *exactly* as if it were an "official" package:

```toml
[package.upstream]
url = "https://github.com/tspader/foo.git"
commit = "6937fa02243da7b693c5692cea84a696950d4669"
```

## Conditional and private dependencies

## Configuration

### Options

Packages can [provide options](#options), either as an enumeration or a boolean. Options for `foo` are set in `[config.foo]`, like this:

```toml
[config.foo]
tls = "openssl"
zstd = true
```

### Linkage

Libraries declare the linkages they support:
- `shared`
- `static`
- `source`, which compiles the package's sources directly into your target as if they were your own files.

```toml
[config.foo]
kind = "shared"
```

## Patching

What if you need to patch `foo`? You don't want to go back to the technology of the ancients, vendoring or submoduling entire repositories just to change a few lines. You want a specific commit with just a few changes. Do this in your `spn.toml`:

```toml
[deps.package]
foo = "6.9.0"

[patch.foo]
files = ["patches/foo.patch"]
```

That's it! Patches fold into the build system like everything else. If you change the contents of a patch, add or remove patches, you'll still get a correct, incremental build.

Your dependencies can't patch. If you ask for a given commit, you get exactly that commit.

## System dependencies

A *system dependency* is a mirage. It's possible to build against system dependencies reliably on *some* system, but to encompass the quirks and patches of every distro (just to speak of Linux) is a fool's errand.

But you didn't come here for a lecture! Sometimes, you just need to link to something on your damn system.

When people talk about *system dependencies*, they mean two separate things:
1. Linking to a shared library which is resolved at runtime. This is totally fine.
2. Compiling against whatever headers happen to be lying around. This is bad.

The first case is sometimes not just acceptable, but *principled*, regardless of whether you want to. You might link to the system `libcurl.so` to pull in security patches, for example. Or to benefit from bugfixes long after you abandon your program to pursue a career in professional bass fishing. Therefore, you can specify `system_deps` in your manifest:

```toml
[package]
name = "foo"
system_deps = [
  { lib = "pthread", when = { os = "linux" } },
  { lib = "shell32", when = { os = "windows" } }
]

[[lib]]
name = "bar"
system_deps = ["m"]
```

This does what you'd expect, e.g. `-lpthread`. Linking to whatever's on the system comes with plenty of problems, but it's an important tool in your toolbelt.

The second case, however, is almost always bad. Linking to the system makes you unpredictable at *runtime*, but compiling against the system makes you unpredictable at *compile time*. An object file compiled against slightly different versions of `curl.h` are completely different. Your build cache dies. And, at best, you compiled against a header which is ABI compatible with the `libcurl.so` on someone else's machine.

There I go lecturing again! I don't mean to tell anyone that the way they work is wrong. Not everyone needs truly hermetic builds. All I mean to say is that after all the the insane distro wrangling and ABI mess that comes with discovering system headers, `spn` would *still* produce a binary that's fundamentally broken more often than not. If your constraints tolerate this, you can always do this:

```toml
[package]
name = "foo"
include = ["/usr/include"]
```

## The lockfile

# Building

`spn build` compiles your project and all dependencies. Compilation targets a *triple* (e.g. `x86_64-windows-gnu`) and uses a *profile* (e.g. release, O2, statically linked).

## Build output

By default, spn outputs to `build/`. If you explicitly pass a triple, it uses `build/triple/profile`. Otherwise, it uses `build/profile`. If you were to run this:

```bash
spn build
spn build --target x86_64-windows-gnu
```

It would produce this:

```
build/
├── debug/
└── x86_64-windows-gnu/
    └── debug/
```

Inside a given build, each executable is placed at the top level beside the shared libraries it needs, so it runs from there and the directory ships as-is. Tests and examples get the same treatment under `test/` and `example/`. Every package, including your dependencies, has its own prefix under `store/` holding the headers, libraries, and binaries it produced. Finally, a JSONL file with a detailed trace of the build is in `.spn/build.jsonl`

```
build/debug/
├── main.exe
├── whatever.dll
├── store/
│   ├── my-package/
│   │   └── bin/
│   │       └── main.exe
│   └── whatever/
│       ├── lib/
│       │   └── whatever.dll
│       └── include/
│           ├── whatever.h
│           └── ...
└── .spn/
    └── build.jsonl
```

## Selecting targets

### Profiles

Packages are compiled against a profile, which contains the target triple, toolchain, build mode, optimization level, sanitizers, etc. `spn` bakes in two profiles, `debug` and `release`, and reserves `[profile.default]` as the base fields which every profile uses unless overridden.

```toml
[profile.default]
toolchain = "zig"
linkage = "static"
standard = "c11"
mode = "debug"

[profile.debug]
opt = "2"

[profile.foo]
abi = { value = "gnu", when = { os = "linux" } }
toolchain = [
  { value = "clang", when = { os = "macos" } },
  { value = "gcc" },
]
```

Here, `debug` applies optimization on top of the default profile, and `foo` overrides the toolchain and ABI. Lists are tried in order such that the first match wins and an ungated entry is the fallback.

When building:
- `spn build` uses `debug`
- `spn build --mode release` uses `release`
- `spn build --profile NAME` uses the named `[[profile]]`

### Target triples

`spn` has two goals when it comes to what you specify in your build:
1. Either produce a correct binary or fail (i.e. no footguns). Never guess.
2. Be very ergonomic for common cases

Because of (1), you must give a full triple (e.g. `x86_64-linux-gnu`) when cross compiling. We never infer facts about a cross compile from the host that builds it, because we have no information as to which ABI is preferred. Silently picking GNU over musl, for example, has no basis.

Because of (2), you can omit the ABI when you aren't crossing. This has some really useful properties:
- By default, `spn build` always produces a binary that runs on the host
- Statically linked executables choose musl for a true self contained binary, regardless of your host
- You don't need Visual Studio installed to use `spn`!

### Linkage

By default, `spn build` produces the most self contained binary that it can. There are three things that programs link to that affect their portability[^1]:

| Field | What it links | How it can break |
|---|---|---|
| `linkage` | Your dependencies (e.g `libcurl`)| If a program links to the system's `libcurl.so`, then it won't run on a system without it|
| `runtime` | Runtime support libraries (e.g. `libstdc++`, `libgcc`, or the CRT on Windows)| The ubiquitous "redistributables" problem on Windows, `libstdc++` on old Ubuntu, Yocto/Buildroot minimal systems |
| `libc` | The C standard library | The mess of distributing Linux binaries that spawned entire ecosystems of tools like AppImage or Snap |

Specify them like this:

```toml
[profile.whatver]
linkage = "static"
runtime = "static"
libc = "shared"
```

Different platforms have different stances on some or all of these knobs, and there's a huge variety in what's needed across different kinds of C programs. `spn` allows you to toggle each between `static` and `shared`, plus toggle individual dependencies. Where the platform or toolchain doesn't support one of these, `spn` errors appropriately. For example:
- macOS forces you to link dynamically to `libSystem.dylib` (i.e. libc)
- GNU's libc is famously antagonistic to static linkage, so `libc = "static"` requires (and infers) musl
- Some sanitizers require the loader to be available, and produce an error otherwise

Generally, though, we aim to allow you to express any build you might need. Let's look at some examples:

| Build | Dependencies | Runtime | Libc | ABI | |
|---|---|---|---|---|---|
| A Linux binary that runs anywhere | `static` | `static` | `static` | `musl`| This is a completely self contained binary that will run on any Linux system you'll find in the wild; `spn` does this by default |
| A portable Linux video game       | any | `static` | `shared` | `gnu`| Games need Vulkan or OpenGL, which must be used from a shared library, but otherwise bundle everything else they need inside the binary |
| A Windows binary which doesn't need redistributables | any | `static` | `static` | `msvc` | Like `/MT`; The UCRT, vcruntime, and the C++ STL all live inside the executable |
| A plugin that gets loaded by a DAW | `shared` | `shared` | `shared` |  | You *want* to share the host's runtime, so that an e.g. `std::string` can cross between your DLL and the host cleanly |
| A fully sanitized build (ASan, TSan, MSan)|  |  | `shared` | any | These sanitizers don't work without a loader |


### Modes and optimization

### Sanitizers

## Cross compilation

### Target triples

### --os, --arch, --abi

## CI

`spn` was built for CI from day one:
- The build cache is designed to scale to your entire team
- First class cross compilation destroys your insane matrix of runners
- Out of the box, zero configuration integrations with GitHub Actions
- The CLI can output a structured JSONL stream. You never have to scrape an error message
- If it doesn't work how you want, write your own CLI that links to `libspn` instead
- @spader Are these weak? People care about fast builds mostly but we got that elsewhere...

## Caching

spn has a built in build cache which caches any intermediate artifact that your build creates, like `ccache` or `sccache`. Builds are incremental by default. That doesn't mean "incremental on my machine". That means *incremental*. Build `foo.exe` on one machine, and the cache is designed such that *any* subsequent machine building `foo.exe` can see a fully cached build.

That's because, at its core, `spn` is a *content addressed DAG*. If you've never seen the terms, let's take a ride!

### Incremental builds

This means that everything in your build is turned into nodes in a graph. If something in the middle of the graph changes, we know exactly what needs to be rebuilt and in what order. This is like every other incremental build system that has ever existed.

### Content addressing

If you use CMake, you've probably encountered this:

```sh
# Build once. After this, builds are incremental. You are happy.
make

# Ah, but you gotta check out another branch
git checkout whatever
git checkout main

# This is a full rebuild, because Git touched every file and made it
# appear to have been edited since the last build. You are sad.
make
```

That's because CMake, and in fact most build systems, lie to you. They tell you that they know when `foo.c` changed. But they have no god damn idea! None whatsoever! Now, they have *proxies* which in practice are...fine. Like, for example, a file's mtime. Last build was at 3:00, `foo.c` says 3:05, let's rebuild it.

But files get touched *all the time*! For no reason! Sometimes, they even go *backwards*, like when you decompress an archive, and then your build is wrong instead of just slow. This is the first problem: Traditional build systems have trouble knowing when something changed.

There's a beautiful solution to both of these things at once! If you're having trouble giving everything a unique identity, and you're having trouble figuring out when a given thing changed, *make their identity be their content*. When we build `foo.o`, we hash its bytes. Let's say that hash comes out to, miraculously, `0x69`. Now, there's no such thing as `foo.o`; there's just a file called `0x69` in the cache.

That's great, but the next build still needs to be able to know that, ah, yes, we need the cache entry keyed at `0x69`. To do this, we look at all of the inputs to `foo.o`:
- `foo.c`, of course
- Let's say that the compiler reported that `stdint.h` was used, too

We hash their content too, and get `0x420` and `0x5F3759DF`. Then, all we do is write down a fact:

> If the inputs to the compiler are `0x420` and `0x5F3759DF`, then the output will be `0x69`

Next time, when we're ready to build `foo.o`, we have all of its inputs ready to go. Take those inputs, and ask the fact machine if it knows the answer for that set of inputs. If `foo.c` was edited, the inputs are no longer (`0x420`, `0x5F3759DF`). If `spum.h` got added to the build, then there are now three inputs rather than two. And, if the inputs are the same but `0x69` isn't in the cache, all you have to do is rebuild!

This is beautiful. Identity is content; content is identity. A file doesn't have a name. It simply *is*. There are, of course, many kinks to work out in such a system, but they're all tractable.

This is the exact principle behind Bazel, BuildXL, and Nix, and it's the fundamental reason why `spn` is so good at caching your builds across machines.

### --force

## Running tests

## Output

### compile_commands.json

### JSON event stream

[^1]: There are more, of course, like the mess of versioned symbols in glibc. But for our purposes, three is fine.

# Metaprograms

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

Metaprograms have access to a guest API (as opposed to the [host API](#libspn) you'd use when you use `spn` as a library).

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

# Toolchains

## Defaults

## Zig

## Probing

## Bring your own

### [[toolchain]]

### Hosted artifacts

### Target support

## MSVC

# Package indexes

When you run `spn add lua`, a version appears from the mist. Where does it come from? And what happens when I want to maintain my own versions of Lua, or some private package?

## Directory indexes

A *package index* is just metadata about what packages exist, and what versions are available. The simplest possible index is just a directory of packages:

```
packages
├── flecs
│   └── spn.toml
├── fmt
│   └── spn.toml
├── imgui
│   └── spn.toml
├── sdl2
│   ├── spn.c
│   └── spn.toml
├── sdl2_mixer
│   └── spn.toml
└── tracy
    └── spn.toml
```

Point your build at it like this, or enable it globally in `~/.config/spn/spn.toml`:

```toml
[[index]]
name = "randy"
path = "./packages"
```

If you only care about one version existing at a time, this is all you need. If you just want to build your personal libraries, or use some common packages in different projects, you're done.

## Git indexes

Let's say you have a project that uses `sqlite==3.51.0`. You like it. But then, Richard Hipp returns from an ayahuasca retreat having had a vision: `4.0`. You can prompt an LLM from *inside* a query, to say the least.

You want to start a new project and use it, but you still need `3.51.0`. You've graduated to a second directory. This time, it's a directory of JSONL metadata:

```
index
└── sqlite.jsonl
```

Each JSONL entry is a release specifying the version, where the source code is, where the manifest is, etc.

```json
{
  "name": "sqlite",
  "version": "4.0.0",
  "yanked": false,
  "source": {
    "url": "https://github.com/sqlite/sqllm.git",
    "rev": "a1de0694c693a5d741b2b424e5dfea45eaa30e69"
  },
  "manifest": {
    "url": "https://github.com/you/manifests.git",
    "rev": "c7faa527fd",
    "dir": "sqlite"
  },
  "paths": {
    "manifest": "spn.toml",
    "script": "spn.c"
  }
}
```

Note the `manifest` field. If `sqllm` decided that `spn` is the tool of the future, and decided to have a *first party* manifest that lives in their repo, this field would point to `sqlite/sqllm.git`. But since we're writing a package for a third party library that doesn't know we exist, we point it at a repository that *we* own. These entries are created with `spn publish`, not by hand.

Use the index the same way as the simpler one:

```toml
[[index]]
name = "randy"
url = "git@github.com:you/index.git"
```

Let's dig into that a little more, because it's important: **Package indexes are just Git repositories**. You're going to *really* like this:
  - Commits are transactional, reversible, and everything else Git does for your source code
  - It comes with provenance and auth that you already have set up
  - Every god damn developer on the planet knows how to use it

If you've ever used Homebrew and waited multiple tens of seconds for kegs to be sterilized, you may balk at this. "Spader," you might say, "Git doesn't scale for package indexes". And you'd be right! But you'd also be wrong:
- `spn` isn't tied to Git. Index backends are just interfaces; there's a stubbed out HTTP backend in there already if you want to host it on Cloudflare or whatever
- C is not JS. It's not a distro, it's not Rust. Nobody uses packages in C because there are no good package managers for all its special little quirks. We're talking orders (plural) of magnitude less scale.

## Global configuration

## Publishing

### spn publish

### Namespaces

## The ecosystem

Here's some of the packages which have native, high quality `spn` manifests. The full list is in [the package manifest repository](https://github.com/tspader/spam). If a package is on this list, it has full support. That means you can cross compile it, pull in its optional dependencies, build its examples, and so on. Everything.
- [Clay](https://github.com/nicbarker/clay)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [flecs](https://github.com/SanderMertens/flecs)
- [fmt](https://github.com/fmtlib/fmt)
- [FreeType](https://github.com/freetype/freetype)
- [Lua](https://github.com/lua/lua)
- [mbedtls](https://github.com/Mbed-TLS/mbedtls)
- [ninja](https://github.com/ninja-build/ninja)
- [raylib](https://github.com/raysan5/raylib)
- [SDL2](https://github.com/libsdl-org/SDL)
- [SDL2_mixer](https://github.com/libsdl-org/SDL_mixer)
- [SDL3](https://github.com/libsdl-org/SDL)
- [SDL3_image](https://github.com/libsdl-org/SDL_image)
- [SDL3_shadercross](https://github.com/libsdl-org/SDL_shadercross)
- [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf)
- [sp.h](https://github.com/tspader/sp) :^)
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- [stb](https://github.com/nothings/stb)
- [Tracy](https://github.com/wolfpld/tracy)
- [yyjson](https://github.com/ibireme/yyjson)

# libspn

## There's a stable C ABI

## It works with any event loop

## It's so much better at hotloading than your crappy script

# Workspaces

## `spn` is a task runner

## `spn` is a test runner

# Why do I care?

## "My CI isn't absurdly fast with zero configuration"

Look. I get it. Setting up the mess of tooling and integrations needed to cache an object file reliably is hard. It's so much easier to just sink into that warm abyss of twenty minute builds (twenty minutes is normal, right?) and tell yourself it doesn't matter.

Twenty minutes is not normal! An hour is not normal! 90% of your builds in CI should be so fast that you have to double check that it actually happened.

If this isn't the case for whatever you work on, [send me an email right god damn now](mailto:admin@spader.zone)! Building native code is an *extremely well understood* problem. The problem's not that we don't know how, it's that we know how, and, well, it's pretty fucking hard unless you spend a *lot* of time on it. You, person who are presumably making and/or selling something other than an obscure meta-tool, don't have that much spare time[^grass].

takes quite a bit of rigor and care that most folks who are actually making and selling things can't spare.

## "My company doesn't approve tools with dependencies (e.g. Python or JS)"

`spn` has *zero* dependencies. It is exactly one binary. Builds are hermetic and sandboxed by default, and it's as easy to pin the exact commits of your dependencies as it is to use packages from the index.

[^grass]: And if you did, you'd probably spend it running your hands through the soft day-warmed grass of some shaded grove, those whom you love most by your side, feeling the beams of the sun against your face and basking in the simple pleasure of being alive.

# FAQ

## What about `compile_commands.json`?

`spn` emits it on every build, at your project's top level. If you were to run:

```bash
spn build
spn build --target=x86_64-windows-gnu
```

Then your `compile_commands.json` would point to MinGW stuff.

## Does it work with MSVC / Xcode?

Yes.

## Why wouldn't I just use...

### ...CMake?

Here we go. The big boy. CMake is as close to a standard as C and C++ have ever had. It's not that bad these days, almost in spite of itself. How could something with a wacky DSL, `CMAKE_MINIMUM_REQUIRED`, no actual build system, and a package manager which is roughly "whatever the hell your distro ships" become the standard option? Well, because it works[^1].

Well, why wouldn't you use something that works? Even if it's flawed, a flawed ecosystem that works is always better than one that fixes the warts but is fragmented or broken. In other words, it's not enough to simply fix CMake's quirks. Nobody cares that you have to write stuff like this:

```cmake
target_link_libraries(app PRIVATE libwhatever)
```

If writing that lets you *compile the fucking program*. That's all people want!

People want this so badly that even *new features* aren't enough to switch. For example, Meson lets you programmatically manipulate `meson.build`. You can hook it into an IDE and get first class support for all kinds of stuff. And nobody cares, because the whole world uses CMake and people just want to compile the program.

People only switch off of things like CMake when the new thing solves entire classes of problems for them. Meson[^2] has a lot of nice stuff, but it's still a generator. You cannot succeed writing a generator, no matter how nice, because the sum total of all that niceness is still less than CMake's inertia.

This is why `spn` is fundamentally different than every other tool in this space.

### ?
- Generators. These are programs that generate inputs to another program which actually compiles your code.
- Package managers. These are programs that know how to produce named packages. Sometimes they build them, sometimes they serve binaries.
- Executors. These are the programs that actually invoke the compiler.

| Tool          | Generator | Package manager | Executor |
|---------------|-----------|-----------------|----------|
| meson         | yes       | kind of         |          |
| xmake         | sometimes | yes             | yes      |
| premake       | yes       |                 |          |
| cmake         | yes       |                 |          |
| conan         |           | yes             |          |
| vcpkg         |           | yes             |          |
| ninja         |           |                 | yes      |
| make          |           |                 | yes      |
| msbuild       |           |                 | yes      |
| xcodebuild    |           |                 | yes      |

### ...Conan?

Conan is a package manager, not a build system. Conan packages are Python scripts which usually just orchestrate the project's native build, which means that compilation is a black box.

[^1]: The aforementioned are *not* knocks on the folks who make CMake, who have willed their product to success through an unbelievable amount of the kind of raw, dirty engineering that nobody wants to do. CMake may be good in spite of itself, but it's good *because* of its maintainers.

[^2]: If you work on Meson, I'm sorry that I'm picking on you. You have a good solution to the problem you picked and a thoughtfully written tool. I'm just arguing that it's not the right problem to begin with.

# Can this replace...

## ...bear, compiledb?

Yes. `spn` generates `compile_commands.json` out of the box, with no instrumentation needed.

# Development

## Building

## Testing

## PRs
