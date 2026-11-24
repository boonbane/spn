---
title: An extremely fast build system
head: spn.toml
code: fast.toml
order: 1
---
Write project manifests in TOML, and `spn` turns them into a build graph and executes it in parallel. You don't have to envy Rust programmers any more.

`spn`'s build executor is as fast as Ninja, but since it owns your entire build, it can aggressively parallelize and cache in ways that other tools simply aren't able to.

It's really, *really* fast.
