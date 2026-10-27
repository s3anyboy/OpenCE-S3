# Firefight Gametype Design Summary

## Overview

This design implements a **Firefight** gametype for OpenCE - a cooperative wave-based survival mode that uses **CTF spawn points** instead of standard Slayer spawns.

## Key Design Features

### 1. CTF Engine Foundation
- **Game Engine**: `game_engine_ctf` (not Slayer)
- **Rationale**: CTF maps have well-positioned, team-specific spawn points that are ideal for cooperative defense scenarios
- **Spawn Strategy**: Players spawn at red team CTF locations, creating natural defensive positions

### 2. Cooperative Gameplay
- **Team Configuration**: All players on red team
- **Blue Team**: Reserved for future AI enemy spawning (not yet implemented)
- **Friendly Fire**: Disabled by default
- **Shared Objective**: Survival and wave completion

### 3. CTF Without Flags
- The gametype **disables all CTF objectives** (flags, scoring, capture mechanics)
- CTF engine is used **only for spawn point access**
- This is the cleanest way to leverage CTF spawns without implementing custom spawn logic

## Implementation Architecture

### Core Components

```
firefight.h          - Public API and function declarations
firefight.c          - Implementation with CTF engine override
game_modes.c         - Integration into OpenCE's gametype system
game_engine.h        - _game_mode_firefight enum definition
```

### Integration Points

1. **Game Mode Enum**: Added `_game_mode_firefight` to `source/game/game_engine.h`
2. **Gametype Registry**: Added to `game_modes_gametypes[]` array with name "firefight"
3. **Engine Override**: `firefight_engine()` called from `game_modes_engine()`
4. **Post-Spawn Hook**: `firefight_postspawn()` called from `game_modes_postspawn()`
5. **Mode Validation**: Extended `game_modes_variant_mode()` to support CTF-based modes

### Spawn Point Logic

```
CTF Map:
  - Red flag spawn → Red team base spawn points
  - Blue flag spawn → Blue team base spawn points (reserved for AI)
  
Firefight:
  - All players → Red team → Spawn at red CTF points
  - Enemies (future) → Blue team → Spawn at blue CTF points
  - Creates natural attack/defend scenario
```

## Default Settings

| Setting | Value | Rationale |
|---------|-------|-----------|
| Time Limit | 15 minutes | Standard round duration |
| Respawn Time | 5 seconds | Quick respawn for cooperative play |
| Primary Weapon | Assault Rifle | Versatile starting weapon |
| Secondary Weapon | Pistol | Backup precision weapon |
| Grenades | 4 Frag + 4 Plasma | Generous starting ammo |
| Map Weapons | Enabled | Players can find better weapons |
| Vehicles | Enabled | Map-dependent vehicle support |
| Motion Sensor | Enabled | Cooperative advantage |
| Friendly Fire | Off | Prevent team damage accidents |

## Network Compatibility

This change **increments HALO_PORT_NETWORK_VERSION** because it:
- Adds a new game mode enum value
- Modifies spawn point selection logic
- Changes game variant validation rules

Machines with different versions cannot play together.

## Map Compatibility

✅ **Works on any map with CTF spawn points:**
- Blood Gulch
- Battle Creek
- Sidewinder
- Infinity
- Danger Canyon
- Ice Fields
- All Custom Edition CTF maps

❌ **Gracefully degrades on non-CTF maps:**
- Falls back to standard team spawns
- Still playable but loses positional advantage

## Future Expansion Path

The current implementation provides a **foundation** for wave-based gameplay. Future enhancements can add:

### Phase 2: AI Director (Future)
- Host spawns AI enemies at blue CTF points
- Wave progression with increasing difficulty
- Enemy variety (grunts, elites, hunters)
- Wave completion bonuses

### Phase 3: Advanced Features (Future)
- Custom wave configurations
- Special enemy types
- Power weapon drops
- Boss waves
- Scoring and leaderboards

## Code Style Compliance

The implementation follows OpenCE's rules:

✅ **Follows project conventions**:
- C89 style (declarations before statements)
- MSVC ABI compatibility
- 80-column comment wrapping
- Port functions marked with `/* port: */` comments

✅ **Integration pattern matches existing modes**:
- Same structure as `infection.c` and `gun_game.c`
- Uses distributed netcode patterns (host authority)
- Proper `_enabled()`, `_engine()`, `_postspawn()` hooks

✅ **Documentation**:
- Header comments explain the design
- Inline comments explain non-obvious logic
- Separate design document (`docs/firefight.md`)

## Testing Strategy

### Unit Tests
```bash
# Test mode validation
python -m pytest -q tools/harness/tests/game_modes.c

# Test gametype creation
python -m pytest -q tools/test_firefight.py
```

### Integration Tests
```bash
# Local network test
HALO_NETWORK_TEST=host:bloodgulch halo --gametype firefight

# Server test
python -m pytest -q tools/test_dedicated_server.py --gametype firefight
```

### Map Coverage
```bash
# Test on all CTF maps
for map in bloodgulch sidewinder dangercanyon; do
  python tools/net_compare.py --gametype firefight --map $map
done
```

## Usage Examples

### Command Line
```bash
# Host a game
halo --server --gametype firefight --map bloodgulch --time-limit 15

# Join with network test
HALO_NETWORK_TEST=join halo
```

### Server Configuration
```json
{
  "mode": "firefight",
  "map": "bloodgulch", 
  "time_limit": 15,
  "max_players": 8
}
```

### Menu Selection
1. Multiplayer Setup
2. Edit Gametypes
3. Select Gametype
4. **OPENCE** bank
5. **Firefight**

## Technical Highlights

### Why CTF Engine?

The CTF engine was chosen over Slayer because:

1. **Spawn Points**: CTF has dedicated team spawn points at bases
2. **Positional Design**: CTF maps have natural attack/defend layouts
3. **Team Assignment**: CTF naturally separates teams (players vs AI)
4. **No Custom Logic**: Leverages existing engine behavior

### Why Not Slayer?

Slayer spawns are:
- Scattered across the map
- Not team-specific
- Designed for deathmatch flow
- Would require custom spawn logic to create defensive positions

### Engine Override Pattern

```c
struct game_engine *firefight_engine(struct game_engine *engine)
{
    if (engine->game_engine_index != game_engine_ctf)
        return engine;
    
    // Override CTF behaviors for Firefight
    firefight.ctf = *engine;
    firefight.engine = firefight.ctf;
    firefight.engine.initialize = firefight_initialize;
    
    return &firefight.engine;
}
```

This pattern:
- Preserves CTF spawn logic
- Disables CTF objectives
- Adds Firefight-specific behavior

## Summary

**Firefight** is a cooperative survival gametype that cleverly reuses the CTF engine to access well-positioned spawn points, creating natural defensive scenarios without requiring custom spawn logic. The implementation is clean, follows OpenCE conventions, and provides a solid foundation for future wave-based gameplay features.

The design demonstrates that new gametypes can leverage existing engines in creative ways - CTF isn't just for flags, it's a team-based positional framework that works perfectly for cooperative defense.
