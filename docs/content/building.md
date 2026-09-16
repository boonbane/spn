---
title: Building
order: 5
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

Inside a given build, executables are placed at the top level for easy running. All other artifacts (headers, libraries, your dependencies' artifacts) are placed in `store/` in the usual way. Finally, a JSONL file with a detailed trace of the build is in `.spn/build.jsonl`

```
build/debug/
├── store/
│   ├── bin/
│   │   ├── main.exe
│   ├── lib/
│   │   ├── libwhatever.a
│   ├── include/
│   │   ├── whatever.h
│   │   └── ...
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
