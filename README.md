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

`spn` doesn't provide any packages for OS package managers at the moment. Install by grabbing and running the latest install script, which you [can and should read](#installer):

### macOS & Linux

```sh
curl -fsSL https://github.com/boonbane/spn/releases/latest/download/install.sh | sh
```

### Windows

```powershell
irm https://github.com/boonbane/spn/releases/latest/download/install.ps1 | iex
```

This launches a short, interactive installer which will put `spn` on your `PATH` and configure your shell(s). Set `SPN_INSTALL_AUTO` for unattended installation. By default, it is installed to `$HOME/.spn/bin` everywhere.

## Hello, world!

To create a project:

```sh
spn init
```

This will create three files, `spn.toml`, `main.c`, and `.gitignore`, failing if any already exist. The command prompts you for a name, and creates a manifest like this:

```toml
[package]
name = "hello"
version = "0.1.0"

[[bin]]
name = "hello"
source = ["main.c"]
```

Build your project:

```sh
spn build
./build/debug/hello
```

By default, builds go in `build/$profile`, uses the `debug`, and compiles all libraries, executables, tests, and examples. Pass the name of a target to just build that target, or e.g. `--test` for just tests. Next, we'll look at how to use a dependency.

## Dependencies

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

Since C, unlike many languages, has no central package registry, you need to choose where your Lua package comes from. Out of the box, you can use [spam](https://github.com/boonbane/spam)[^1], which has packages for many common C libraries written and maintained alongside `spn`. Add it, and then add Lua:

```sh
spn index add https://github.com/boonbane/spam.git
spn add lua
```

This adds an entry to your manifest's `[deps.package]`. Packages can be vendored, or come from self-contained directories locally, or properly published to an index of releases; they can come from public or private repositories. You can read more about packaging [here](#packages).

With your new `main`, build:

```sh
spn build
./build/debug/hello
```

## Cross compilation

Cross compilation is first class in `spn`. You can cross compile the same Lua example trivially:

```sh
spn build --target x86_64-windows-gnu
spn build --target aarch64-macos
spn build --target x86_64-linux-gnu
```

When you explicitly pass a target, the build goes in `build/$target/$profile`. The above commands produce:
- `build/x86_64-windows-gnu/debug/demo.exe`
- `build/aarch64-macos-apple/debug/demo`
- `build/x86_64-linux-gnu/debug/demo`

## Build directory

In addition to executables, the build directory for a given profile (e.g. `build/x86_64-windows-gnu/debug`) also contains:

- `test/`, your test executables
- `store/$pkg`, an insall prefix for every package in your workspace
- `.spn/`, build metadata and work directories for metaprograms
- `.spn/build.jsonl`, a rich, structured JSONL event stream from your build (equivalent to `--json` output)


## compile_commands.json

`spn` generates `compile_commands.json` out of the box, no configuration needed.


## What's next?

[](https://github.com/boonbane/spn/tree/main/example)


## Installer

The installer comes as either a [shell script](https://github.com/boonbane/spn/blob/main/tools/install/install.sh) or a [PowerShell script](https://github.com/boonbane/spn/blob/main/tools/install/install.ps1). Both of these scripts are extremely thin programs which mostly just download and verify the appropriate `spn` executable. Any logic is implemented in `spn` itself, and exposed via `spn self`.

The entry point for that code is [here](https://github.com/boonbane/spn/blob/main/source/cli/commands/self.c), roughly. I encourage you (or your helpful digital assistant) to read these sources. If you find anything in the installer difficult, insecure, or in any way wrong, I'd be happy to hear from you on [our Discord](https://discord.gg/7v4C5Kwsp7) or personally over [email](mailto:admin@spader.zone).

[^1]: spn's package manifests

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

A *package* is a collection of executables and libraries, tests, and examples, called *targets*. Targets have fields like `source`, or `include`, or `system_deps`, and most fields can be configured at the package level for all targets or for an individual target.

We'll cover conditional compilation on [the next page](https://spn.spader.zone/docs/conditions), but for now it's enough to know that nearly every field can be set conditionally with a `when` clause:

```toml
[[lib]]
system_deps = {
  { lib = "advapi32", when = { os = "windows" } },
}
source = [
  { path = "src/audio/pipewire/*.c", when = { os = "linux", pipewire = true } },
]
```

## Targets

There are five kinds of targets, as shown in the example above.

```toml
[package]
name = "foo"
include = ['common']

[[lib]]
name = "foo"
source = ['source/lib.c']
include = ['source/lib']

[[example]]
name = "kram"
source = ['example/kram.c']

[[test]]
name = "unit"
source = ['test/main.c', 'test/foo.c']
define = ['BAR', 'BAZ=69']
system_deps = ['m']

[[tool]]
name = "serve"
source = ['tools/server.c']
deps = ['foo']
```

### Executables

`[[bin]]`. These are executables which are exported by your package. They're placed at your build's top level, `$build`.

### Libraries

`[[lib]]`. These are libraries which are exported by your package. Every `[[lib]]` declared by a dependency is compiled unconditionally, and no executables are compiled.

A library declares the linkages it supports with the `kinds` field. For example, a library that can't work when compiled statically:

```toml
[[lib]]
kinds = ["shared"]
```

### Examples

``[[example]]`. These are examples, placed in `$build/example/`

### Tests

`[[test]]`. These are tests, for some definition of the word test. `spn` has no test framework or integration with one. Instead, `[[test]]` executables are reguler executables which:
- Are not exported
- Have their own dependencies in `[deps.test]`.

`spn test` runs all such executables and treats a nonzero exit code as failure. For more information, read about [the task runner features](https://spn.spader.zone/docs/task) of `spn`.

### Tools

`[[tool]]`. These are executables that aren't exported by your package. They're used like `scripts` in a `package.json`, (usually) simple, native tools that you run locally. They have their own dependencies in `[deps.tool]`.

### Metaprograms

`[package.configure]` and `[package.build]`. Metaprograms are just like any other executable, except that they're compiled to WebAssembly and run inside a sandbox in your build. For more information, check out the [documentation on metaprograms](https://spn.spader.zone/docs/metaprograms).

Unlike other targets, metaprograms are restricted to one per package (i.e. one configure metaprogram, one build metaprogram).

## Shared fields

Several target fields can also be declared at the package's top level (`include`, `define`, `system_deps`, `host_deps`, and `macos`). These are merged in to each target. For example, the following executable would get `common/` and `special/` as its include directories:

```toml
[package]
include = ["common"]

[[bin]]
include = ["special"]
```

For a full list of fields, see [the manifest reference](https://spn.spader.zone/docs/manifest).


## Sources

The `source` field lists a target's sources. Each entry is either a plain string, for common files and globs, or a table, when the entry is conditional or not in the repository (more on these in a bit).

Paths are always relative, must use forward slashes, and cannot contain `.` or `..` segments. Code outside the package's directory

Finally, language is inferred from the entry's extension:
- `.cpp`, `.cc`, `.cxx`, `.c++`, and `.C` compile as C++
- `.s`, `.S`, and `.asm` compile as assembly
- Everything else compiles as C (e.g. `.m` files)

### Globs

Paths can be written as globs. `spn` implements shell-style globs, plus `**`. That means you can use `*`, `**`, `?`, `[a-z]`, `[!a-z]`, `[^a-z]`, and `{a,b}`. Wildcards also match directory components. It differs from how the shell usually works in a few ways:
- `*` matches dotfiles
- `**` matches at any depth
- `*` and `?` never cross a separator
- There's no escaping. `\` is just rewritten to `/`
- Matching is byte-exact on every OS, so e.g. `*.c` doesn't match `FOO.C`
- Unlike `.gitignore`, where `foo/` matches `a/b/foo`, our patterns are checked from the top level

### Trees

Sometimes, a file doesn't live in your repository:
- Files generated by a [metaprogram](https://spn.spader.zone/docs/metaprograms#using-generated-files)
- Source-relative files when [packaging an upstream repository](https://spn.spader.zone/docs/packaging#starting-a-package) (e.g. you want to refer to `example/foo.c` from `libfoo`, but your repository only contains a manifest)

To this end, you can specify which tree a path is relative to by using the table form:

```toml
[[example]]
source = [
  { path = "example/foo.c", tree = "source" },
  { path = "src/whatever.c", tree = "manifest" },
  "example/common/*.c" # Implicitly source-rooted
]
```

This lets you mix between files *you* wrote (`whatever.c`) and files that *upstream* wrote (`foo.c`), without forking or patching.


## Headers

`[package.include]` is to include directories as `source` is to sources; they can be single entries or globs, and they can be plain strings or tables. Unlike sources, `.` is allowed as a special case to mean the top level.

```toml
[package]
include = [
  ".",
  "common/"
  { path = "include/", tree = "source" },
  { path = "my_special_includes/", tree = "manifest" },
  { path = "generated/", tree = "work" },
]
```

Paths listed under `[package.include]` are not exported, just shared between your targets. If you're looking to publish headers to packages that consume you, [see here](#exports).

## Preprocessor

`[package.define]` is an array of entries of the form `NAME` or `NAME=VALUE`. Each definition is private to this package's compilation. To publish preprocessor definitions to consumers, [see here](#publishing-defines).

## Flags

You can forward custom flags verbatim to the compiler and linker with `flags` and `link_flags`, respectively. Use `when` clauses with `driver` as the predicate to limit the flags to where they're needed.

`link_flags` only apply to the target's own link. They aren't added to a consumer's link line. Moreover, since static libraries are not linked, merely archives, these flags only apply to shared libraries.

## C standard

C is compiled as strict C11 (`-std=c11`) by default. `spn` supports the following: `c89`, `c99`, `c11`, `gnu89`, `gnu99`, `gnu11`. Until newer standards are added, pass them as `flags`.

Many functions which one might assume are standard parts of C, like `strdup` or `clock_gettime`, are actually POSIX or GNU extensions. `spn` doesn't yet have a way for packages to communicate the standard they require to their consumers, so fix this by defining the corresponding feature macro (e.g. `_GNU_SOURCE` or `_POSIX_C_SOURCE=200809L`) until then.

## C++

The `cxx` field on a target controls C++ settings. `spn` exposes `standard`, `exceptions`, and `rtti`; for a full list of values and defaults, see [the manifest reference](https://spn.spader.zone/docs/manifest#cxx_options).

C programs that link a static library containing C++ get the C++ runtime automatically.

### Exceptions

Under WASM, exceptions are off by default. They're also enabled at the toolchain level, since driver support is spotty and differs between the same driver across different toolchains.

## Dependencies

Each kind of executable (`bin`, `test`, `example`, `tool`, plus metaprogram) can have its own dependencies. An executable's `deps` name other packages or targets within your own package. When a package is named, the executable links to every library in that package.

### system_deps

`system_deps` are libraries provided by the *target*, like `m`, or `ws2_32`, or macOS frameworks like `Cocoa`. They are always spelled as a table:

```toml
[package]
system_deps = [
  { lib = "advapi32", when = { os = "windows" } },
  { lib = "m", when = { os = "linux" } },
  { framework = "Cocoa" },
]
```

These can of course be specified as raw `link_flags`, but `system_deps` will automatically spell them for the linker in use. Since `system_deps` are by definition platform specific, you'll usually use them with a `when` clause (except `framework`, which is automatically ignored when not targeting macOS).

A static library's `system_deps` follow it to whatever links it. You don't need to re-declare your dependencies' `system_deps`.

### host_deps

`host_deps` are libraries provided by the *host* i.e. the build machine. They are [discouraged](https://spn.spader.zone/docs/dependencies#system-dependencies), and they disable cross compilation. However, tools exist to serve, not to artificially restrict or get in the way, so when you just want to link to something on the build machine this field is available.


## Exports

`[package.publish]`

Let's say that you have these headers:

```
foo/
  bar/
    b.h
  baz/
    qux/
      c.h
    spum/
      d.h
  a.h
```

And you publish them like so, flattening out `baz` and otherwise treating `foo` as the top level:

```toml
[package.publish]
include = [
  { from = "foo/*.h"},
  { from = "foo/bar/*.h", to = "bar" },
  { from = "baz/**/.*.h", to = "baz" },
]
```

You'd end up with this:

```
include/
  a.h
  bar/
    b.h
  baz/
    c.h
    d.h
```

### Publishing defines

> This section is a stub.

- `[package.publish] define` lists macros that dependents compile with.
- A bool [option](https://spn.spader.zone/docs/conditions#options) with `define` and `public = true` does the same while the option is on.
- Publish any macro that changes your public headers, like a static linking switch or one that changes a struct layout.
- Without it, the package and its dependents silently compile the same header differently.
- `[package.publish] system_deps` lists system libraries that dependents link.

### Library kinds

> This section is a stub.

- `kinds` is `static`, `shared`, or both, and `["static"]` when omitted.
- Offer `shared` only when upstream supports a shared build.
- A dependent links the static kind when the library offers it, unless the root sets [`[config.foo] kind`](https://spn.spader.zone/docs/dependencies#linkage).
- Which kinds are built for your own package is the profile's [`emit`](#libraries-1).

Targets declare what kinds of linkage they support:
- `static`
- `shared`
- `object`, which skips the linker and directly publishes object files to `lib/`
- `source`, which means that the component object files are compiled directly into your program

These kinds are opt-in; there's no default, and you must declare what you support. A target can also set `link = false` to simply compile the sources without linking them[^1].

`kinds` lists what a library can be built as. The default is `["static"]`.

| Kind | Output |
|---|---|
| `static` | An archive (`libfoo.a`, or `foo.lib` for MSVC) |
| `shared` | `libfoo.so`, `libfoo.dylib`, or `foo.dll` with `foo.dll.lib` to link against. On WebAssembly, a module (`foo.wasm`) that exports the library's functions |

A WebAssembly module can't be linked against, so a library that other targets link needs `static` there.

### Header-only packages

`spn` is perfectly happy with header only packages. That includes single header libraries, like `sokol`, as well as packages that provide headers for a system library like X11 or PulseAudio.

For true header only libraries, you don't need a `[[lib]]` target. Such a library is just:

```toml
[package.publish]
include = [...]
```

With no library to carry them, the package's `system_deps` and `define` don't reach dependents; put them in `[package.publish]`.

## Platform specific

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

## Embedding files in binaries

`spn` can embed arbitrary files and bytes in your binary. This is a very common way to ship a program with fonts, shaders, assets, etc. without depending on a filesystem at runtime.

Embedded files are specified in TOML. `spn` creates:
- An object file containing the literal bytes
- A header file with metadata for use in your program

Each entry names a file (or, with `dir = true`, every file under a directory) and the path it is embedded as; the symbol is that path with `/`, `\\`, `.`, and `-` replaced by `_`. For example, this configuration:

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

[^1]: For example, `tinycc` wants to compile `libtcc1.a`, which is the compiler's runtime library that it links into programs it compiles. It's not linked into the `tcc` binary itself; think of it like an asset that `tcc` wants available at runtime. It's a binary produced from C and Assembly code, so `spn` should damn well be able to build is as part of its build, but it doesn't get *linked* into anything, and it doesn't get *published*, so `object` isn't appropriate either.

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

Inside a given build, each executable is placed at the top level. The shared libraries it needs (itself or transitively) are staged next to it. The executable is relocatable.

Tests and examples get the same treatment under `test/` and `example/`. Every package, including your dependencies, has its own prefix under `store/` holding the headers, libraries, and binaries it produced. Finally, a [JSONL file with a detailed trace of the build](#json-event-stream) is in `.spn/build.jsonl`

```
build/debug/
├── foo.exe
├── bar.dll
├── store/
│   ├── foo/
│   │   └── bin/
│   │       └── foo.exe
│   └── bar/
│       ├── lib/
│       │   └── bar.dll
│       └── include/
│           ├── bar.h
│           └── ...
└── .spn/
    └── build.jsonl
```


## Profiles

Packages are compiled against a profile, which contains the target triple, toolchain, build mode, optimization level, sanitizers, etc. `spn` bakes in two profiles, `debug` and `release`, and reserves `[profile.default]` as the base fields which every profile uses unless overridden.

```toml
[profile.default]
toolchain = "zig"
static_deps = true
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

### Optimization

`opt` in the profile overrides the mode's default, and can be `0`, `1`, `2`, `3`, `s`, or `z`. When provided by the toolchain, `s` optimizes for size, and `z` does so more aggressively.

### Stripping

`strip` controls what is stripped from binaries: `none` does nothing, `debuginfo` strips debug information but otherwise nothing, and `symbols` also strips unused symbols (which may be undesirable for some kinds of libraries)

### Build modes

`mode` is a field of the profile which controls whether you build a `debug` or `release` binary. By default, it is `debug`. The field determines whether your binary contains debug info and whether `NDEBUG` is defined, plus defaults for optimization and stripping:

| Mode | Debug info? | `NDEBUG` | Optimization | Strip |
|---|---|---|---|---|
| `debug` | yes | not defined | `-O0` | `none` |
| `release` | no | defined | `-O2` | `debuginfo` |

## Sanitizers

`spn` has first class support for sanitized builds. It supports `address`, `thread`, `undefined`, `memory`, and `leak`.

```toml
[profile.safe]
sanitize = ["address", "undefined"]
```

```sh
spn build --sanitize thread,leak

# --sanitize always overrides the profile
spn build --sanitize thread,leak --profile safe
spn build --sanitize none --profile safe
```

### Availability

`spn` doesn't bundle sanitizers. It uses what's provided by the toolchain. `spn` never probes to discover what a toolchain supports, but rather expects it to be declared. We therefore provide a reasonable default support matrix for [system toolchains](https://spn.spader.zone/docs/toolchains#system-toolchains).

| Toolchain | Linux | macOS | Windows |
|---|---|---|---|
| `zig` | `undefined`, `thread` | `undefined`, `thread` | `undefined` |
| `clang`, `llvm` | `address`, `thread`, `undefined`, `memory`, `leak` | `address`, `thread`, `undefined`, `leak` | none |
| `gcc` | `address`, `thread`, `undefined`, `leak`[^3] | `address`, `thread`, `undefined` | none |
| `msvc`, `llvm` | | | `address`[^5] |

Of course, without brittle probing, `spn` can't provide a default that covers every real toolchain installed on a user's machine. If your system's toolchain is different, or if you're pinning to an external toolchain, [it's easy to declare what sanitizers it supports](https://spn.spader.zone/docs/toolchains#custom-toolchains).

### Toolchain selection

When you don't explicitly pass a toolchain, `spn` may pick a toolchain that can support the sanitizers you asked for. For example, `--sanitize address` can't be satisfied by the default Zig toolchain, so `spn` might fall back to your system `clang`. If this is undesirable, pass a toolchain explicitly.

Everything except `undefined` needs a dynamic loader. A native Linux build switches from static musl to glibc, and `libc = "static"` is an error.

### Errata

The built in Zig toolchain enables UBSan in debug builds by default. Thank you, Zig!


## Cross compilation

Cross compilation is one of the fundamental principles of `spn`, and setting aside draconian practices like *copyrighting your platform's headers*[^6] there's no reason that any piece of software shouldn't be able to be cross compiled.

To cross compile, pass `--target $triple`. A triple is of the format `$arch-$os-$abi`, and the ABI can be omitted when the OS only supports one ABI (like `macos` and `apple`). The following values are supported:
- arch: `x86_64`, `aarch64`, `wasm32`
- os: `linux`, `macos`, `windows`, `wasi`, `freestanding`
- abi: `gnu`, `musl`, `msvc`, `apple`, `none`, `elf`

Here are a few sample triples of note:
| Triple | Notes |
|---|---|
| `aarch64-macos` | Equal to `aarch64-macos-apple` |
| `wasm32-wasi` | Equal to `wasm32-wasi-musl` |
| `wasm32-freestanding` | Equal to `wasm32-wasi-none` |
| `x86_64-freestanding-none` | Raw machine code, like a kernel |
| `x86_64-linux-none` | Raw ELF binary, no libc |
| `x86_64-windows-gnu` | MinGW |

You must give a full triple (e.g. `x86_64-linux-gnu`) when cross compiling. We never infer facts about a cross compile from the host that builds it, because we have no information as to which ABI is preferred.

### zig cc

Out of the box, `spn` uses the Zig toolchain, which means that it can cross compile your code to Linux, Windows, macOS, and WebAssembly from any host, with no external tools or compilers needed. This is largely thanks to the truly, truly excellent `zig cc`, which does the work of bundling together and unkinking countless toolchains and provides a build of `clang` that's preconfigured to use them[^7].

### macOS

A macOS program that links frameworks needs a macOS SDK, and Apple's licensing prevents us (or anyone) from redistributing the headers and `.tbd` stubs needed to compile and link from a non-macOS host[^8].

- `--os`, `--arch`, and `--abi` set one part each. `os`, `arch`, and `abi` in a profile make the target permanent.
- The bundled zig cross compiles to Linux, Windows (MinGW), macOS, and WebAssembly from any host, with nothing else installed.
- System gcc, clang, and MSVC only build for the machine they run on, unless you declare a cross [toolchain](https://spn.spader.zone/docs/toolchains).

### Windows

Because the bundled Zig toolchain ships with MinGW sources, Windows builds by default target MinGW. MSVC has first class support, and `spn` will detect your Visual Studio installations and Windows SDKs automatically; just pass `--toolchain msvc`, or see [the toolchain docs](https://spn.spader.zone/docs/toolchains#msvc) for how to pick between installations or use e.g. `xwin`.

## Runtime and libc

Besides their dependencies, there are two things that programs control that affect their portability[^1]:
- `runtime`. Libraries linked by the compiler, like the unwinder, low level math routines, the C++ standard library (e.g. `libstdc++`, `libgcc`, `winpthreads`)
- `libc`. The C standard library (e.g. `libc.so`, `libSystem.dylib`, the UCRT)

Specify them like this; both fields can be `static` or `shared`:

```toml
[profile.whatever]
runtime = "static"
libc = "shared"
```

### Defaults

Different platforms have different stances on some or all of these knobs, but by default  `spn build` produces the most self contained binary that it can. Here's the values that get picked for different combinations of target:

| OS | ABI | `runtime` | `libc` | Notes |
|---|---|---|---|---|
| `linux` | `musl` | `static` | `static` | The default on any Linux box |
| `linux` | `gnu` | `shared` | `shared` | `spn` will implicitly switch to `gnu` when shared libraries are allowed and some dependency or sanitizer runtime cannot be statically linked |
| `macos` | `apple` | `shared` | `shared` | macOS forces programs to link dynamically to `libSystem.dylib` (i.e. libc) |
| `windows` | `gnu` | `static` | `shared` | MinGW does not provide a `libc` to statically link, and regardless `ucrtbase.dll` ships with Windows |
| `windows` | `msvc` | `static` | `static` | Avoid requiring Visual C++ Redistributables, by default[^9] |

### Dependencies

By default, dependencies are linked statically, and if a package doesn't support static linkage it will be linked dynamically when your target supports it. For example, if you're building against glibc and producing a dynamically linked binary, `spn` will build and link to `libfoo.so` if `libfoo` can't be compiled statically. But if you're building a statically linked musl binary, it will fail.

If you don't want this fallback to dynamic linkage, setting `static_deps` on your profile will fail when static linkage isn't available[^2]:

```toml
[profile.whatever]
static_deps = true
```

This isn't an *override*. Rather, it's a *check*. It tells the build system "Unless I explicitly asked for a shared library, do not allow one". You can control how individual dependencies link in the `[config]` section:

```toml
[config.foo]
kind = "shared"
```

Taking the two snippets above, here's what would happen with three hypothetical dependencies:

| Library | Supports | Result |
|---|---|---|
| `libfoo` | `static`, `shared` | `libfoo.so`, because you explicitly asked for it |
| `libbar` | `static`, `shared` | `libbar.a`, because dependencies link statically by default |
| `libqux` | `shared` | Failure, because you told `spn` not to fall back to dynamic linkage |

### Libraries

When you build the package itself, a library is built as `static` when it offers it. A profile's `emit` lists the kinds to build instead:

```toml
[profile.release]
emit = ["static", "shared"]
```

`emit` only chooses for a library that offers both kinds. An archive that an executable in the same build links is built either way.

A `shared` kind needs a dynamic loader, so asking for one moves the default Linux build off its static libc, and executables in that build link libc dynamically too. Where there is no loader, as with `libc = "static"`, the `shared` kind is skipped.

### WASM

WASM has no concept of a shared library in the same sense as a traditional OS. When compiling a target for WASM:
- If it's your top level target, `shared` means to link it as a module, `foo.wasm`. On WASI, that's a reactor.
- If it's a dependency, `shared` and `static` are equivalent, and it's just a regular static archive

### Portability

`runtime` and `linkage` are what most often produce unportable binaries. If you're experienced with C/++, feel free to skip these examples. If you aren't, I've provided them to show why these knobs aren't arbitrary.

#### Gotcha: Why doesn't my program work on Ubuntu?

`linux` + `gnu` + `shared` libc

*The* classic Linux trap. Compile on a machine with, say, glibc `2.69`, and unknowingly pull in some function that was just added. Ubuntu is known for having a slow, fixed release cycle[^11], which means it might come with, say, glibc `2.34`, which doesn't have that function. Your program crashes because it can't load the function it needs.

#### Gotcha: Why can't I open a window?

`linux` + `static` libc

Static linkage means libc is *inside* your binary. You ask the user's machine for nothing. But, by definition, it means there's no loader for shared libraries[^10], so if you need to load a graphics driver, X, Wayland, or any kind of plugin, you're F'd.

#### Gotcha: What the heck is a redistributable?

`windows` + `msvc` + `shared` runtime and libc

Probably *the* classic Windows trap, and fundamentally the same as its Linux counterpart, except instead of versioning individual symbols the version is for the entire binary. The user therefore needs the correct version binary at runtime, and if they don't they get the dreaded `VCRUNTIME140.dll not found`.

#### Gotcha: Freeing a `std::string` crashed my program

`windows` + `msvc` + `static` runtime/libc

If you've ever freed a `std::string` allocated in a DLL and crashed your program, it's because `static` gives everyone their own heap and CRT state. It's as if you tried to free a string that was allocated by `taskmgr.exe`.

#### Gotcha: Nothing I build with MinGW works

`windows` + `gnu` + `shared` runtime

`shared` by definition means "load at runtime", which means the library must *exist* at runtime. For MSVC, that's usually OK, because redistributables are commonly installed. But here, you're requiring the user to have e.g. `libstdc++-6.dll` installed, which here's a secret: They don't.

#### Gotcha: macOS

Joke's on you. There are no gotchas on macOS here. A walled garden has its perks, eh?


### Cheat Sheet

Here are a few examples of useful combinations of these knobs. This is, of course, not exhaustive. If you need help understanding any of these options, or if you know what you want but aren't sure how to tell `spn` to do it, stop by [our Discord](https://discord.gg/7v4C5Kwsp7) or [send me an email](mailto:admin@spader.zone).

| Build | Deps | Runtime | Libc | ABI | |
|---|---|---|---|---|---|
| A Linux binary that runs anywhere | `static` | `static` | `static` | `musl`| This is a completely self contained binary that will run on any Linux system you'll find in the wild; `spn` does this by default |
| A portable Linux video game       | any | `static` | `shared` | `gnu`| Games need Vulkan or OpenGL, which must be used from a shared library, but otherwise bundle everything else they need inside the binary |
| A Windows binary which doesn't need redistributables | any | `static` | `static` | `msvc` | Like `/MT`; The UCRT, vcruntime, and the C++ STL all live inside the executable |
| A plugin that gets loaded by a DAW | any | `shared` | `shared` |  | You *want* to share the host's runtime, so that an e.g. `std::string` can cross between your DLL and the host cleanly |
| A fully sanitized build (ASan, TSan, MSan)|  |  | `shared` | any | These sanitizers don't work without a loader |


## JSON event stream

When given `--json`, every command writes the JSONL event stream instead of normal, human readable output. `.spn/build.jsonl` contains the last build's events, regardless of whether you passed the flag.

Each event in the stream is a tagged union:

```json
{
  "event": "target_build_passed",
  "level": "debug",
  "data": { ... }
}
```

For a full list of events emitted and their shape, see the [schema on GitHub](https://github.com/boonbane/spn/blob/main/source/core/codegen/schema/events.jtd.json), but some useful data emitted in this event stream is:
- Every invocation of the compiler or linker, with full command line and timing
- Every call to the guest API
- Every package resolve and download, with URL and timing

The event stream is a simple rendering of the events delivered to any in process consumer of [libspn](https://spn.spader.zone/docs/embedding).

[^1]: There are more, of course, like the mess of versioned symbols in glibc. But for our purposes, three is fine.
[^2]: It is often desirable to statically link to all of your dependencies while linking dynamically to libc. For example, a program which wants to be hermetic against the versions of libraries on the user's machine, but which wants to load plugins at runtime. To this end, you'd like to compile your dependencies statically or fail the build.
[^3]: Only for the `gnu` ABI; stock GCC's sanitizer runtimes are generally incompatible with musl. These are broad defaults. They are *not* intended to model arbitrary, unknown systems. They're meant to make the common case of downloading `spn` and using it against whatever you have installed work reasonably well, with no configuration or probing. If they don't work for you, simply write down what your toolchain can do and `spn` will happily oblige.
[^5]: x86_64 only.
[^6]: Get bent, Apple. Seriously.
[^7]: `spn` wouldn't exist without Zig. Part of that is the massive amount of work and care taken to create `zig cc` and gift it to the world, gratis. And part of it is the attitude that simply refuses to accept the status quo of systems programming, the love and care they put into fixing it, and the simple existence proof that you can build software like this. Thank you, Zig!
[^8]: From my research, this isn't exactly correct. Many such headers are released under Apple's open source license, and from my understanding could be bundled with `spn`. However, it's an incomplete set, and more importantly they *differ* from the actual headers you might find in a real SDK. Even though it's incomplete, it's not *trivial*. But, most importantly, it doesn't cover Cocoa which is Objective-C. It is more complicated than that, since you can write C headers against `libobjc`, but very few upstream libraries actually compile against such an interface and in general I have no idea how feasible that is[^6]. I'm sure my research is scattershot compared to someone who actually understands all of this, so if you *are* that person, hit me up on [Discord](https://discord.gg/7v4C5Kwsp7) or [send me an email](mailto:admin@spader.zone).
[^10]: If there were, you'd need to ensure that the libraries it loads are ABI compatible with your binary, which means you need to be using the same libc, which means you need to dynamically link to it!
[^11]: I don't mean to pick on Ubuntu; this is a univeral problem in the Linux world. Folks may be more familiar with it on Ubuntu given its popularity and release cadence.
