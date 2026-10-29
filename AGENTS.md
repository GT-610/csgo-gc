This repository is developed from `main`. Keep `master` as the branch that tracks upstream-oriented integration work and DO NOT TOUCH UNLESS EXPLICITLY ASKED.

## Build Environment

For Windows builds, prefer the Visual Studio bundled CMake and initialize the MSVC developer environment first. CS:GO Windows builds are 32-bit, so use the x86 developer environment.

Set `VS_BUILDTOOLS` to the local Visual Studio BuildTools installation path and
`VCPKG_ROOT` to a vcpkg checkout before running the commands below. The vcpkg
checkout must contain the baseline recorded in `vcpkg.json`. Run commands from
the repository root.

## Recommended Agent Build

Use `build` as the agent build directory. If it was created by a different CMake, compiler, or Visual Studio installation, remove it and configure again because CMake caches absolute tool paths.

Ensure the Visual Studio bundled CMake and Ninja directories appear before
MSYS2 or other toolchains in the child process `PATH`. vcpkg builds a host
`protoc` executable as well as the x86 target library, and mixing MSYS2 CMake
with MSVC can break Windows resource compilation. If compiler discovery is
ambiguous, pass the x86 `cl.exe`, Windows SDK `rc.exe`, and `mt.exe` paths to
CMake explicitly.

```bat
cd /d <repo>

"%VS_BUILDTOOLS%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  -S . ^
  -B build ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x86-windows-static

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

## Dependency Notes

The project uses vcpkg manifest mode for:

- protobuf
- mbedTLS

Funchook and its diStorm dependency are still downloaded with `FetchContent`.

The first configure can be slow because vcpkg builds both the host `protoc`
tool and the 32-bit target libraries. Avoid interrupting an active dependency
build. If a configure is interrupted, check whether repository-specific
`cmake`, `ninja`, `vcpkg`, or `git` processes are still running before removing
the agent build directory and configuring again.

Interrupted FetchContent subbuilds may later fail with errors like:

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
  -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x86-windows-static

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

When a change affects test code or GC behavior covered by tests, also build the
affected test target and run the full configured suite:

```bat
"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  --build build ^
  --config Release ^
  --target gc_message_tests

"%VS_BUILDTOOLS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" ^
  --test-dir build ^
  -C Release ^
  --output-on-failure
```

Build-generated protobuf sources and vendored Steamworks SDK headers are noisy. When searching for project issues, usually scope searches to:

```bat
rg <pattern> csgo_gc launcher CMakeLists.txt README.md AGENTS.md
```

## Final Client Compatibility Invariants

The primary runtime target is the final September 2023 legacy CS:GO client.
Preserve the shared CS:GO GC message IDs and protobuf shapes so common
client/server functionality remains interoperable with upstream. Fork-only
state should be additive and safe for an implementation that ignores it; do
not describe this as complete feature parity without direct testing.

Inventory equipped state is class/team-specific. Do not clear another class's
state when unequipping an item, and do not publish empty SO updates when no
state changed. Default-item swaps are order-sensitive: clear the target keyed
SO before publishing a displaced default equip into its new slot.

The store currently permits one active transaction per ClientGC. Preserve the
delayed `MicroTxnAuthorizationResponse_t` callback so the client can store the
transaction ID before starting Finalize. Do not introduce a pending
authorization queue unless the single-active-transaction model changes.
Finalize must publish created inventory SOs before its success response, and a
partial creation failure must remove unpublished items and clear the pending
transaction.

## Documentation Repository

The documentation site is maintained in the separate `csgo-gc-docs`
repository. This repository currently links to the site but does not contain a
`.gitmodules` entry or a documentation gitlink. Verify the actual checkout
topology before attempting a submodule pointer update. Keep English and Chinese
pages aligned, and commit and push documentation-repository changes before
committing related README or agent-guidance changes here.

## Reverse Engineering Notes

This project is often developed alongside a local Ghidra project for the final
CS:GO client build. When Ghidra is connected through MCP, use it as the preferred way
to inspect Ghidra programs from an agent session.

Start by listing and connecting to the open Ghidra instance, then list project
files and open programs before running analysis queries. If a program was saved
with an older Ghidra language version, Ghidra may need a manual open/save or
language upgrade in the GUI before MCP tools can open it.

MCP methods varies based on the MCP server user has chosen. Recommended to use https://github.com/bethington/ghidra-mcp.

Prefer targeted analysis over full-project analysis. The CS:GO binaries are
large, and full auto-analysis can be slow. For most tasks, first search strings,
symbols, imports, and nearby functions, then analyze/decompile only the relevant
area.

The most useful reverse-engineering targets are usually:

- `client.dll`: primary client gameplay and GC-facing logic.
- `client_panorama.dll`: useful comparison target; it contains many similar GC
  strings and classes even if the running game path appears to use `client.dll`.
- `server.dll`: server-side gameplay behavior, dedicated-server interactions,
  and client/server state effects.
- `matchmaking.dll`: lobby, session, server browser, and join-data behavior.
- `engine.dll`: module loading, Steam interface access, networking, and
  client/server boundaries.
- The original game launcher executable: useful for comparing launcher,
  bootstrap, and module-loading behavior against this project's replacement
  launcher.
- `steam_api.dll`: lower-priority boundary reference for Steam interfaces such
  as `SteamGameCoordinator001`.
- Panorama runtime modules only when investigating UI/runtime behavior.

Avoid importing or analyzing every bundled third-party runtime DLL by default.
Libraries such as V8, ICU, media codecs, font/rendering libraries, and audio
middleware add noise unless the task specifically involves them.

Local reference source trees, when present, are valuable for naming and
orientation:

- Older CS:GO/source-engine code is best treated as a structure and naming
  dictionary. The `gcsdk`, `game/client`, `game/server`, `matchmaking`,
  `public`, and `engine` areas are especially useful.
- The final Panorama frontend source is useful for tracing inventory, store,
  loadout, and lobby UI behavior before diving into native Panorama binaries.

Useful GC-related anchors to search in Ghidra or reference source include:

```text
SteamGameCoordinator001
ISteamGameCoordinator
CMsgClientHello
CMsgClientWelcome
CMsgSOCacheSubscribed
SOCache
CSOEconItem
ClientJob_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello
```

When comparing the local game install with build outputs, remember that the DLL
installed into the game directory may lag behind the current `build` output.
Check file size, timestamp, or hash before assuming runtime behavior matches
the current source tree.

Some Windows agent environments may expose both `PATH` and `Path`. MSBuild can
fail with a duplicate environment key before compiling project code. If this
happens, normalize the child process environment before invoking the Visual
Studio developer prompt, for example by clearing one spelling and preserving the
other in the same `cmd` session.
