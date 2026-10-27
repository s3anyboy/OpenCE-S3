# The Shadowrun prototype's maps

The native builds load and run the maps of the Shadowrun prototype: FASA's
2005 prototype of Shadowrun, built on Halo's Xbox engine as it was in
December 2001 (build `01.12.07.0132`, "Test SP Prototype"). Its maps are Xbox
caches (version 5), as this build's are. What differs is where they are
linked in memory, a few enums Shadowrun extended, its script functions, and
the tag groups it added. This document says what is done about each, and
what does not work.

The maps examined are the prototype's `missionlet.map` (the level
`levels\test\hangemhigh\missionlet`) and its `ui.map`.

## Running a map

Put the map in the data root's `maps` folder, beside the game's own: the
Xbox caches' loader reads it from there. It is listed in the map lists'
CUSTOM SINGLEPLAYER (New Game's, and the Map screen's), with the Custom
Edition campaign maps, and played as they are: alone at the difficulty
chosen next, or hosted as network co-op (not tried). As a Custom Edition
map can, it may have beside it `<name>.bmp`, its picture, and `<name>.txt`,
its description (without one: "Shadowrun prototype map"). Its level name is
`shadowrun\<name>` (`custom_edition_maps.h`, `SHADOWRUN_LEVEL_NAME_PREFIX`),
which also loads it from the console or `init.txt`, as its own scenario path
does:

```
map_name shadowrun\missionlet
map_name levels\test\hangemhigh\missionlet
```

A map is listed when its header is a Shadowrun prototype map's of a solo
scenario: an Xbox cache (version 5) of the build `01.12.07.0132`
(`shadowrun_cache_header_campaign`); only the header is read. A map named
as one of the game's own levels (`ui`, or a name holding a campaign level's,
such as `a10`) is not listed, nor is a copy whose header was edited to pass
for another map (renamed `a10`, with the retail build's string): such a copy
still loads by its level name, since the loader tells the prototype's maps
apart by where their tags are linked, not by their headers.

The map may be compressed, as the prototype's are, or decompressed (by a
tool, as copies of it are passed around): precaching copies an
uncompressed map's bytes as they are, where it inflates a compressed one's
(`cache_files_decompress_windows.c`, `cache_copy_stored`).
Shadowrun's `ui.map` also runs as the game's `ui.map`, and the main menu
loads from it.

The level's encounters are all placed by its scripts, as the player walks
into their trigger volumes. Its scripts give the player magic with
Shadowrun's own functions (see [Scripts](#scripts) and [Magic](#magic)).

## What differs, and what is done

### Where its tags are linked

An Xbox cache's tag data is linked to the address the game reads it to: the
first word of its tag header is the address of the tag table, which follows
the 0x24-byte header. This build's maps link it to the base of the tag cache,
`0x803A6000`. The prototype's game state is larger, so its tag cache starts
higher, and its maps link their tag data to `0x8069E000`. That is still
inside this build's tag cache (`0x803A6000` to `0x819A6000`): missionlet's
16 MB of tag data ends at `0x8169C834`.

So the tags are read to the tag cache's base, as always, and then moved
where they are linked (`cache_files.c`, `cache_files_tags_place`;
`cache_file_formats.c`, `xbox_cache_tag_data_address`). Nothing in them
needs changing. The tag validator checks them there, with the tag cache from
there to its end (`tag_validate_linked_tags`).

### Where its structure BSPs are linked

The prototype links its structure BSPs past the end of the tag data, where
its own tag cache had room: missionlet's 1.4 MB BSP to `0x81EBB800`, the
`ui.map`'s to `0x8201A800`. Both are past this build's tag cache, where its
game state is (`0x81A00000`). Each BSP that would load outside the free room
after the tags is loaded as low in that room as it fits instead, with the
same offset in its 4096-byte page so that every alignment within it holds
(`xbox_cache_structure_bsp_address`). The scenario's reference is given the
new address (`cache_files_structure_bsps_place`).

As each BSP is read, before anything checks or reads it, every pointer in it
that points into where it was linked is moved by the same distance
(`tag_validate.c`, `tag_validate_relocate_structure_bsp`):

- the BSP header's structure, vertex buffer and index buffer pointers, and
  each buffer's data;
- every block and data pointer that the structure BSP's schema names, walked
  as the validator walks it;
- the pointers that the schema's checks follow but its fields don't name:
  each lightmap material's vertex buffers' base addresses and hardware
  buffers (`tag_schema_collision.c`,
  `tag_schema_structure_bsp_vertex_buffer_pointers`).

Pointers that point anywhere else (into the tags, which did not move) are
left alone. A BSP whose header is damaged, or whose new place overlaps where
it was linked (a pointer moved there would be moved again), is refused. The
walk is bounded by the BSP's size.

After it, every aligned word of the BSP is checked: in missionlet's, one
still holds a value in the range it was linked to, `0x8200FF00`, among
vertex colours (`ff00ff00 ffb45a00 8200ff00 ff00ff01`). It is not a pointer.
The tag validator then checks the BSP as it checks every map's, and the
game's geometry checks agree (structure culling: 0 of 10976 triangles
disagree with their vertex normals).

### Its tags

The groups this build knows are laid out as its own: the tag validator
passes every one of missionlet's 3144 tags and `ui.map`'s, with the
corrections below.

Shadowrun's own groups, which this build has no schema or code for, are left
as they are. Nothing reads them:

| Group | Name | In missionlet |
| --- | --- | --- |
| `srwd` | Shadowrun weapon | 23 |
| `srud` | Shadowrun unit | 4 |
| `srac` | Shadowrun actor | 10 |
| `m1ed`, `m2ed`, `m3ed` | magic effect data | 3, 2, 1 |
| `t1ed`, `t2ed`, `t3ed` | tech effect data | 1, 1, 1 |
| `buym` | buy menu | 1 |

Shadowrun extended some enums that this build reads:

- **Actor types.** Halo has 16. Shadowrun adds hologram, dog, ninja, mook,
  guard, necro, dwarf (its executable's names, 16 to 22) and a 23rd that its
  executable does not name. Before the tags are checked, each actor
  definition of one of them gets this build's nearest type by what its
  actors do (`shadowrun_cache_convert`): the spirit dog charges with its
  teeth, as the flood's combat forms do (combat form); the ninja dodges and
  closes in, as an elite does (elite); the rest are soldiers with guns
  (marine). Missionlet has six such definitions: its troll and guard
  (guard), commander and mage (the 23rd), ninja and hologram.
- **Values with no near equivalent.** These are corrected by the tag
  validator, as any value past an enum's end is:

| Tag | Field | Shadowrun's value | Becomes |
| --- | --- | --- | --- |
| `globals\globals` | grenades | 5 grenade types | the first 2 |
| `shadowrunstuff\antimagic grenade\antimagic grenade` | grenade type | 2 | 0 (fragmentation) |
| `shadowrunstuff\tree of life grenade\tree of life grenade` | grenade type | 2 | 0 (fragmentation) |
| `powerups\magic bamf` | powerup type | 7 | 0 |
| missionlet's scenario | encounter 5's squad's initial and default state | 12 | none |

### Scripts

A compiled script calls functions and reads engine globals by their index in
the engine's tables. The prototype's tables are not this build's (430 functions
and 443 engine globals, read from its executable, `cachesym.xbe`, numbered
otherwise than this build's). Compiled
scripts keep every name in their string data, though, so the scripts of a
map whose tags are linked elsewhere are given this build's indices by name.
This is the conversion Custom Edition maps' scripts get
(`custom_edition_scripts.c`, `scenario_scripts_convert_by_name`). Both
engines number their value types alike (the prototype's 54 type names are
this build's, in the same order).

What this build does not have does nothing: a call becomes a constant of its
type. Of the prototype's functions, these are missing here:

| Function | What it did |
| --- | --- |
| `player_change_tech` | give the players tech |
| `ai_drop_equipment_when_dead` | an actor drops a spell's powerup |
| `drop_grenade` | a grenade dropped at a cutscene flag |
| `cheats_loadout`, `player_action_test_loadout` | the loadout menu |
| `game_difficulty_set_ratio`, `game_difficulty_get_ratio` | dynamic difficulty |
| `mp_between_rounds` | multiplayer rounds |
| `player_effect_set_allow_rumble` | rumble |

So are the engine globals `dynamic_difficulty_enabled`,
`rasterizer_DXTC_noise` and `rasterizer_splitscreen_VB_optimization`.
This build has `player_set_magic`, `player_change_magic` and
`game_set_assignment_scheme`, as the prototype's (see [Magic](#magic)); on
other maps they do nothing. Missionlet's scripts make 110 calls, all of
them this build's now. The `ui.map`'s make 36, all of them this build's.

Maps whose tags are linked to the tag cache's base (every Xbox map this
build ran before) are loaded exactly as they were.

## Magic

The prototype's magic is ported from its executable
(`port/linux/game/shadowrun_*.c`), and plays on its maps only: a map is
Shadowrun's when its tags have Shadowrun's groups. A unit whose biped has an
`srud` (Shadowrun unit definition) gets its race (`srac`'s by the srud's
races), its head, a pool of mana and the spells of its race's magic tags
(`m1ed`, `m2ed`, `m3ed`):

| Spell | What it does |
| --- | --- |
| Teleport | the caster vanishes and reappears a way off, the way it moves |
| Gust | a blast that throws back what is before the caster |
| Tree of Life | a thrown seed grows a tree that heals those near it, and withers |
| Resurrection | raises the dead near the caster as undead on its side |
| Strangle | thorns grow from the surfaces where the caster aims, and hold |
| Summon Elemental | an elemental (an encounter of the map) fights for the caster for a time |
| Spirit Animal | the caster grows into its animal form while its mana lasts |
| Smoke | the caster fades into smoke, unseen and unharmed, while its mana lasts |

A spell costs mana and holds back some of the pool while it lasts; mana
comes back over time. A projectile flagged for it grows a tree of life where
it lands (the Tree of Life grenade), or a firewall (the fireball's): fires
that burn those of other sides. Magic shows at the bottom of the HUD: the
spell, the mana, a cell a point (what spells hold back in dark red).

Its controls are simpler than the prototype's, whose radial menu assigned
spells to slots on the triggers and buttons: on these maps the right bumper
(the switch-grenade control) casts the current spell, and the d-pad's right
goes through the known spells and then the grenades. (The d-pad's up and
down walk the buy menu, so the cycle has its right, and its left, which the
buy menu's pages use.) The left trigger throws grenades as ever. The
scripts' `player_set_magic` and `player_change_magic` give the players
spells; `game_set_assignment_scheme` is kept but only the one scheme plays.

## Races

A unit's race (a player's chosen race, an actor's from its variant) sets
how it takes damage: its own scale of damage and of melee, and a player's
melee is dealt at its race's scale. A player moves at its race's speed with
the weapon in its hand (the weapon's `srwd`, else its `srud`'s): a human is
a little slower with an assault rifle, a troll slower with all of them.

The troll rages. Hurt enough over half a second, it rages more with each
hit, up to all the way; half a second after the last hit its rage goes down
again, slowly. As it rages it takes less damage (a fifth of it, raging
fully) and is healed less, moves and turns more slowly, grows larger, and
its colours go pale; past a little rage, in the campaign, what hits it
strikes as on a jackal's shield. A player troll pays mana for its rage (an
actor's spells and rage cost nothing). A race may also keep off with its
rage only the damage from before it; the troll's keeps off all of it.

A human's body heals itself: three seconds after it was last hurt it heals
a little each second, as its race's regeneration has it. (The elves' race
has regeneration too, but its threshold keeps it from ever healing.)

## Camera

As in the prototype, a player holding a melee weapon (the katana, the
razorclaws: weapons with a flag of Shadowrun's) is seen from behind, on its
unit's third camera track, closer than the first; so is a player in its
smoke, unless it looks through a scope. Aiming is as in first person: only
the view changes. Units keep their third camera track (this build's limit,
the tool's, was two; the game reads only the first otherwise).

## Buy menu

The prototype's buy menu is the multiplayer one, ported from its
executable: the Back control opens it on these maps (the prototype opened it
only at its own multiplayer rounds, and in a campaign had a list of
loadouts instead, which nothing opened). Each player starts with the
executable's $5,000,000. Its pages sell weapons (the one in hand is dropped
for a new one, with both hands full), grenades (up to two), spells and a
gift of $250 to a teammate, at the prices of the player's `srud`. The stick
or the d-pad moves through it, the jump control buys, the melee control goes
back a page or closes it, and the action control buys a magazine for the
weapon in hand; while it is open the player does nothing else. Tech and the
anti-magic grenade are listed but cannot be bought. A co-op client's
purchases are its own: they are not sent to the host.

## What does not work

This build runs the prototype's maps on Halo's game code; of Shadowrun's own
systems these are not there:

- tech (the `t?ed` tags), anti-magic and essence;
- Shadowrun's weapons as Shadowrun defined them (`srwd`): but for each
  race's speed with them, they play as Halo's do from their Halo tags;
- Shadowrun's AI behaviours for its new actor types (they play as the types
  above), its extra AI state, and the actors' spells (the elf mage's and the
  ninja's teleport);
- dynamic difficulty (the prototype's `dyndiff.log`), and the magic
  grenades and powerups, which become Halo's;
- network games: not tried. The prototype's maps examined are single player
  (missionlet) and the menus (`ui.map`).

## Evidence

Measured on `missionlet.map` (compressed, as shipped, and decompressed) and
the prototype's `ui.map`:

- `tools/map_validate` (built by `ninja linux`) loads each as the game
  loads it. Their tags are linked to `8069e000`; missionlet has 7
  corrections and the `ui.map` 3, as listed above. Missionlet's BSP loads to
  `8169d800` and the `ui.map`'s to `80833800`.
- `map_validate --fuzz` on them: 1000 runs of the `ui.map`, 3000 of the
  decompressed missionlet and 500 of the compressed one, with no crash or
  hang, and every map let through clean when checked again.
- The debug build on Linux, null renderer, `map_name
  levels\test\hangemhigh\missionlet` from `init.txt`, queried through the
  telnet console (`debug.telnet_console`):
  - The level loads and runs with no assertion: game time advances and the
    player spawns at full health.
  - Each of the 25 encounters, placed with `ai_place`, has as many actors as
    its squads ask for (CivsInYard 9, FightBackwards 8, Trolls 2, the spirit
    1, and so on). They live, and fight: the civilians fall to the soldiers
    placed beside them.
  - The player teleported into `SpawnHallCombats` (`object_teleport` to the
    flag `HP1`, inside it): the startup script waiting on that volume places
    `hall1tohall2`, `hall2tohall3`, `hall2` and `hall3` (1, 1, 1 and 2 actors).
- The main menu loads from the prototype's `ui.map`.
- With `missionlet.map` in `maps` (and a copy of it with its header edited to
  pass for `a10`), the map lists' scan finds missionlet alone, as CUSTOM
  SINGLEPLAYER's entry `shadowrun\missionlet`, which the menus take for a
  campaign map (and New Game starts as one); `map_name shadowrun\missionlet`
  loads and plays it.
- `tools/test_shadowrun_caches.py` tests the format functions on tag data it
  builds, and the real maps with `HALO_SHADOWRUN_MAPS` set to a folder of
  them.
