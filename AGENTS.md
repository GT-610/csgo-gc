# Agent Notes

This repository is developed from `main`. Keep `master` as the branch that tracks upstream-oriented integration work and DO NOT TOUCH UNLESS EXPLICITLY ASKED.

## Build Environment

For Windows builds, prefer the Visual Studio bundled CMake and initialize the MSVC developer environment first. CS:GO Windows builds are 32-bit, so use the x86 developer environment.

Set `VS_BUILDTOOLS` to the local Visual Studio BuildTools installation path before running the commands below. Run commands from the repository root.

## Recommended Agent Build

Use `build` as the agent build directory. If it was created by a different CMake, compiler, or Visual Studio installation, remove it and configure again because CMake caches absolute tool paths.

```bat
cd /d <repo>

"%VS_BUILDTOOLS%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  -S . ^
  -B build ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  --build build ^
  --config Release ^
  --target csgo_gc
```

If a full package-style build is needed, build the launcher targets too:

```bat
"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  --build build ^
  --config Release ^
  --target csgo srcds csgo_gc
```

## FetchContent Notes

The top-level CMake project downloads dependencies with `FetchContent`:

- protobuf
- cryptopp-cmake
- funchook

The first configure can be slow because these dependencies are cloned. If configure appears stuck, check whether it is still downloading before interrupting it.

If a configure run is interrupted, Ninja may later fail inside `_deps/*-subbuild` with errors like:

```text
ninja: error: failed recompaction: Permission denied
```

In that case, stop only the build processes that belong to this repository, then remove the agent build directory and configure again:

```bat
rmdir /s /q build
```

Avoid killing all `git`, `cmake`, or `ninja` processes by name unless the user confirms they are unrelated to other work.

## Alternative Visual Studio Generator

If Ninja is problematic, use the Visual Studio generator:

```bat
cd /d <repo>

"%VS_BUILDTOOLS%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  -S . ^
  -B build ^
  -G "<installed Visual Studio generator>" ^
  -A Win32

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  --build build ^
  --config Release ^
  --target csgo_gc
```

## Practical Checks

Before making changes:

```bat
git status --short --branch
```

After changing C++ code, at minimum build:

```bat
"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  --build build ^
  --config Release ^
  --target csgo_gc
```

Generated protobuf sources and vendored Steamworks SDK headers are noisy. When searching for project issues, usually scope searches to:

```bat
rg <pattern> csgo_gc launcher CMakeLists.txt README.md AGENTS.md
```
