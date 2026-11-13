"""Season of Ice object families: names, sprite slots and the Spyro 2 stand-ins (placeholders).

Evidence: the sprite slot each spawner (080170a4 switch) or its callbacks reference, as
`slot << 6` constants (sprite object field +8 holds slot << 6, read by 0800540c), traced by
reachability over the Ghidra call graph (see docs/SEASON_OF_ICE_PORT.md, "Sprite banks"), plus the
rendered banks in data/soi/sprites/ (inspected). Enemy families take per-level slot 0x25 (first)
or 0x26 (second). Descriptions are visual, from the decoded sprites.

S2 stand-ins name retail classes that run natively today; placeholders are marked and are meant to
be swapped for Spyro 3 imports (Rhynocs, Bianca) once those load (user decision, 3 Nov 2026).
"""
import json
from pathlib import Path

GLIMMER, SUMMER = 17, 15

# (lo, hi): (key, name, sprite slots, evidence, s2 (scene slot, class) or None, placeholder?)
FAMILIES = {
    (1, 1): ("player", "Spyro (player)", ["0x05", "0x06", "0x10", "0x11", "0x20", "0x28"], "spawned at the start table entry by 08016594", None, False),
    (15, 15): ("sparx", "Sparx", ["0x18"], "spawned by the loader; slot 0x18 = small winged companion", None, False),
    (0, 0): ("system", "system object (slot 0x19)", ["0x19"], "spawned by the loader at (0,0,0)", None, False),
    (2, 3): ("system", "system objects", [], "spawned by the loader at fixed points", None, False),
    (21, 21): ("system", "loader-spawned helper (slots 0x18, 0x0c)", ["0x18", "0x0c"], "spawned by the loader", None, False),
    (117, 120): ("gem", "gem (1/2/5/10)", ["0x10"], "0801c474, value table 081ab8f0", (GLIMMER, 0x001), False),
    (126, 129): ("treasure_chest", "treasure chest (releases gems)", ["0x13", "0x10"], "0803694c: break sprite 0x13 + gems; text 'Treasure found:'", (GLIMMER, 0x0C8), True),
    (130, 134): ("bg_container_a", "background-drawn gem container (10 per level)", ["0x10"], "0801fbc4 uses only the gem and shadow slots; present in every map incl. homes", (GLIMMER, 0x0C8), True),
    (135, 139): ("bg_container_b", "background-drawn gem container (5 per level)", ["0x10"], "same spawner as 130-134", (GLIMMER, 0x0C8), True),
    (10, 11): ("critter", "level-themed critter (fodder, drops gems)", ["0x23", "0x10"], "080210e8 / 08020964: per-level slot 0x23 + gems", (SUMMER, 0x12C), True),
    (16, 16): ("critter", "level-themed critter (fodder; health/butterfly binding under review)", ["0x23"], "08021ec0 initializes slot 0x23, palette 10; exact damage dispatch still requires proof", (SUMMER, 0x12C), True),
    (245, 245): ("critter", "level-themed critter (fodder, drops gems)", ["0x23", "0x10"], "080217dc: slot 0x23 + gems", (SUMMER, 0x12C), True),
    (40, 81): ("portal", "level portal (destination and gate depend on type)", ["0x16"], "0801d428 initializes portal slot 0x16; 0801d764 maps destinations (type41 -> level2); 0801d8a4 handles range/transition", None, False),
    (90, 90): ("warp", "warp / vortex object (swirl 0x14, urn 0x1f)", ["0x14", "0x1e", "0x1f", "0x20"], "08036d90; 0-4 per map; identity unconfirmed", None, False),
    (115, 116): ("grendor", "Grendor (boss)", ["0x25"], "08033b80, only in the two arenas", None, True),
}
ENEMY_SLOT = {140: "0x25", 145: "0x26", 150: "0x25", 155: "0x26", 160: "0x25", 165: "0x26", 170: "0x25",
              175: "0x26", 180: "0x25", 185: "0x26", 190: "0x25", 195: "0x26", 200: "0x25", 205: "0x26",
              210: "0x26", 215: "0x25", 220: "0x26", 225: "0x25", 230: "0x26", 240: "0x25"}
ENEMY_LOOK = {140: "club-carrying caveman Rhynocs (Lava Prairie)", 145: "masked tribal Rhynocs (Lava Prairie)",
              150: "small pirate Rhynocs (Mermaid Coast)", 155: "hooded purple creatures (Mermaid Coast)",
              165: "red armoured soldiers (Hummingbird Fort)", 170: "small armoured beekeeper Rhynocs (Honey Marsh)",
              175: "giant bees (Honey Marsh)", 180: "spear guards (Market Mesa, Hummingbird Fort, Roman City)",
              185: "green-robed Rhynocs (Market Mesa)", 190: "yellow-green bandits (Panda Gardens)",
              195: "round green warriors (Panda Gardens)", 200: "cowboy Rhynocs (Dusty Trails)",
              205: "sombrero Rhynocs with projectiles (Dusty Trails)", 210: "helmeted Roman Rhynocs (Roman City)",
              215: "purple Rhynocs (Twilight Bulb Factory)", 220: "green pod robots (Time Machine Lab, Twilight Bulb Factory)",
              225: "small red robots (Star Park)", 230: "parka Rhynocs (Star Park)", 240: "masked Rhynocs (Time Machine Lab)"}


def family(t: int) -> dict:
    t &= 0xFF
    for (lo, hi), (key, name, slots, ev, s2, ph) in FAMILIES.items():
        if lo <= t <= hi:
            return dict(family=key, name=name, slots=slots, evidence=ev,
                        s2=None if s2 is None else dict(scene=s2[0], cls=f"0x{s2[1]:03x}"), placeholder=ph)
    if 140 <= t <= 244:
        base = t - t % 5
        first = ENEMY_SLOT.get(base) == "0x25"
        # Rhynoc stand-ins until the Spyro 3 imports work: Glimmer's native club / small lizards.
        s2 = (GLIMMER, 0x22B) if first else (GLIMMER, 0x1F0)
        return dict(family=f"enemy_{base}", name=ENEMY_LOOK.get(base, "enemy"), slots=[ENEMY_SLOT.get(base, "?")],
                    evidence="spawner references slot<<6 (0x940 = slot 0x25, 0x980 = slot 0x26)",
                    s2=dict(scene=s2[0], cls=f"0x{s2[1]:03x}"), placeholder=True)
    return dict(family="unknown", name="unidentified", slots=[], evidence="", s2=None, placeholder=False)


if __name__ == "__main__":
    out = {str(t): family(t) for t in range(256) if family(t)["family"] != "unknown"}
    p = Path(__file__).resolve().parents[2] / "data" / "soi" / "families.json"
    p.write_text(json.dumps(out, indent=1))
    print("wrote", p, len(out))
