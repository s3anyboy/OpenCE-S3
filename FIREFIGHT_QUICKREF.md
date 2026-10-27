# Firefight Quick Reference

## What is it?
Cooperative wave-based survival mode using CTF spawn points.

## Key Concept
Uses CTF game engine for spawn points, disables CTF objectives.

## Files Modified/Created

### New Files
- `port/linux/game/firefight.h` - API declarations
- `port/linux/game/firefight.c` - Implementation
- `docs/firefight.md` - Full documentation
- `deploy/gametypes/firefight.json` - Server config example

### Modified Files
- `source/game/game_engine.h` - Added `_game_mode_firefight` enum
- `port/linux/game/game_modes.c` - Integrated Firefight into game modes system

## Code Additions

### Enum Value
```c
// source/game/game_engine.h
enum {
    _game_mode_none = 0,
    _game_mode_swat,
    _game_mode_infection,
    _game_mode_gun_game,
    _game_mode_firefight,  // NEW
    NUMBER_OF_GAME_MODES
};
```

### Gametype Entry
```c
// port/linux/game/game_modes.c
{ "firefight", L"Firefight", L"Firefight" },
```

### Engine Selection
```c
case _gametype_firefight:
    *variant = *build_game_variant_ctf(&temporary);
    break;
```

### Mode Configuration
```c
case _gametype_firefight:
    options->mode = _game_mode_firefight;
    variant->universal_variant.respawn_time = 5 * TICKS_PER_SECOND;
    options->time_limit = 15;
    options->friendly_fire = _friendly_fire_off;
    // ... more settings
    break;
```

## API Functions

```c
boolean firefight_enabled(void);
void firefight_dispose(void);
struct game_engine *firefight_engine(struct game_engine *engine);
void firefight_update(void);
short firefight_wave_number(void);
short firefight_enemies_remaining(void);
void firefight_postspawn(long player_index);
void firefight_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire);
void firefight_gametype(struct game_variant *variant, struct game_variant_options *options);
```

## Default Settings Summary

| Setting | Value |
|---------|-------|
| Engine | CTF |
| Teams | Yes (red only) |
| Time | 15 min |
| Respawn | 5 sec |
| Primary | Assault Rifle |
| Secondary | Pistol |
| Grenades | 4+4 |
| FF | Off |
| Radar | On |

## Spawn Behavior

- **Players**: Red team → Red CTF spawn points
- **AI (future)**: Blue team → Blue CTF spawn points
- **Fallback**: Standard team spawns if no CTF points

## Testing Commands

```bash
# Local test
HALO_NETWORK_TEST=host:bloodgulch halo --gametype firefight

# Server test
python -m pytest -q tools/test_dedicated_server.py --gametype firefight

# Network test
python tools/net_compare.py --gametype firefight --map bloodgulch
```

## Compatible Maps

Any map with CTF spawn points:
- Blood Gulch ✓
- Sidewinder ✓
- Battle Creek ✓
- Danger Canyon ✓
- Ice Fields ✓
- Custom Edition CTF maps ✓

## Future Work

- [ ] AI wave spawning at blue CTF points
- [ ] Wave difficulty progression
- [ ] Enemy variety (grunts, elites, hunters)
- [ ] Scoring system
- [ ] Custom wave configurations
- [ ] Boss waves
- [ ] Network state synchronization

## Network Version Impact

⚠️ This change increments `HALO_PORT_NETWORK_VERSION`:
- New game mode enum value
- Modified spawn logic
- Changed variant validation

## Build Integration

Firefight is compiled as part of the standard build:
- Linux: `ninja linux`
- Windows: `ninja windows`
- Android: `ninja android`
- Web: `ninja web`

No special build flags required.

## Code Style Notes

✓ C89 compatible (declarations before statements)
✓ MSVC ABI conventions
✓ Comments marked with `/* port: */`
✓ Follows existing mode patterns (infection, gun_game)
✓ Host authority for state management

## Important Constants

```c
#define FIREFIGHT_DEFAULT_TIME_LIMIT 15
#define FIREFIGHT_DEFAULT_RESPAWN_TIME 5
#define FIREFIGHT_DEFAULT_STARTING_GRENADES 4
```

## Integration Points

1. `game_modes_engine()` - Routes to `firefight_engine()`
2. `game_modes_postspawn()` - Calls `firefight_postspawn()`
3. `game_modes_variant_mode()` - Validates CTF+Firefight combo
4. `game_modes_kind_name()` - Returns "FIREFIGHT" label

## Why CTF Engine?

CTF provides:
- Team-specific spawn points at bases
- Natural attack/defend layout
- Positional gameplay framework
- No custom spawn logic needed

## Quick Debug

```c
// Check if Firefight is active
if (firefight_enabled()) {
    platform_log("Firefight mode active, wave %d", firefight_wave_number());
}

// Check game mode
if (game_modes_mode() == _game_mode_firefight) {
    platform_log("Firefight gametype detected");
}
```

## Common Issues

**Issue**: Players spawn scattered
**Fix**: Ensure map has CTF spawn points, check team assignment

**Issue**: CTF flags visible
**Fix**: Verify CTF overrides are applied (all disabled)

**Issue**: Network version mismatch
**Fix**: All clients must be on same build version

## Related Documentation

- `docs/firefight.md` - Full design document
- `port/linux/README.md` - Settings and controls
- `port/linux/NETCODE.md` - Network protocol
- `docs/custom_edition_caches.md` - Map compatibility
