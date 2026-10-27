# AI Spawning for Firefight - Technical Analysis

## Requested Feature
Spawn AI enemies on blue team CTF spawn points by loading character tags from c40 (The Maw campaign level).

## Why Loading from c40 is Not Feasible

### 1. Cache File Architecture Limitation

Halo's engine is designed to have **one active cache file** at a time:

```c
// cache_files.c - Single global tag cache
static byte *tag_cache_base_address = (byte *)0x40440000;  // 23 MB on Xbox
static struct cache_file_globals cache_file_globals;       // One active cache
```

**Problems:**
- Tag indices are **absolute pointers** into the single tag cache
- Loading c40's tags would **overwrite** the current map's tags
- Tag references between maps would be invalid (different tag indices)
- The engine has no concept of "loading tags from another map while keeping current map"

### 2. Tag Reference Invalidation

```c
struct tag_reference {
    unsigned long group_tag;  // 'bipd', 'actv', etc.
    long index;               // Index into CURRENT map's tag array
    char *name;               // Pointer into CURRENT map's string table
};
```

If you load c40's elite biped:
- Its tag index would conflict with current map's indices
- All its references (model, animations, physics) point to c40's tag cache
- You'd need to **rebind every reference** to the current map's cache

### 3. Missing Tag Dependencies

An elite from c40 needs:
- Biped definition (`bipd`)
- Model (`mode`)
- Physics model (`phmo`)
- Actor variant (`actv`)
- Character definition (`char`)
- Animations (`antr`)
- Damage effects (`jpt!`)
- Sounds (`snd!`)
- Bitmaps (`bitm`)
- Shaders (`soso`, `sgla`, etc.)

**All of these must come from c40** and be remapped to the CTF map.

### 4. AI System Requires Scenario Encounters

The AI system is **encounter-driven**:

```c
struct encounter_datum {
    struct encounter_definition *definition;  // From scenario tags
    short squad_index;
    short platoon_index;
    // ... firing positions, patrol routes, all from scenario
};

void ai_place(void) {
    // Spawns actors from SCENARIO-DEFINED encounters
    // Not designed for dynamic spawning
}
```

**Problems:**
- No API for "spawn an actor without an encounter"
- AI behavior, firing positions, movement all come from encounter definitions
- Encounters are baked into the scenario at map compile time

### 5. Network Synchronization Nightmare

Dynamically spawned AI would need:
- Every client to have c40's tags loaded (impossible)
- Network protocol changes to sync dynamic AI spawn
- Object indices, AI states, all synchronized
- Massive network bandwidth for AI updates

## What Actually Works: The Practical Approach

### Use Scenario-Defined Encounters

The **correct** way to add AI to Firefight:

1. **Map authors add encounters to their maps** at compile time
2. **Firefight triggers those encounters** when waves start
3. **Use existing AI system** - no tag loading needed

```c
// In firefight.c
void firefight_spawn_wave(short wave_number) {
    char encounter_name[32];
    long encounter_index;
    
    // Map has encounters named "firefight_wave_1", "firefight_wave_2", etc.
    sprintf(encounter_name, "firefight_wave_%d", (int)wave_number);
    encounter_index = scenario_get_encounter_by_name(
        global_scenario_get(), encounter_name);
    
    if (encounter_index != NONE) {
        // Activate the encounter - AI spawns at their defined points
        ai_scripting_encounter_activate(encounter_index);
    }
}
```

### How It Works

**Map Setup (Sapien/Tool):**
```
blood_gulch.scenario has:
  - Encounter "firefight_wave_1" at blue base
    - Squad "grunts" (8 grunts, spawn at blue CTF points)
  - Encounter "firefight_wave_2" at blue base  
    - Squad "elites" (4 elites, spawn at blue CTF points)
  - Encounter "firefight_wave_3" at blue base
  - Squad "mixed" (2 elites + 6 grunts)
```

**Firefight triggers them:**
```c
// Wave 1: activate encounter "firefight_wave_1"
// Wave 2: activate encounter "firefight_wave_2"  
// Wave 3: activate encounter "firefight_wave_3"
```

### Advantages

✓ **Uses existing AI system** - no tag loading needed
✓ **AI behavior works** - firing positions, patrols, all defined in scenario
✓ **Network sync works** - encounters already network-synchronized
✓ **Map compatibility** - maps without encounters just don't spawn AI
✓ **Map authors control difficulty** - they place enemies and set variants

### Disadvantages

✗ **Requires map support** - vanilla maps don't have Firefight encounters
✗ **Not dynamic** - can't scale difficulty beyond what map defines
✗ **Map-specific** - each map needs Firefight encounters added

## Alternative: Use Existing Map Enemies

For maps that **already have AI** (campaign maps, custom maps with AI):

```c
void firefight_use_map_enemies(void) {
    struct actor_iterator iterator;
    struct actor_datum *actor;
    
    // Find all dormant AI on the map
    actor_iterator_new(&iterator, FALSE);
    while ((actor = actor_iterator_next(&iterator)) != NULL) {
        long actor_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->header.index);
        
        // Activate dormant enemies for Firefight
        if (!actor->active && actor->encounter_index != NONE) {
            encounter_activate(actor->encounter_index);
        }
    }
}
```

## Conclusion

**Loading tags from c40 is architecturally impossible** without rewriting the cache file system, tag reference system, and AI system. The engine is not designed for it.

**The practical solution:**
1. Maps designed for Firefight include pre-placed AI encounters
2. Firefight activates those encounters in waves
3. Uses 100% existing engine functionality
4. Works with network play
5. Map authors control the experience

## Implementation Recommendation

I recommend implementing encounter-based wave spawning:

1. **Define encounter naming convention** - "firefight_wave_N"
2. **Firefight scans for encounters** at map load
3. **Waves activate encounters** in sequence
4. **Graceful degradation** - maps without encounters = no AI (still playable defense mode)

This gives you:
- Working AI enemies
- Network-synchronized spawning
- Map compatibility (works on any map, AI is optional)
- Extensible (map authors can add better Firefight experiences)

Would you like me to implement encounter-based wave spawning instead?
