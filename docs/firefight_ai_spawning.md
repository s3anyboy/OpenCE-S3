# Firefight AI Spawning - Implementation Guide

## Overview

Firefight's AI comes from the map's own encounters, placed by `encounter_create`
at the blue team's spawn points. This document explains how to set up maps for
Firefight and how the system works.

## How It Works

### For Players

1. **Load a Firefight-enabled map** (one with "firefight_wave_N" encounters)
2. **Start Firefight gametype**
3. **Players spawn at red CTF points**
4. **First wave spawns after 5 seconds**
5. **Defend against waves of AI enemies**
6. **Each wave cleared triggers the next after 30 seconds**
7. **Survive all waves or until time runs out**
8. **Team lives (if set): each death spends one; the team respawns until none**

### For Map Authors

#### 1. Map Setup in Sapien

Create encounters at **blue team CTF spawn points** with specific names:

```
Encounter Name: firefight_wave_1
  Location: Blue base
  Squads:
    - "grunts" (8 grunts)
      Starting locations: Blue CTF spawn points
      
Encounter Name: firefight_wave_2
  Location: Blue base
  Squads:
    - "elites" (4 elites)
      Starting locations: Blue CTF spawn points
      
Encounter Name: firefight_wave_3
  Location: Blue base
  Squads:
    - "mixed" (2 elites + 6 grunts)
      Starting locations: Blue CTF spawn points
```

#### 2. Naming Convention

**Required pattern:** `firefight_wave_N` where N is 1-10

Examples:
- `firefight_wave_1` - First wave (easiest)
- `firefight_wave_2` - Second wave
- `firefight_wave_3` - Third wave
- ... up to ...
- `firefight_wave_10` - Tenth wave (hardest)

#### 3. Encounter Configuration

For each encounter:

**Location:**
- Place at or near **blue team CTF spawn points**
- Use blue base or blue side of map
- AI will spawn at starting locations within the encounter

**Squads:**
- Define AI squads (grunts, elites, jackals, hunters, etc.)
- Set starting locations at blue CTF spawn points
- Configure squad size, weapons, grenades

**Firing Positions:**
- Optional: define firing positions for AI combat
- Recommended: add firing positions for better AI behavior

**Example Sapien Setup:**
```
[encounters]
  [encounter "firefight_wave_1"]
    name: firefight_wave_1
    team_index: 1 (covenant)
    [squads]
      [squad "grunts"]
        actor_type: grunt
        initial_state: alert
        starting_locations: 8 points at blue CTF spawns
```

## Technical Implementation

### System Architecture

```
Game Start
    ↓
firefight_initialize()
    ↓
firefight_scan_encounters()  // Finds "firefight_wave_1", etc.
    ↓
Schedule first wave (5 seconds)
    ↓
Game Loop
    ↓
firefight_update() (every tick, host only)
    ↓
Check if it's time to spawn next wave
    ↓
    YES → firefight_spawn_wave()
           ↓
           ai_place()  // Activates the encounter
           ↓
           Count enemies spawned
           ↓
           Monitor wave until cleared
           ↓
           Schedule next wave (30 seconds)
    ↓
    NO → Update enemy count
          ↓
          Check if wave is cleared
```

### Code Flow

#### 1. Initialization

```c
static void firefight_scan_encounters(void)
{
    // Scans the scenario for "firefight_wave_1" through "firefight_wave_10"
    // (encounter_get_by_name), storing each index in wave_encounters[]
    // Sets wave_count to the number of waves found
}
```

#### 2. Wave Spawning

```c
static void firefight_spawn_wave(short wave_number)
{
    // encounter_create(encounter_index, NONE, NONE) places the squads'
    // actors at their starting locations (the blue team's CTF spawns),
    // then the wave's enemies are counted
}
```

#### 3. Wave Tracking

```c
void firefight_update(void)
{
    // Host only
    // Checks if it's time to spawn next wave
    // Counts living enemies in current wave
    // Detects wave clear condition
    // Schedules next wave
}
```

#### 4. Enemy Counting

```c
static short firefight_count_wave_enemies(long encounter_index)
{
    // Iterates through the encounter's actors (encounter_actor_iterator_new)
    // Gets each actor's unit (actor->meta.unit_index)
    // Counts the units that are alive (_object_dead_bit clear)
}
```

### Timing

| Event | Delay |
|-------|-------|
| First wave after game start | 5 seconds |
| Between waves (after clear) | 30 seconds |
| Enemy count update | Every tick |

### Constants

```c
FIREFIGHT_WAVE_START_DELAY = 5      // Seconds before first wave
FIREFIGHT_WAVE_INTERVAL = 30        // Seconds between waves
FIREFIGHT_MAXIMUM_WAVES = 10        // Max waves scanned
```

## Map Compatibility

### Maps With Firefight Encounters

✅ **Full Firefight experience:**
- AI waves spawn
- Progressive difficulty
- All Firefight features

**Setup required:**
- Add encounters named "firefight_wave_N"
- Place AI at blue CTF spawn points
- Test wave progression

### Maps Without Firefight Encounters

✅ **Defense/Survival mode:**
- Players spawn at red CTF points
- No AI enemies
- Pure player vs. player or training mode
- Still playable and fun

**No setup needed:**
- Works on any CTF map
- Graceful degradation
- No errors or crashes

### Vanilla Halo Maps

Maps like Blood Gulch, Sidewinder, etc.:
- Work in Firefight mode
- Players spawn at red base
- No AI (would need encounters added)
- Can be modded to add Firefight encounters

## Network Synchronization

### Host Authority

The **host** is responsible for:
- Scanning for wave encounters
- Spawning waves (calling `ai_place()`)
- Tracking enemy counts
- Timing between waves

### Clients

**Clients** receive:
- AI spawn events (via existing network sync)
- AI positions and states (via existing object sync)
- Encounter activation (via existing AI sync)

**No additional network protocol needed** - Firefight uses 100% existing network code.

## Example: Creating a Firefight Map

### Step 1: Base Map

Start with a CTF map (e.g., Blood Gulch):
- Red base: player spawn points
- Blue base: AI spawn points
- Open space between for combat

### Step 2: Add Encounters

In Sapien, add encounters:

**Wave 1 - Easy:**
```
Name: firefight_wave_1
Squads: 8 grunts (minor)
Starting Locations: Blue CTF spawn points
```

**Wave 2 - Medium:**
```
Name: firefight_wave_2
Squads: 4 grunts (minor) + 2 elites (minor)
Starting Locations: Blue CTF spawn points
```

**Wave 3 - Hard:**
```
Name: firefight_wave_3
Squads: 2 elites (major) + 4 grunts (major)
Starting Locations: Blue CTF spawn points
```

**Wave 4 - Boss:**
```
Name: firefight_wave_4
Squads: 1 elite (gold) + 2 hunters
Starting Locations: Blue CTF spawn points
```

### Step 3: Configure Squads

For each squad:
- Set actor type (grunt, elite, jackal, hunter)
- Set rank (minor, major, ultra, gold)
- Configure weapons (needlers, plasma rifles, etc.)
- Set grenades (plasma grenades)
- Define behavior (patrol, defend, attack)

### Step 4: Add Firing Positions

Add firing positions near blue base:
- Cover positions
- Elevated positions
- Multiple angles of attack

### Step 5: Test

1. Compile map with Tool
2. Load in Halo
3. Start Firefight gametype
4. Check wave spawning
5. Verify AI behavior
6. Adjust difficulty as needed

## Debugging

### Console Logs

Firefight logs to console (host only):

```
firefight: found encounter 'firefight_wave_1' at index 5
firefight: found encounter 'firefight_wave_2' at index 6
firefight: map has 2 wave encounters

firefight: wave 1 spawned, 8 enemies
firefight: wave 1 cleared
firefight: next wave in 30 seconds

firefight: wave 2 spawned, 6 enemies
firefight: wave 2 cleared
firefight: all 2 waves spawned
```

### Common Issues

**Problem:** No AI spawns
**Solution:** 
- Check encounter names match "firefight_wave_N" exactly
- Verify encounters exist in scenario
- Check console logs for "map has X wave encounters"

**Problem:** AI spawns in wrong location
**Solution:**
- Verify squad starting locations are at blue CTF spawn points
- Check encounter zone includes spawn points

**Problem:** Wave doesn't clear
**Solution:**
- Check all AI are in the encounter (not separate encounters)
- Verify enemy count is accurate
- Look for stuck AI

**Problem:** Waves spawn too fast/slow
**Solution:**
- Adjust `FIREFIGHT_WAVE_START_DELAY` constant
- Adjust `FIREFIGHT_WAVE_INTERVAL` constant
- Recompile

## Advantages of This Approach

✅ **Uses existing AI system** - no new AI code needed
✅ **Network-synchronized** - works in multiplayer automatically
✅ **Map-controlled difficulty** - authors design the challenge
✅ **Graceful degradation** - works without AI (defense mode)
✅ **Modder-friendly** - easy to add Firefight to existing maps
✅ **Maintainable** - follows Halo's existing patterns
✅ **No tag loading** - no cache file complexity
✅ **Tested engine** - AI system is battle-tested

## Future Enhancements

Possible additions:
- [ ] Dynamic difficulty scaling (adjust enemy health/damage)
- [ ] Random wave selection (pool of waves, pick randomly)
- [ ] Bonus objectives (kill elite commander for points)
- [ ] Wave modifiers (shields, weapons, speed)
- [ ] Boss waves with special AI
- [ ] Survival mode (infinite waves, increasing difficulty)
- [ ] Wave status HUD (wave number, enemies remaining)
- [ ] End-of-round statistics

## Files Changed

| File | Changes |
|------|---------|
| `port/linux/game/firefight.c` | Added AI spawning, wave tracking, encounter scanning |
| `port/linux/game/firefight.h` | Updated documentation |
| `port/linux/game/game_modes.c` | Integrated `firefight_update()` call |

## Conclusion

Firefight's AI spawning uses **scenario-defined encounters** activated by the game mode. This approach is:
- **Practical** - uses existing, tested systems
- **Flexible** - map authors control the experience
- **Reliable** - network-synchronized automatically
- **Compatible** - works on any CTF map, AI is optional

Map authors create the Firefight experience. The game mode provides the framework.
