# csgo_gc

> [!CAUTION]
> This project is under active development and is approaching stability, but issues may still exist. Back up your game files before installation.

> [!NOTE]
> Docs are at https://csgo-gc.gt610.dpdns.org/.

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

## Currently not planned
- Matchmaking (can't be implemented without a centralized server, possibly will be considered in the future)

## Installation
See [User Guide](https://csgo-gc.gt610.dpdns.org/user/).

## Inventory editing
Inventory can still be edited offline through `inventory.txt`. For manual editing, there is a guide made by someone else [here](https://gist.github.com/dricotec/1ae3deb06c42012970c00df914348e76). For GUI editors, see [this issue](https://github.com/mikkokko/csgo_gc/issues/82).

The local RCON interface can also create and remove items while the game is running. This is intended for scripts and GUI editors that want to avoid the old edit-file-and-restart workflow.

See [Souvenir Packages](https://csgo-gc.gt610.dpdns.org/user/souvenirs) for tournament attributes, examples
for different Major eras, manual `inventory.txt` editing, and an explanation of
how package loot lists differ from souvenir sticker metadata.

## Configuration
See [Configuration](https://csgo-gc.gt610.dpdns.org/user/configuration) for available options.

Supplemental server-only loot lists that are missing from the client schema are
stored in [csgo_gc/gc_loot_lists.txt](examples/gc_loot_lists.txt). An external
loot list can include an `item_sets` block to reuse collections from
`items_game.txt` without duplicating every item in the collection.

## RCON
RCON is disabled by default and binds to localhost with the default configuration; the actual listen address is controlled by `bind_address`. It uses the Source RCON binary protocol, so existing Source RCON clients can be used.

See [RCON user guide](https://csgo-gc.gt610.dpdns.org/user/rcon) for protocol details, configuration, supported parameters and error formats.

## Building
See [Building](https://csgo-gc.gt610.dpdns.org/developer/building) for building instructions.

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
