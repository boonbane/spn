---
title: Package indexes
---

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
.spn
└── index
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
    "manifest": "spn.toml"
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

## Manifests in a repository

Those two indexes differ in two ways that have nothing to do with each other: where the files come from, and what the files are. A directory of manifests can live in a Git repository just as well as on your disk:

```sh
spn index add https://github.com/tspader/spam.git
```

`spn index add` clones the repository, names the index after it, works out which kind it is, and writes the entry to your manifest. `--global` writes it to the [user config file](/docs/configuration) instead, and `--name` picks a different name. For `spam`, it writes:

```toml
[[index]]
name = "spam"
url = "https://github.com/tspader/spam.git"
format = "manifests"
```

| | `format = "releases"` | `format = "manifests"` |
|---|---|---|
| `url` | The default. Release records under `.spn/index`, written by `spn publish` | A repository of `<name>/spn.toml`, held at one commit |
| `path` | The same records in a local directory | The default. A directory of `<name>/spn.toml` |

A repository of manifests has no record of its releases. The commit *is* the version of every package in it, so `spn` always resolves against exactly one. The first build takes the tip of the repository's default branch and records it in `spn.lock`:

```toml
[[index]]
name = "spam"
url = "https://github.com/tspader/spam.git"
rev = "9b3cd3ebd05f5b39df7652d8f9372eeac08471b3"
```

The index stays at that commit until `spn update` moves it to the tip. Every package you've locked from it moves with it. Each keeps its version if the repository still has it, and takes the newest one your manifest allows if not. If the new commit has nothing your manifest allows for some package, the update fails and `spn.lock` is left alone.

Only `spn update` with no names moves the index. `spn update lua` leaves it where it is, so a package from a repository of manifests doesn't change until the whole index does.

To hold an index at a commit no matter what the lockfile says, put `rev` in its `[[index]]` entry. Nothing moves an index pinned that way but you. Change `rev` and the next build re-locks its packages at the new commit; with `--locked`, that's an error instead.

## Global configuration

`[[index]]` entries in the [user config file](/docs/configuration) apply to every project on the machine. They take the same fields as `[[index]]` in a manifest, with one difference: a repository of manifests needs a `rev` here, because the user config has no lockfile to record a commit in. `spn index add --global` writes the commit it cloned.

To find a package, `spn` checks each index in turn and takes every version of it from the first index that has it. The order is:
1. The project manifest's indexes, in the order they're declared
2. The user config's indexes, in the order they're declared

There is no default index; `spn` only looks where you've told it to. A user config index with the same name as one of the project's is ignored. `spn index list` prints the indexes in this order.

## Publishing

### spn publish

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
