---
title: A package manager built for C and C++
head: spn.toml
code: packages.toml
order: 4
---
C does not have a package culture. Instead of forcing a central registry, `spn` provides simple primitives you can use to publish and consume code.


A package index is just a Git repository of manifests. Use [the one we maintain](https://github.com/boonbane/spam), host your own, or skip the index and point a manifest at a commit.

Dependencies are built from source with the options you choose, pinned in a lockfile, and patched without forking.
