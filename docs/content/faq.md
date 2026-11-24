---
title: FAQ
readme: false
---

## `compile_commands.json`

`spn` emits it on every build, at your project's top level. If you were to run:

```bash
spn build
spn build --target=x86_64-windows-gnu
```

Then your `compile_commands.json` would point to MinGW stuff.

## Does it work with MSVC / Xcode?

Yes.

## You can't use Git for a package manager

Let's dig into that a little more, because it's important: **Package indexes are just Git repositories**. Repositories have some very nice properties:
  - Commits are transactional, reversible, and everything else Git does for your source code
  - It comes with provenance and auth that you already have set up
  - Every god damn developer on the planet knows how to use it

If you've ever used Homebrew and waited multiple tens of seconds for kegs to be sterilized, you may balk at this. "Spader," you might say, "Git doesn't scale for package indexes". And you'd be right! But you'd also be wrong:
- `spn` isn't tied to Git. Index backends are just interfaces; there's a stubbed out HTTP backend in there already if you want to host it on Cloudflare or whatever
- C is not JS. It's not a distro, it's not Rust. Nobody uses packages in C because there are no good package managers for all its special little quirks. We're talking orders (plural) of magnitude less scale.
