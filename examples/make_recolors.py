#!/usr/bin/env python3
"""Recolours the game's own pattern finishes into new custom finishes and cases.

No new textures are needed: hydrographic and anodized finishes are a pattern texture painted with
four colours, so the same pattern with other colours is a new finish.

    python make_recolors.py <items_game.txt> <csgo_english.txt> <out_dir>

writes   <out_dir>/recolor_paint_kits.txt   paste into csgo_gc/custom_paint_kits.txt
         <out_dir>/recolor_cases.txt        paste into csgo_gc/custom_cases.txt

The output is deterministic, so running it again gives the same finishes. Needs only the standard library.
"""
import colorsys
import random
import re
import sys

STYLES_USED = {"2", "3"}       # hydrographic, anodized multicolored: both work on any weapon
SKIP_KEYS = {"use_normal"}
# a pattern made for one weapon's model looks wrong on the others
WEAPON_WORDS = ("ak47", "ak_", "awp", "m4a1", "m4a4", "deagle", "scar20", "g3sg1", "p250", "p90", "mp9", "mp7", "mac10", "ump",
                "famas", "galil", "aug_", "sg553", "sg556", "ssg08", "nova", "xm1014", "mag7", "sawedoff", "negev", "m249",
                "glock", "usp", "p2000", "fiveseven", "tec9", "cz75", "elite", "bizon", "revolver", "knife", "glove")
COLOR_WORDS = {"red", "blue", "green", "orange", "yellow", "pink", "purple", "black", "white", "gray", "grey", "brown",
               "tan", "gold", "silver", "teal", "violet", "crimson", "azure", "emerald", "amber", "rose", "ash"}
# rarity, how many of each, and how much of the (best first) pattern list the slot picks from
CASE_SHAPE = [("ancient", 2, 20), ("legendary", 3, 45), ("mythical", 5, 80), ("rare", 6, 1000)]
RARITY_RANK = {"legendary": 5, "mythical": 4, "rare": 3, "uncommon": 2, "common": 1}

# theme: (hue in degrees, saturation, hue spread across the four colours). The original colours keep their
# brightness, so a finish keeps its shading and only changes hue
THEMES = {
    "Crimson": (355, 0.78, 14),
    "Ember": (18, 0.82, 14),
    "Amber": (40, 0.80, 12),
    "Lime": (82, 0.70, 18),
    "Emerald": (148, 0.68, 16),
    "Teal": (178, 0.70, 14),
    "Azure": (208, 0.74, 16),
    "Cobalt": (226, 0.78, 12),
    "Violet": (272, 0.62, 18),
    "Magenta": (316, 0.66, 16),
    "Rose": (338, 0.60, 12),
    "Ash": (210, 0.10, 0),
}

# two-tone themes for the knives: (first hue, second hue, saturation). marbled and Doppler patterns are made of two
# families of colour, so these keep that look. the colours alternate between the two hues by brightness
DUO_THEMES = {
    "Aurora": (148, 272, 0.70),
    "Sunset": (18, 316, 0.78),
    "Glacier": (190, 214, 0.55),
    "Toxic": (82, 272, 0.72),
    "Inferno": (355, 40, 0.80),
    "Midnight": (226, 316, 0.74),
}

# the official knife finishes that are made of colours, and what to call them once recoloured
KNIFE_BASES = {
    "am_blackpearl_marbleized": "Pearl Marble", "am_doppler_phase1": "Doppler", "am_doppler_phase2": "Doppler Swirl",
    "am_doppler_phase3": "Doppler Flow", "am_doppler_phase4": "Doppler Drift", "am_marble_fade": "Marble Fade",
    "am_ruby_marbleized": "Marbleized", "am_sapphire_marbleized": "Marbled Glass", "am_zebra": "Slaughter",
    "an_tiger_orange": "Tiger Tooth", "aa_fade": "Fade", "aq_blued": "Blued Steel", "aq_damascus": "Damascus",
    "aq_damascus_90": "Damascus Fold", "aq_forced": "Stained", "aq_oiled": "Hardened", "aq_steel_knife": "Steel",
    "hy_webs": "Web",
}
KNIFE_WEAPONS = ["weapon_bayonet", "weapon_knife_flip", "weapon_knife_gut", "weapon_knife_karambit", "weapon_knife_m9_bayonet",
                 "weapon_knife_tactical", "weapon_knife_butterfly", "weapon_knife_falchion", "weapon_knife_push",
                 "weapon_knife_survival_bowie", "weapon_knife_stiletto", "weapon_knife_ursus", "weapon_knife_gypsy_jackknife",
                 "weapon_knife_widowmaker", "weapon_knife_skeleton", "weapon_knife_canis", "weapon_knife_cord",
                 "weapon_knife_outdoor", "weapon_knife_css"]

CASES = [
    ("kaleido", "Kaleido Case", "4003"),
    ("dye", "Dye Case", "4009"),
    ("tincture", "Tincture Case", "4005"),
    ("pigment", "Pigment Case", "4017"),
]

# more cases, each with finishes of its own and a set of brand new knife finishes as its golds
NEW_CASES = [
    ("aurora", "Aurora Case", "4002"),
    ("nebula", "Nebula Case", "4004"),
    ("eclipse", "Eclipse Case", "4011"),
    ("solstice", "Solstice Case", "4018"),
    ("zephyr", "Zephyr Case", "4010"),
    ("paragon", "Paragon Case", "4001"),
]
KNIFE_FINISHES_PER_CASE = 12
KNIVES_PER_FINISH = 2

# knife and glove finishes for the golds, spread over the types
GOLD_KITS = {
    "weapon_bayonet": ["am_ruby_marbleized", "am_sapphire_marbleized", "am_emerald_marbleized", "aa_fade", "am_doppler_phase1"],
    "weapon_knife_flip": ["am_ruby_marbleized", "am_sapphire_marbleized", "aa_fade", "am_doppler_phase2", "hy_ocean_knife"],
    "weapon_knife_gut": ["am_emerald_marbleized", "aa_fade", "am_doppler_phase3", "hy_gamma_doppler_phase1"],
    "weapon_knife_karambit": ["am_ruby_marbleized", "am_sapphire_marbleized", "aa_fade", "am_doppler_phase4"],
    "weapon_knife_m9_bayonet": ["am_ruby_marbleized", "am_emerald_marbleized", "aa_fade", "am_doppler_phase1"],
    "weapon_knife_tactical": ["am_ruby_marbleized", "aa_fade", "am_doppler_phase2"],
    "weapon_knife_butterfly": ["am_sapphire_marbleized", "aa_fade", "am_doppler_phase3"],
}

TOKEN = re.compile(r'"([^"]*)"\s*"([^"]*)"')


def parse_blocks(text, name):
    """The first block called `name` as {kit id: {key: value}} (one level of children)."""
    start = text.index('"%s"' % name)
    start = text.index("{", start)
    depth, i = 0, start
    while True:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        elif c == '"':
            i = text.index('"', i + 1)
        i += 1


def kits_and_rarities(items_game):
    kits = {}
    block = parse_blocks(items_game, "paint_kits")
    for kit_id, body in re.findall(r'"(\d+)"\s*\{(.*?)\n\t\t\}', block, re.S):
        values = dict(TOKEN.findall(body))
        if "name" in values:
            kits[values["name"]] = values
    rarity_block = parse_blocks(items_game, "paint_kits_rarity")
    rarities = dict(TOKEN.findall(rarity_block))
    return kits, rarities


def read_titles(path):
    raw = open(path, "rb").read()
    text = raw.decode("utf-16") if raw[:2] == b"\xff\xfe" else raw.decode("utf-8", "replace")
    titles = {}
    for key, value in TOKEN.findall(text):
        if key.lower().startswith("paintkit_") and key.lower().endswith("_tag"):
            titles[key[len("PaintKit_"):-len("_Tag")].lower()] = value
    return titles


def plain_title(title):
    """The pattern's name without its old colour word, so "Red Laminate" can become "Laminate Amber"."""
    words = title.split()
    kept = [w for w in words if w.lower().strip("(),") not in COLOR_WORDS]
    return " ".join(kept) if kept else title


def brightness(color):
    r, g, b = (float(v) for v in color.split()[:3])
    return 0.299 * r + 0.587 * g + 0.114 * b


def recolor(values, theme):
    """The kit's four colours with the theme's hue, brightness kept. The colours are spread a little around
    the hue, brightest ones toward the warm side, so a finish isn't flat."""
    if theme in DUO_THEMES:
        return recolor_duo(values, DUO_THEMES[theme])
    hue, saturation, spread = THEMES[theme]
    originals = [values["color%d" % i] for i in range(4)]
    order = sorted(range(4), key=lambda i: brightness(originals[i]))
    out = [None] * 4
    for rank, index in enumerate(order):
        parts = originals[index].split()
        r, g, b = (float(v) / 255.0 for v in parts[:3])
        value = max(r, g, b)
        h = (hue + spread * (rank / 3.0 - 0.5)) % 360
        sat = saturation
        if value > 0.85:
            sat *= 0.6      # keep the light colours from going neon or pastel
        elif value < 0.2:
            sat *= 0.85
        value = min(0.97, 0.12 + 0.88 * value ** 0.85)   # lift the darkest colours off pure black
        rr, gg, bb = colorsys.hsv_to_rgb(h / 360.0, sat, value)
        out[index] = " ".join([str(round(rr * 255)), str(round(gg * 255)), str(round(bb * 255))] + parts[3:])
    return out


def recolor_duo(values, theme):
    """Like recolor, but the four colours alternate between two hues: dark and light ones in the first family,
    the in-between ones in the second, so a marbled pattern keeps two colours."""
    hue_a, hue_b, saturation = theme
    originals = [values["color%d" % i] for i in range(4)]
    order = sorted(range(4), key=lambda i: brightness(originals[i]))
    out = [None] * 4
    for rank, index in enumerate(order):
        parts = originals[index].split()
        r, g, b = (float(v) / 255.0 for v in parts[:3])
        value = max(r, g, b)
        sat = saturation * (0.6 if value > 0.85 else 0.85 if value < 0.2 else 1.0)
        value = min(0.97, 0.12 + 0.88 * value ** 0.85)
        hue = hue_a if rank in (0, 3) else hue_b
        rr, gg, bb = colorsys.hsv_to_rgb(hue / 360.0, sat, value)
        out[index] = " ".join([str(round(rr * 255)), str(round(gg * 255)), str(round(bb * 255))] + parts[3:])
    return out


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1

    items_game = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    titles = read_titles(sys.argv[2])
    kits, rarities = kits_and_rarities(items_game)

    candidates = []
    for name, values in sorted(kits.items()):
        if values.get("style") not in STYLES_USED or not all("color%d" % i in values for i in range(4)):
            continue
        if SKIP_KEYS & set(values) or name not in rarities or name.lower() not in titles:
            continue
        if any(word in name for word in WEAPON_WORDS):
            continue
        # a pattern that is dark all over, or flat, looks dull in any colour
        levels = [brightness(values["color%d" % i]) for i in range(4)]
        if max(levels) < 110 or max(levels) - min(levels) < 45:
            continue
        candidates.append(name)

    # the game's showier patterns first
    candidates.sort(key=lambda n: (-RARITY_RANK.get(rarities[n], 0), n))
    print(len(candidates), "patterns to recolour")

    rng = random.Random(20260514)
    themes = list(THEMES)
    used = set()
    kit_text, case_text = [], []

    def make_kit(base, theme, rarity):
        key = "rc_%s_%s" % (base, theme.lower())
        if key in used:
            return None
        used.add(key)
        values = kits[base]
        lines = ['"%s"' % key, "{", '\t"title"\t"%s %s"' % (plain_title(titles[base.lower()]), theme),
                 '\t"rarity"\t"%s"' % rarity]
        colors = recolor(values, theme)
        for name, value in values.items():
            if name in ("name", "description_string", "description_tag"):
                continue
            if name.startswith("color") and name[5:].isdigit():
                value = colors[int(name[5:])]
            lines.append('\t"%s"\t"%s"' % (name, value))
        kit_text.append("\n".join(lines) + "\n}\n")
        return key

    weapons = ["weapon_ak47", "weapon_m4a1", "weapon_m4a1_silencer", "weapon_awp", "weapon_deagle", "weapon_usp_silencer",
               "weapon_glock", "weapon_p250", "weapon_fiveseven", "weapon_tec9", "weapon_cz75a", "weapon_mp9", "weapon_mac10",
               "weapon_mp7", "weapon_ump45", "weapon_p90", "weapon_bizon", "weapon_famas", "weapon_galilar", "weapon_aug",
               "weapon_sg556", "weapon_ssg08", "weapon_nova", "weapon_xm1014", "weapon_mag7", "weapon_sawedoff", "weapon_negev",
               "weapon_m249", "weapon_scar20", "weapon_g3sg1", "weapon_mp5sd", "weapon_hkp2000", "weapon_elite", "weapon_revolver"]

    for case_key, case_name, based_on in CASES:
        lines = ['"%s"' % case_key, "{", '\t"based_on"\t"%s"' % based_on, '\t"name"\t"%s"' % case_name, "", '\t"items"', "\t{"]
        case_weapons = weapons[:]
        rng.shuffle(case_weapons)
        picked_bases = set()
        for rarity, count, window in CASE_SHAPE:
            lines.append("\t\t// %s" % {"ancient": "Covert", "legendary": "Classified", "mythical": "Restricted", "rare": "Mil-Spec"}[rarity])
            for _ in range(count):
                pool = [n for n in candidates[:window] if n not in picked_bases]
                base = rng.choice(pool)
                picked_bases.add(base)
                theme = rng.choice(themes)
                key = make_kit(base, theme, rarity)
                while key is None:
                    theme = rng.choice(themes)
                    key = make_kit(base, theme, rarity)
                lines.append('\t\t"[%s]%s"\t\t"%s"' % (key, case_weapons.pop(), rarity))
        lines += ["\t}", "", '\t"golds"', "\t{"]
        for weapon, names in GOLD_KITS.items():
            for name in names:
                if name in kits:
                    lines.append('\t\t"[%s]%s"\t\t""' % (name, weapon))
        lines += ["\t}", "}", ""]
        case_text.append("\n".join(lines))

    # ---- more cases: new finishes of their own, and new knife finishes as the golds ----
    knife_weapons = [w for w in KNIFE_WEAPONS if '"name"\t\t"%s"' % w in items_game]
    knife_bases = [b for b in KNIFE_BASES if b in kits and all("color%d" % i in kits[b] for i in range(4))]
    knife_themes = list(THEMES) + list(DUO_THEMES)
    knife_rng = random.Random(20260707)
    used_knife = set()

    def knife_ok(base, theme):
        """A recolour that isn't used yet and has a bright colour to show (these patterns have dark bases)."""
        return ("rk_%s_%s" % (base, theme.lower())) not in used_knife \
            and max(brightness(c) for c in recolor(kits[base], theme)) >= 110

    def make_knife_kit(base, theme):
        key = "rk_%s_%s" % (base, theme.lower())
        if key in used_knife:
            return None
        values = kits[base]
        # these patterns have dark bases: skip a recolour that has no bright colour to show
        if max(brightness(c) for c in recolor(values, theme)) < 110:
            return None
        used_knife.add(key)
        lines = ['"%s"' % key, "{", '\t"title"\t"%s %s"' % (KNIFE_BASES[base], theme), '\t"rarity"\t"ancient"']
        colors = recolor(values, theme)
        for name, value in values.items():
            if name in ("name", "description_string", "description_tag"):
                continue
            if name.startswith("color") and name[5:].isdigit():
                value = colors[int(name[5:])]
            lines.append('\t"%s"\t"%s"' % (name, value))
        kit_text.append("\n".join(lines) + "\n}\n")
        return key

    for case_key, case_name, based_on in NEW_CASES:
        lines = ['"%s"' % case_key, "{", '\t"based_on"\t"%s"' % based_on, '\t"name"\t"%s"' % case_name, "", '\t"items"', "\t{"]
        case_weapons = weapons[:]
        rng.shuffle(case_weapons)
        picked_bases = set()
        for rarity, count, window in CASE_SHAPE:
            lines.append("\t\t// %s" % {"ancient": "Covert", "legendary": "Classified", "mythical": "Restricted", "rare": "Mil-Spec"}[rarity])
            for _ in range(count):
                pool = [n for n in candidates[:window] if n not in picked_bases]
                base = rng.choice(pool)
                picked_bases.add(base)
                theme = rng.choice(themes)
                key = make_kit(base, theme, rarity)
                while key is None:
                    theme = rng.choice(themes)
                    key = make_kit(base, theme, rarity)
                lines.append('\t\t"[%s]%s"\t\t"%s"' % (key, case_weapons.pop(), rarity))

        lines += ["\t}", "", '\t"golds"', "\t{"]
        weapons_pool = knife_weapons[:]
        knife_rng.shuffle(weapons_pool)
        bases_pool = knife_bases[:]
        knife_rng.shuffle(bases_pool)
        for n in range(KNIFE_FINISHES_PER_CASE):
            # the next pattern that still has a recolour to give
            key = None
            for step in range(len(bases_pool)):
                base = bases_pool[(n + step) % len(bases_pool)]
                options = [t for t in knife_themes if knife_ok(base, t)]
                if options:
                    key = make_knife_kit(base, knife_rng.choice(options))
                    break
            if key is None:
                break
            for _ in range(KNIVES_PER_FINISH):
                if not weapons_pool:
                    weapons_pool = knife_weapons[:]
                    knife_rng.shuffle(weapons_pool)
                lines.append('\t\t"[%s]%s"\t\t""' % (key, weapons_pool.pop()))
        lines += ["\t}", "}", ""]
        case_text.append("\n".join(lines))

    out = sys.argv[3].rstrip("/\\")
    header = ("// Recolours of the game's own pattern finishes, made by make_recolors.py.\n"
              "// No textures needed: the same pattern painted with other colours.\n\n")
    open(out + "/recolor_paint_kits.txt", "w", newline="\n").write(header + "\n".join(kit_text))
    open(out + "/recolor_cases.txt", "w", newline="\n").write(header + "\n".join(case_text))
    print(len(kit_text), "finishes,", len(case_text), "cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
