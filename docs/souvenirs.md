# Souvenir Packages

csgo_gc can open legacy CS:GO souvenir packages and generate the tournament
metadata and stickers expected for their Major era. The package definition
selects the map collection and tournament, while attributes on that particular
package describe the match.

The package does not need a `loot list` attribute. Loot lists are GC-side data:
`gc_loot_lists.txt` decides which weapons can be produced, and the package's
tournament attributes decide which souvenir stickers are applied to the result.

## Creating a Package with RCON

Use `give_item` with a souvenir package defindex and the teams from the match:

```powershell
rcon -a 127.0.0.1:37016 -p 1 "give_item 4013 tournament_team0=31 tournament_team1=1"
```

Package `4013` is the EMS One Katowice 2014 Souvenir Package. Its item
definition already supplies tournament event `3`, so `tournament_event=3` is
optional. This example produces the dedicated Katowice 2014 event sticker and
the Virtus.Pro and Ninjas in Pyjamas Foil team stickers when the package is
opened.

The supported tournament parameters are:

| Parameter | Attribute | Meaning |
| --- | ---: | --- |
| `tournament_event` | 137 | Tournament event id. Usually inherited from the package definition. |
| `tournament_stage` | 138 | Tournament stage id. Optional; retained on the resulting souvenir. |
| `tournament_team0` | 139 | First team id. |
| `tournament_team1` | 140 | Second team id. |
| `tournament_mvp` | 223 | MVP Steam account id for autograph-era souvenirs. |

These values identify tournament records, not sticker-kit defindexes. csgo_gc
looks up the correct sticker variants in `items_game.txt`.

### Cologne 2015 through Berlin 2019

Souvenirs from the autograph era use a fourth Gold sticker belonging to the
match MVP. Supply the player's 32-bit Steam account id with `tournament_mvp`:

```powershell
rcon -a 127.0.0.1:37016 -p 1 "give_item 4132 tournament_team0=46 tournament_team1=1 tournament_mvp=64640068"
```

In this example:

- `4132` is the ESL One Cologne 2015 Mirage Souvenir Package.
- Team `46` is Team EnVyUs.
- Team `1` is Ninjas in Pyjamas.
- Account id `64640068` selects the Gold kennyS autograph for event `7`.

If `tournament_mvp` is omitted, the package still opens, but the resulting item
only has the event and two team stickers and is not a complete match-specific
souvenir for this era.

### Stockholm 2021 and Later CS:GO Majors

Beginning with Stockholm 2021, the fourth sticker is the Gold map sticker, not
an MVP autograph. csgo_gc derives the map automatically from the package
definition:

```powershell
rcon -a 127.0.0.1:37016 -p 1 "give_item 4810 tournament_team0=12 tournament_team1=59"
```

Package `4810` is the Stockholm 2021 Mirage package. This produces the event,
Natus Vincere, G2 Esports, and `de_mirage_gold` stickers. Do not supply
`tournament_mvp` for this format.

## Editing `inventory.txt`

The same metadata can be added manually to a souvenir package in
`csgo_gc/inventory.txt`. For an autograph-era package, its `attributes` block
can contain:

```text
"attributes"
{
    "137"    "7"
    "139"    "46"
    "140"    "1"
    "223"    "64640068"
}
```

Attribute `137` can be omitted when the correct package definition already
contains the tournament event. The values must still describe a sensible
match: csgo_gc resolves sticker kits but does not verify that the specified
teams and MVP actually played one another historically.

After creating a package through RCON, inspect and persist it with:

```powershell
rcon -a 127.0.0.1:37016 -p 1 "item_info <item-id>"
rcon -a 127.0.0.1:37016 -p 1 save_inventory
```

## Sticker Formats by Era

The finish and fourth sticker changed during CS:GO's lifetime. csgo_gc selects
these variants automatically:

| Tournament era | Generated stickers |
| --- | --- |
| DreamHack Winter 2013 | Tournament-themed sticker; no team metadata was available. |
| EMS Katowice and ESL One Cologne 2014 | Dedicated event sticker and two team Foil stickers. |
| DreamHack Winter 2014 | Event sticker and two team Gold stickers. |
| ESL One Katowice 2015 | Event Gold and two team Gold stickers. |
| ESL One Cologne 2015 through Berlin 2019 | Event Gold, two team Golds, and an MVP Gold autograph. |
| Stockholm 2021 through Paris 2023 | Event Gold, two team Golds, and a Gold map sticker. |

Early events contain multiple sticker finishes with the same tournament/team
metadata. Selecting the first matching schema entry is incorrect and is also
nondeterministic when entries are stored in an unordered map. The implementation
therefore selects the era-appropriate suffix (`_foil` or `_gold`) and randomly
chooses only where the historical format has multiple valid event designs.

## Notes for Maintainers

- Sticker-kit tournament fields are parsed from `items_game.txt`:
  `tournament_event_id`, `tournament_team_id`, and `tournament_player_id`.
- Attribute `223`, `tournament mvp account id`, supplies the match-specific
  player for the 2015-2019 autograph format.
- The standalone map Gold stickers (`de_mirage_gold`, `de_nuke_gold`, and so
  on) do not carry a tournament id. For Stockholm 2021 and later packages, the
  map is derived from the package name such as
  `crate_stockh2021_promo_de_mirage`.
- Package loot contents and souvenir sticker generation are separate. Missing
  server-only package contents belong in `gc_loot_lists.txt`; sticker selection
  should remain schema-driven rather than be duplicated in an external rules
  file.

Research references:

- [Valve: The Major is Back (Stockholm 2021)](https://store.steampowered.com/news/app/730/view/4103335143107243030)
- [Valve: The Paris 2023 Major](https://store.steampowered.com/news/app/730/view/5138087875081246097)
- [Final CS:GO `items_game.txt` tracked by SteamTracking](https://github.com/SteamTracking/GameTracking-CS2/blob/e63fc7fdb8dfb4f1873f0db214c0cc16613d5beb/csgo/scripts/items/items_game.txt)
- [Structured sticker-kit export used for cross-checking variants](https://github.com/zwolof/schema-gen/blob/054ce0d1c0b20ab18927d9aa59922abb5d2566c8/exported/sticker_kits.json)
