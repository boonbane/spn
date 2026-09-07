---
title: FAQ
---

## What about `compile_commands.json`?

`spn` emits it on every build, at your project's top level. If you were to run:

```bash
spn build
spn build --target=x86_64-windows-gnu
```

Then your `compile_commands.json` would point to MinGW stuff.

## Does it work with MSVC / Xcode?

Yes.

## Why wouldn't I just use...

### ...CMake?

Here we go. The big boy. CMake is as close to a standard as C and C++ have ever had. It's not that bad these days, almost in spite of itself. How could something with a wacky DSL, `CMAKE_MINIMUM_REQUIRED`, no actual build system, and a package manager which is roughly "whatever the hell your distro ships" become the standard option? Well, because it works[^1].

Well, why wouldn't you use something that works? Even if it's flawed, a flawed ecosystem that works is always better than one that fixes the warts but is fragmented or broken. In other words, it's not enough to simply fix CMake's quirks. Nobody cares that you have to write stuff like this:

```cmake
target_link_libraries(app PRIVATE libwhatever)
```

If writing that lets you *compile the fucking program*. That's all people want!

People want this so badly that even *new features* aren't enough to switch. For example, Meson lets you programmatically manipulate `meson.build`. You can hook it into an IDE and get first class support for all kinds of stuff. And nobody cares, because the whole world uses CMake and people just want to compile the program.

People only switch off of things like CMake when the new thing solves entire classes of problems for them. Meson[^2] has a lot of nice stuff, but it's still a generator. You cannot succeed writing a generator, no matter how nice, because the sum total of all that niceness is still less than CMake's inertia.

This is why `spn` is fundamentally different than every other tool in this space.

### ?
- Generators. These are programs that generate inputs to another program which actually compiles your code.
- Package managers. These are programs that know how to produce named packages. Sometimes they build them, sometimes they serve binaries.
- Executors. These are the programs that actually invoke the compiler.

| Tool          | Generator | Package manager | Executor |
|---------------|-----------|-----------------|----------|
| meson         | yes       | kind of         |          |
| xmake         | sometimes | yes             | yes      |
| premake       | yes       |                 |          |
| cmake         | yes       |                 |          |
| conan         |           | yes             |          |
| vcpkg         |           | yes             |          |
| ninja         |           |                 | yes      |
| make          |           |                 | yes      |
| msbuild       |           |                 | yes      |
| xcodebuild    |           |                 | yes      |

### ...Conan?

Conan is a package manager, not a build system. Conan packages are Python scripts which usually just orchestrate the project's native build, which means that compilation is a black box.

[^1]: The aforementioned are *not* knocks on the folks who make CMake, who have willed their product to success through an unbelievable amount of the kind of raw, dirty engineering that nobody wants to do. CMake may be good in spite of itself, but it's good *because* of its maintainers.

[^2]: If you work on Meson, I'm sorry that I'm picking on you. You have a good solution to the problem you picked and a thoughtfully written tool. I'm just arguing that it's not the right problem to begin with.
