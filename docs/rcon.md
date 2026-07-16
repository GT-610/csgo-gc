# RCON

csgo_gc exposes an optional Source RCON-compatible control port for the local
ClientGC. It is intended for scripting, debugging, and tools that need to update
the live inventory without restarting CS:GO.

RCON is disabled by default.

## Configuration

Add an `rcon` block to `csgo_gc/config.txt`:

```text
"rcon"
{
    "enabled"      "1"
    "bind_address" "127.0.0.1"
    "port"         "37016"
    "password"     ""
}
```

Options:

- `enabled`: `1` starts the RCON listener. Missing or `0` leaves it disabled.
- `bind_address`: defaults to `127.0.0.1`. Keep this local unless you have added
  your own network protection.
- `port`: defaults to `37016`.
- `password`: Source RCON password. An empty configured password intentionally
  accepts any supplied Source RCON password for compatibility with this custom GC.

The setting is read when the GC is loaded. There is no config hot reload.

Startup logs include the configured bind address, port, protocol, and whether the
password is empty, without printing the password.

## Protocol

The listener speaks the Source RCON binary protocol:

- Little-endian size-prefixed packets.
- `SERVERDATA_AUTH` for authentication.
- `SERVERDATA_EXECCOMMAND` for commands.
- `SERVERDATA_RESPONSE_VALUE` / `SERVERDATA_AUTH_RESPONSE` for responses.

Raw newline text mode is not supported. Use a Source RCON client or a script that
implements Source RCON packets.

Example:

```powershell
rcon -a 127.0.0.1:37016 -p 1 ping
```

Expected response body:

```text
OK pong
```

Basic connectivity check:

```powershell
Test-NetConnection 127.0.0.1 -Port 37016
```

## Response Format

Command responses are plain text inside Source RCON response packets:

```text
OK <message>
ERR <message>
```

Responses are kept within one Source RCON response packet. Commands that can
produce many lines, such as inventory listing or item detail inspection, include
`shown` and `truncated` fields. `truncated=1` means output stopped before all
available lines could fit in the single response packet.

Examples:

```text
OK pong
OK item_ids=1234567890123
OK total=500 shown=48 truncated=1
ERR no client gc
ERR unknown parameter foo
```

## Commands

### `help`

Lists available commands.

### `ping`

Returns:

```text
OK pong
```

This works as long as the RCON server is online, even if no ClientGC is currently
registered.

### `status`

Returns RCON and ClientGC state. If the game client GC is online, the response
includes the local SteamID and basic inventory stats.

### `clients`

Lists the controllable ClientGC instance. v1 only controls the local ClientGC.

### `list_items`

Lists inventory items in stable item-id order.

Syntax:

```text
list_items [limit]
```

The default limit is `50`; the maximum is `500`.

The response includes `total`, `shown`, and `truncated` fields. If
`truncated=1`, reduce the limit or use `find_item` to narrow the output.

### `find_item`

Finds inventory items by exact item id, exact defindex, display name, or custom
name.

Syntax:

```text
find_item <itemid|defindex|text>
```

The response includes `total`, `shown`, and `truncated` fields. At most the
first 50 matches are considered for display, and fewer may be shown if the
single Source RCON packet budget is reached.

### `item_info`

Shows detailed information for one inventory item, including attributes and
equipped state.

Syntax:

```text
item_info <itemid>
```

The response includes `attributes`, `attr_shown`, and `truncated` fields. If an
item has unusually many or long attributes, `truncated=1` indicates that some
attribute lines were omitted from the response.

### `give_item`

Creates one or more inventory items and sends live SO create updates to the game.

Syntax:

```text
give_item <defindex> [count] [key=value...]
```

Compatibility forms:

```text
give_item 7
give_item 7 5
```

Parameterized examples:

```text
give_item 7 paint=44
give_item 7 paint=44 wear=0.12 seed=123 stattrak=5
give_item 7 paint=44 name="RCON Test"
give_item 1314 music=3 stattrak=10
give_item 7 paint=44 sticker0=12 sticker0_wear=0
give_item 4013 tournament_team0=31 tournament_team1=1
give_item 4132 tournament_team0=46 tournament_team1=1 tournament_mvp=64640068
```

Rules:

- `count` is optional and must be `1..100`.
- Parameters are `key=value`.
- Keys are case-insensitive.
- Values may be double quoted. Backslash escapes the next character inside
  quotes.
- Unknown keys return `ERR unknown parameter <key>`.
- Invalid numeric values return `ERR invalid parameter <key>`.
- Successful creation returns `OK item_ids=...`.

Supported parameters:

| Parameter | Type | Notes |
| --- | --- | --- |
| `level` | uint32 | Overrides item level. |
| `quality` | uint32 | Explicit quality override. |
| `rarity` | uint32 | Explicit rarity override. |
| `name` | string | Sets the custom item name. Quote values containing spaces. |
| `paint` | uint32 | Paint kit defindex. Must exist in the item schema. |
| `seed` | uint32 | Paint seed. |
| `wear` | float | Paint wear, `0.0..1.0`. |
| `stattrak` | uint32 | Kill count. `stattrak=1` creates StatTrak with 0 kills. |
| `music` | uint32 | Music definition id. Requires music kit defindex `1314`. |
| `spray_color` | uint32 | Graffiti tint id. |
| `spray_remaining` | uint32 | Remaining spray uses. |
| `tournament_event` | uint32 | Tournament event id. Souvenir package definitions normally provide this automatically. |
| `tournament_stage` | uint32 | Tournament event stage id for a specific match. |
| `tournament_team0` | uint32 | First tournament team id for a souvenir package. |
| `tournament_team1` | uint32 | Second tournament team id for a souvenir package. |
| `tournament_mvp` | uint32 | MVP Steam account id for Cologne 2015 through Berlin 2019 souvenir packages. |
| `sticker0`..`sticker5` | uint32 | Sticker kit defindex. Must exist in the item schema. |
| `stickerN_wear` | float | Sticker wear, `0.0..1.0`. |
| `stickerN_scale` | float | Sticker scale. |
| `stickerN_rotation` | float | Sticker rotation. |

Derived defaults:

- `paint` adds paint kit, seed, and wear attributes. If not explicitly set,
  `seed=0`, `wear=0.001`, `quality=Unique`, and painted rarity are applied.
- `stattrak` adds kill eater attributes. Non-music items use score type `0`.
- `music` adds music id and music-kit score type `1`. It requires `defindex=1314`.
- `stickerN` adds sticker id and defaults that slot's wear to `0` unless
  `stickerN_wear` is supplied.
- Explicit `quality` and `rarity` override derived defaults.
- If no parameters are supplied, the command uses the original basic purchase
  path.

See [Souvenir Packages](souvenirs.md) for complete package examples, tournament
attribute numbers, historical sticker formats, and manual `inventory.txt`
editing.

### `remove_item`

Removes an item and sends a live SO destroy update to the game.

Syntax:

```text
remove_item <itemid>
```

### `refresh_inventory`

Resends the full inventory cache subscription to the game. This is a repair and
debug command, not the normal path for adding items.

### `save_inventory`

Writes the current in-memory inventory to `csgo_gc/inventory.txt`.

Syntax:

```text
save_inventory
```

## Common Errors

```text
ERR no client gc
ERR unknown defindex
ERR unknown paint
ERR unknown music
ERR unknown sticker
ERR item not found
ERR usage: give_item <defindex> [count] [key=value...]
ERR invalid parameter wear
```

`ERR no client gc` means the RCON listener is running, but the local ClientGC has
not registered yet or has already been destroyed.

## Safety Notes

The default bind address is `127.0.0.1` because RCON can mutate local GC state and
inventory. Do not bind to a public interface without adding your own access
controls. Source RCON compatibility is provided for tooling convenience; it is
not TLS-protected.
