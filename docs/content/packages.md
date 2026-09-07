---
title: Packages
---

A *package* is a collection of executables and libraries, tests, and examples, called *targets*.

Packages are defined by TOML manifests.

## Targets

 They have fields like `source`, or `include`, or `system_deps`, and most fields can be configured at the package level for all targets or for an individual target.

```toml
[package]
name = "foo"
include = ['common']

[[lib]]
name = "foo"
source = ['source/lib.c']
include = ['source/lib']

[[example]]
name = "frobnicate"
source = ['example/foo/main.c']

[[test]]
name = "unit"
source = ['test/main.c', 'test/foo.c']
define = ['BAR', 'BAZ=69']
system_deps = ['m']
```

A `[[bin]]` and a `[[script]]`, for instance, are both executables, but their difference isn't strictly cosmetic. `[[bin]]` entries are taken to be *exports* of your package, able to be pulled in by consumers. `[[script]]` entries aren't. `[[test]]` entries are executed in `spn test`, and compiled to `test/` instead of `bin/`.

### Publishing headers

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

### Linkage

Targets declare what kinds of linkage they support:
- `static`
- `shared`
- `object`, which skips the linker and directly publishes object files to `lib/`
- `source`, which means that the component object files are compiled directly into your program

These kinds are opt-in; there's no default, and you must declare what you support. A target can also set `link = false` to simply compile the sources without linking them[^1].

### Dependencies

### define and flags

### system_deps


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

Consumers can then [set these options](/docs/dependencies#options)

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
