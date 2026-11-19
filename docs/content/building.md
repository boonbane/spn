---
title: Building
---

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

Tests and examples get the same treatment under `test/` and `example/`. Every package, including your dependencies, has its own prefix under `store/` holding the headers, libraries, and binaries it produced. Finally, a JSONL file with a detailed trace of the build is in `.spn/build.jsonl`

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

### JSON event stream

When given `--json`, every command writes the JSONL event stream instead of normal, human readable output. `.spn/build.jsonl` contains the last build's events, regardless of whether you passed the flag.

Each event in the stream is a tagged union:

```json
{
  "event": "target_build_passed",
  "level": "debug",
  "data": { ... }
}
```

For a full list of events emitted and their shape, see the [Events reference](/docs/events), but some useful data emitted in this event stream is:
- Every invocation of the compiler or linker, with full command line and timing
- Every call to the guest API
- Every package resolve and download, with URL and timing

The event stream is a simple rendering of the events delivered to any in process consumer of [libspn](/docs/libspn).

## Selecting targets

### Profiles

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

### Target triples

`spn` has two goals when it comes to what you specify in your build:
1. Either produce a correct binary or fail
2. Be very ergonomic for common cases

Because of (1), you must give a full triple (e.g. `x86_64-linux-gnu`) when cross compiling. We never infer facts about a cross compile from the host that builds it, because we have no information as to which ABI is preferred. Silently picking GNU over musl, for example, has no basis.

Because of (2), you can omit the ABI when you aren't crossing. This has some really useful properties:
- By default, `spn build` always produces a binary that runs on the host
- Statically linked executables choose musl for a true self contained binary, regardless of your host
- You don't need Visual Studio installed to use `spn`!

### Linkage

By default, `spn build` produces the most self contained binary that it can. Besides their dependencies, there are two things that programs control that affect their portability[^1]:

| Field | What it links | How it can break |
|---|---|---|
| `runtime` | Runtime support libraries (e.g. `libstdc++`, `libgcc`, or the CRT on Windows)| The ubiquitous "redistributables" problem on Windows, `libstdc++` on old Ubuntu, Yocto/Buildroot minimal systems |
| `libc` | The C standard library | The mess of distributing Linux binaries that spawned entire ecosystems of tools like AppImage or Snap |

Specify them like this:

```toml
[profile.whatever]
runtime = "static"
libc = "shared"
```

Both fields can be `static` or `shared`. Different platforms have different stances on some or all of these knobs. Where the platform or toolchain doesn't support one of these, `spn` errors appropriately. For example:
- macOS forces you to link dynamically to `libSystem.dylib` (i.e. libc)
- GNU's libc is famously antagonistic to static linkage, so `libc = "static"` requires (and infers) musl
- Some sanitizers require the loader to be available, and produce an error otherwise

### Dependencies

By default, dependencies are linked statically. If a package can only be compiled dynamically and your target has a loader, it will be compiled as a shared library. If your target doesn't have a loader, the build will fail.

You can force dynamic linkage for a library like this:

```toml
[config.foo]
kind = "shared"
```

Finally, it is often desirable to statically link to all of your dependencies while linking dynamically to libc. For example, a program which wants to be hermetic against the versions of libraries on the user's machine, but which wants to load plugins at runtime. To this end, you'd like to compile your dependencies statically or fail the build:

```toml
[profile.whatever]
static_deps = true
```

This tells `spn` to fail instead of switching to dynamic linkage. Dependencies explicitly marked `shared` in `[config]` are still perfectly fine.


#### WASM

A dependency is linked statically when it offers `static`; set `[config.foo] kind = "shared"` to load that one at runtime instead. `static_deps = true` makes a dependency that can only be `shared` an error:
- It is only a check. It doesn't change how anything is linked or which target is built
- A dependency you set to `kind = "shared"` yourself is still allowed
- On a target with no loader, like WebAssembly, `kind = "shared"` has no effect and everything is linked statically

Generally, though, we aim to allow you to express any build you might need. Let's look at some examples:

| Build | Dependencies | Runtime | Libc | ABI | |
|---|---|---|---|---|---|
| A Linux binary that runs anywhere | `static` | `static` | `static` | `musl`| This is a completely self contained binary that will run on any Linux system you'll find in the wild; `spn` does this by default |
| A portable Linux video game       | any | `static` | `shared` | `gnu`| Games need Vulkan or OpenGL, which must be used from a shared library, but otherwise bundle everything else they need inside the binary |
| A Windows binary which doesn't need redistributables | any | `static` | `static` | `msvc` | Like `/MT`; The UCRT, vcruntime, and the C++ STL all live inside the executable |
| A plugin that gets loaded by a DAW | any | `shared` | `shared` |  | You *want* to share the host's runtime, so that an e.g. `std::string` can cross between your DLL and the host cleanly |
| A fully sanitized build (ASan, TSan, MSan)|  |  | `shared` | any | These sanitizers don't work without a loader |

### Modes and optimization

- `mode` is `debug` or `release`. Set it with `--mode` (`-m`), or `mode` in a profile.
- `debug` compiles at `-O0` with debug info.
- `release` compiles at `-O2`, defines `NDEBUG` (so `assert` is off), and has no debug info.
- `--opt`, or `opt` in a profile, overrides the level: `0`, `1`, `2`, `3`, `s`, or `z`.
- `strip` in a profile is `none`, `debuginfo`, or `symbols`. `release` defaults to `debuginfo` and `debug` to `none`. There's no flag for it.

### Sanitizers

- `--sanitize address,undefined`, or `sanitize = ["address", "undefined"]` in a profile. `--sanitize none` clears the profile's list.
- The names are `address`, `thread`, `undefined`, `memory`, and `leak`.
- The bundled zig only has `undefined` and `thread`, and only `undefined` on Windows.
- `address` and `leak` need a system clang or gcc, and `memory` needs clang. MSVC has `address`.
- With no `--toolchain`, `spn` moves to a toolchain that has what you asked for: `--sanitize address` on Linux builds with your system clang.
- Everything but `undefined` needs a dynamic loader. A native Linux build switches from static musl to glibc, and `libc = "static"` is an error.
- zig debug builds already have UBSan on. Undefined behavior aborts the program with a `panic:` message and a stack trace; a release build doesn't check.

## Cross compilation

- `--target <arch>-<os>-<abi>` builds for another platform, e.g. `spn build --target aarch64-linux-musl`.
- The names are `spn`'s own. LLVM triples like `x86_64-unknown-linux-gnu` or `arm64-apple-darwin` are rejected.
  - arch: `x86_64`, `aarch64`, `wasm32`
  - os: `linux`, `macos`, `windows`, `wasi`, `freestanding`
  - abi: `gnu`, `musl`, `msvc`, `apple`, `none`, `elf`
- Linux is `gnu` or `musl`, and Windows is `gnu` (MinGW) or `msvc`. macOS and WASI have one ABI, so `aarch64-macos` is a full target.
- `--os`, `--arch`, and `--abi` set one part each. `os`, `arch`, and `abi` in a profile make the target permanent.
- The bundled zig cross compiles to Linux, Windows (MinGW), macOS, and WebAssembly from any host, with nothing else installed.
- System gcc, clang, and MSVC only build for the machine they run on, unless you declare a cross [toolchain](/docs/toolchains).
- Two targets need more than zig:
  - A macOS program that links frameworks needs a macOS SDK: the Xcode Command Line Tools on a Mac, or `SPN_MACOS_SDK` pointing at one.
  - The MSVC ABI needs Visual Studio and the Windows SDK.
- A cross build lands in `build/<target>/<profile>/`, as in [Build output](/docs/building#build-output).

## Running tests

- `spn test` builds every `[[test]]` target and runs each one. `spn test NAME...` runs only the ones you name.
- A test passes when it exits `0`.
- Each test runs from the package's source directory, with no arguments and no stdin.
- Output is shown only for a test that fails.
- Every test runs even after one fails, and `spn test` exits `1` if any did.
- To pass arguments to a test, run its binary yourself from `test/` in the build directory.
- `spn test` takes `-p`, `-m`, `--opt`, `--sanitize`, and `--toolchain`, but not `--target`: it can't run a cross compiled test.
- `spn build` compiles tests without running them.

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

- `spn build --force` (`-f`) runs every step again instead of restoring it from the cache.
- It's rarely needed. Inputs are compared by content, so touching a file or switching branches rebuilds nothing.
- Deleting `build/` doesn't force a rebuild either. The next build restores everything from the shared cache.

### spn clean

- `spn clean` removes `build/`, `compile_commands.json`, and any files [`[stage]`](/docs/metaprograms#staging) copied into your project.
- `spn clean -p NAME` removes only that profile's build directory.
- Neither touches the shared cache. It lives in `spn`'s storage directory, is shared by every project on the machine, and is trimmed on its own at 10 GiB (see [`[cache]`](/docs/configuration#cache)).
- `spn clean --cache` evicts cache entries that haven't been used in the last hour. It isn't a full wipe, and it frees nothing right after a build.
- `--min-age SECONDS` changes the hour, down to ten minutes.

[^1]: There are more, of course, like the mess of versioned symbols in glibc. But for our purposes, three is fine.
