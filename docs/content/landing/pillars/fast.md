---
title: An extremely fast build system
head: spn.toml
code: fast.toml
order: 1
---

`spn` turns familiar TOML package manifests into a build graph that it schedules and runs in parallel.

It *never* wraps another build system; it runs every compile and link in your build, including those of your dependencies.

Even better, it comes with a sophisticated, CI-friendly build cache out of the box. Switching branches, worktrees, or even cross compilation targets no longer means a rebuild!
