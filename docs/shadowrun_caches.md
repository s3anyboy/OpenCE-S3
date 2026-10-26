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
into their trigger volumes. Its scripts call Shadowrun's magic functions,
which do nothing here (see [Scripts](#scripts)).

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
| `characters\sr_player\sr_player` | unit camera tracks | 3 | the first 2 |
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
| `player_set_magic`, `player_change_magic`, `player_change_tech` | give the players magic or tech |
| `game_set_assignment_scheme` | how magic and tech are assigned |
| `ai_drop_equipment_when_dead` | an actor drops a spell's powerup |
| `drop_grenade` | a grenade dropped at a cutscene flag |
| `cheats_loadout`, `player_action_test_loadout` | the loadout menu |
| `game_difficulty_set_ratio`, `game_difficulty_get_ratio` | dynamic difficulty |
| `mp_between_rounds` | multiplayer rounds |
| `player_effect_set_allow_rumble` | rumble |

So are the engine globals `dynamic_difficulty_enabled`,
`rasterizer_DXTC_noise` and `rasterizer_splitscreen_VB_optimization`.
Missionlet's scripts make 110 calls; `player_set_magic` and
`player_change_magic` (three times each) are the ones made inert. The
`ui.map`'s make 36, all of them this build's.

Maps whose tags are linked to the tag cache's base (every Xbox map this
build ran before) are loaded exactly as they were.

## What does not work

This build runs the prototype's maps on Halo's game code, so Shadowrun's own
systems are not there:

- magic and tech (the spells, their effects and the `m?ed`/`t?ed` tags),
  essence, the buy menu, and resurrection;
- Shadowrun's weapons and units as Shadowrun defined them (`srwd`, `srud`):
  the weapons and bipeds play as Halo's do from their Halo tags;
- Shadowrun's AI behaviours for its new actor types (they play as the types
  above), and its extra AI state;
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
