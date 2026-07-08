# csgo_gc

> [!CAUTION]
> This project is incomplete and not ready for general use.

## What is this?
In Valve games, the Game Coordinator (GC) is a backend service most notably responsible for matchmaking and inventory management (like loadouts and skins). This project redirects the GC traffic to a custom, in-process implementation.

## Why would you want this?
While it's still possible to connect CS:GO to CS2's GC by spoofing the version number, this may break in the future if Valve updates the GC protocol. This project aims to restore most GC-related functionality without relying on a centralized server.

## Current features
- Editable inventory (`inventory.txt`)
- Live inventory updates while the game is running
- Item equipping and loadout updates
- Opening cases, sticker capsules, patch packs, graffiti boxes, music kit boxes and souvenir packages
- Souvenir item generation
- Stickers and patches
- Name tags
- Graffiti support
- Music kits
- Weapon StatTrak support
- Music kit StatTrak support, including round MVP count propagation
- Storage Units
- StatTrak Swaps
- Trade ups
- In-game store purchases
- Read-only inventory consistency diagnostics
- Source RCON-compatible local control interface
- Parameterized RCON item creation for paint kits, wear, seed, StatTrak, music kits, sprays, stickers and custom names
- Works without full Steam API emulation
- Full Windows, Linux and macOS support
- Functional lobbies
- Dedicated server support
- Functional server browser (only shows csgo_gc servers by default)
- Networking using Steam's P2P interface

## Planned features
- More validation and polish for edge-case inventory operations
- Tooling around live inventory editing

I'm still looking for the **full** CS:GO Item Schema. If you have a relatively recent copy of it and are willing to share it, let me know!

## Not planned
- Matchmaking (can't be implemented without a centralized server)

## Installation
- Download [CS:GO from Steam](steam://install/4465480)
- Download the latest release for your platform from the [releases page](https://github.com/GT-610/csgo_gc/releases/latest)
- Navigate to the game's installation directory
- Back up your existing launcher executables as they'll be overwritten (i.e. csgo.exe, srcds.exe, csgo_linux64, etc.)
- Extract the contents of the downloaded archive to your game directory, replace the executables when prompted
- Launch the game. If you get the annoying VAC message box, launch the game with the -steam argument
- macOS users: The release binaries are not notarized, so if you're using them, you'll have to deal with that somehow

## Inventory editing
Inventory can still be edited offline through `inventory.txt`. For manual editing, there is a guide made by someone else [here](https://gist.github.com/dricotec/1ae3deb06c42012970c00df914348e76).

The local RCON interface can also create and remove items while the game is running. This is intended for scripts and GUI editors that want to avoid the old edit-file-and-restart workflow.

## Configuration
See [csgo_gc/config.txt](examples/config.txt) for available options.

## RCON
RCON is disabled by default and binds to localhost with the default configuration; the actual listen address is controlled by `bind_address`. It uses the Source RCON binary protocol, so existing Source RCON clients can be used.

See [rcon.md](docs/rcon.md) for protocol details, configuration, supported parameters and error formats.

## Building
Requirements:
- Git
- CMake 3.20 or newer
- C++ compiler with C++17 support (VS 2017 or later, Clang 5 or later, GCC 7 or later)

The top-level CMake project downloads protobuf, cryptopp-cmake and funchook through `FetchContent` during configure.

The game is 32-bit on Windows so you need to build as 32-bit:

```bat
cmake -A Win32 -B build
cmake --build build --config Release --target csgo_gc
```

Linux dedicated servers are also 32-bit:

```sh
cmake -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 -B build
cmake --build build --target csgo_gc
```

On macOS, you need to build for x86_64 instead of arm64:

```sh
cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 -DFUNCHOOK_CPU=x86 -B build
cmake --build build --target csgo_gc
```

For Linux clients you don't have to specify any additional options.

For a full launcher package build, also build the launcher targets:

```sh
cmake --build build --config Release --target csgo srcds csgo_gc
```

## License
This project is licensed under the 2-Clause BSD License. See [LICENSE.md](LICENSE.md) for details.

## Credits
* **Mikko Kokko** - Original author
* **Theeto** - Code reused from the predecessor project, unusual loot lists
* **GT610** (`GT-610`) - Fork deveopment
* Contributors who continued inventory, networking, diagnostics and RCON work after the original upstream changes

## Third party dependencies
- [Crypto++](https://github.com/weidai11/cryptopp) ([Boost Software License](https://github.com/weidai11/cryptopp/blob/master/License.txt))
- [funchook](https://github.com/kubo/funchook) ([GPL v2 with Classpath Exception](https://github.com/kubo/funchook/blob/master/LICENSE))
- [diStorm3](https://github.com/gdabah/distorm) ([3-Clause BSD License](https://github.com/gdabah/distorm/blob/master/COPYING))
- [protobuf](https://github.com/protocolbuffers/protobuf) ([3-Clause BSD License](https://github.com/protocolbuffers/protobuf/blob/main/LICENSE))
