# Firefight Gametype

Firefight is a cooperative wave-based survival mode for OpenCE that uses CTF spawn points.

## Design Overview

Firefight is built on the CTF game engine specifically to leverage CTF spawn points, making it compatible with any map that has CTF spawns. Players spawn at red team CTF locations and work together to survive increasingly difficult waves of enemies.

### Key Design Decisions

1. **CTF Engine Base**: Uses `game_engine_ctf` instead of Slayer to access CTF spawn points
2. **Team-Based**: Players are on red team, blue team is reserved for future AI spawning
3. **Cooperative Focus**: Friendly fire off by default, shared objectives
4. **Survival Mode**: Time-limited rounds with no score limit

## Gametype Specifications

### Core Settings

- **Game Engine**: CTF (`game_engine_ctf`)
- **Mode**: `_game_mode_firefight`
- **Teams**: Enabled (players on red team)
- **Time Limit**: 15 minutes (configurable)
- **Score Limit**: None (survival-based)

### Spawn Behavior

- Players spawn at the red team's CTF spawn points
- Enemies come from the map's `firefight_wave_N` encounters, whose squads
  start at the blue team's CTF spawn points (see `firefight_ai_spawning.md`)
- Respawn time: 5 seconds
- No suicide penalty

### Team Lives

The `firefight_team_lives` option is a pool of lives the whole team shares,
set in Edit Gametypes' OPENCE OPTIONS (TEAM LIVES: UNLIMITED by default, else
5, 10, 15, 20, 30, 50 or 100):

- Each player death (a betrayal does not count) spends one life
- While the pool is not empty, the dead respawn as usual
- A player whose team has no lives left does not respawn, and `firefight.c`
  records the round as lost to the host's log (`firefight_lives_exhausted`)
- 0 (UNLIMITED) keeps the stock behaviour: everyone respawns without end

### Equipment & Loadout

- **Primary Weapon**: Assault Rifle
- **Secondary Weapon**: Pistol
- **Grenades**: 4 Frag, 4 Plasma
- **Map Weapons**: Enabled (players can find weapons on map)
- **Vehicles**: Default vehicle set (map-dependent)

### Combat Rules

- **Friendly Fire**: Off
- **Motion Sensor**: Enabled (cooperative advantage)
- **Shields**: Enabled
- **Auto Team Balance**: Disabled

### CTF-Specific Modifications

The gametype disables all CTF objectives:
- Assault: Disabled
- Flag reset on capture: Disabled
- Flag must reset: Disabled
- Flag at home to score: Disabled
- Single flag time: 0

This ensures CTF spawn points are used without CTF gameplay mechanics interfering.

## Implementation Details

### Files

- `port/linux/game/firefight.h` - Public API
- `port/linux/game/firefight.c` - Implementation
- `port/linux/game/game_modes.c` - Integration with game modes system
- `source/game/game_engine.h` - Enum definition for `_game_mode_firefight`

### Key Functions

- `firefight_enabled()` - Check if Firefight is active
- `firefight_engine()` - Override CTF engine behavior
- `firefight_update()` - Spawn the map's wave encounters and track them
- `firefight_postspawn()` - Post-spawn equipment setup
- `firefight_player_killed()` - Spend a shared team life
- `firefight_can_respawn()` - Stop respawns once the lives are gone
- `firefight_gametype()` - Built-in gametype definition

### Network Compatibility

Firefight increments `HALO_PORT_NETWORK_VERSION` as it modifies:
- Game mode enum (new mode added)
- Game variant options structure usage
- Spawn point selection logic

Machines with different network versions cannot play together.

## Future Enhancements

The current implementation provides the foundation for wave-based gameplay. Future additions could include:

1. **AI Director**: Host-side wave spawning at blue team CTF points
2. **Wave Progression**: Increasing difficulty, enemy counts, enemy types
3. **Scoring System**: Points for kills, bonuses for wave completion
4. **Custom Options**: 
   - Initial wave difficulty
   - Enemies per wave
   - Wave duration
   - Lives per player

5. **Network State**: Synchronized wave number and enemy counts across clients
6. **Round End Conditions**: 
   - All players eliminated
   - Wave target reached
   - Time expired

## Map Compatibility

Firefight works on any map with CTF spawn points:
- Blood Gulch
- Battle Creek
- Sidewinder
- Infinity
- All Custom Edition maps with CTF spawns

The gametype gracefully handles maps without CTF spawns by falling back to standard team spawns.

## Usage

### Command Line

```bash
# Start Firefight with debug.network_test
HALO_NETWORK_TEST=host:bloodgulch halo --gametype firefight
```

### In Game

Select "Firefight" from the OPENCE gametype bank in the Select Gametype menu.

### Dedicated Server

```json
{
  "gametype": "firefight",
  "map": "bloodgulch",
  "time_limit": 15
}
```

## Technical Notes

- Firefight is the first OpenCE mode to use a non-Slayer engine
- The `game_modes_variant_mode()` function was extended to support CTF-based modes
- CTF spawn point selection respects team assignment (red team only for players)
- The mode appears in server browser with "FIREFIGHT" label
- Compatible with Hardcore modifier for increased difficulty

## Testing

Run the game mode tests with:
```bash
python -m pytest -q tools/harness/tests/game_modes.c -k firefight
```

Test on CTF maps:
```bash
python tools/net_compare.py --gametype firefight --map bloodgulch
```
