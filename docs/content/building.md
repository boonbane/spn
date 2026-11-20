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

`spn` doesn't bundle sanitizers. It uses what's provided by the toolchain. `spn` never probes to discover what a toolchain supports, but rather expects it to be declared. We therefore provide a reasonable default support matrix for [system toolchains](/docs/toolchains#system-toolchains).

| Toolchain | Linux | macOS | Windows |
|---|---|---|---|
| `zig` | `undefined`, `thread` | `undefined`, `thread` | `undefined` |
| `clang`, `llvm` | `address`, `thread`, `undefined`, `memory`, `leak` | `address`, `thread`, `undefined`, `leak` | none |
| `gcc` | `address`, `thread`, `undefined`, `leak`[^3] | `address`, `thread`, `undefined` | none |
| `msvc`, `llvm` | | | `address`[^5] |

Of course, without brittle probing, `spn` can't provide a default that covers every real toolchain installed on a user's machine. If your system's toolchain is different, or if you're pinning to an external toolchain, [it's easy to declare what sanitizers it supports](/docs/toolchains#custom-toolchains).

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
- System gcc, clang, and MSVC only build for the machine they run on, unless you declare a cross [toolchain](/docs/toolchains).

## Linkage

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

### WASM

WASM has no concept of a shared library in the same sense as a traditional OS. When compiling a target for WASM:
- If it's your top level target, `shared` means to link it as a module, `foo.wasm`. On WASI, that's a reactor.
- If it's a dependency, `shared` and `static` are equivalent, and it's just a regular static archive

### Cheat Sheet

Here are a few examples of useful combinations of these knobs. This is, of course, not exhaustive. If you need help understanding any of these options, or if you know what you want but aren't sure how to tell `spn` to do it, stop by [our Discord](link:discord.public) or [send me an email](link:email).

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

For a full list of events emitted and their shape, see the [Events reference](/docs/events), but some useful data emitted in this event stream is:
- Every invocation of the compiler or linker, with full command line and timing
- Every call to the guest API
- Every package resolve and download, with URL and timing

The event stream is a simple rendering of the events delivered to any in process consumer of [libspn](/docs/libspn).

[^1]: There are more, of course, like the mess of versioned symbols in glibc. But for our purposes, three is fine.
[^2]: It is often desirable to statically link to all of your dependencies while linking dynamically to libc. For example, a program which wants to be hermetic against the versions of libraries on the user's machine, but which wants to load plugins at runtime. To this end, you'd like to compile your dependencies statically or fail the build.
[^3]: Only for the `gnu` ABI; stock GCC's sanitizer runtimes are generally incompatible with musl. These are broad defaults. They are *not* intended to model arbitrary, unknown systems. They're meant to make the common case of downloading `spn` and using it against whatever you have installed work reasonably well, with no configuration or probing. If they don't work for you, simply write down what your toolchain can do and `spn` will happily oblige.
[^5]: x86_64 only.
[^6]: Get bent, Apple. Seriously.
[^7]: `spn` wouldn't exist without Zig. Part of that is the massive amount of work and care taken to create `zig cc` and gift it to the world, gratis. And part of it is the attitude that simply refuses to accept the status quo of systems programming, the love and care they put into fixing it, and the simple existence proof that you can build software like this. Thank you, Zig!
[^8]: From my research, this isn't exactly correct. Many such headers are released under Apple's open source license, and from my understanding could be bundled with `spn`. However, it's an incomplete set, and more importantly they *differ* from the actual headers you might find in a real SDK. Even though it's incomplete, it's not *trivial*. But, most importantly, it doesn't cover Cocoa which is Objective-C. It is more complicated than that, since you can write C headers against `libobjc`, but very few upstream libraries actually compile against such an interface and in general I have no idea how feasible that is[^6]. I'm sure my research is scattershot compared to someone who actually understands all of this, so if you *are* that person, hit me up on [Discord](link:discord.public) or [send me an email](link:email).
