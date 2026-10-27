# Spawning Enemies from Loose Tags - Technical Analysis

## Request

Spawn Firefight enemies from a loose folder `firefight/tags` containing tag files.

## Why This Doesn't Work

### The Fundamental Problem

Halo's engine architecture has **one global tag cache** that all game code references:

```c
// cache_files.c - Global tag cache
static byte *tag_cache_base_address = (byte *)0x40440000;  // Fixed memory address
static struct tag_file_index *tag_file_index;              // One tag index
static long tag_cache_size = 0x01700000;                   // 23 MB on Xbox
```

**Every tag reference in the game is an index into this single cache:**

```c
struct tag_reference {
    unsigned long group_tag;  // 'bipd', 'actv', 'weap', etc.
    long index;               // Index into THE tag array
    char *name;               // Pointer into THE string table
    long name_length;
};
```

### What Happens When You Load the Map

```
1. cache_files.c loads bloodgulch.map
2. Tag cache is populated at 0x40440000
3. Tag index built: 500 tags from index 0-499
4. All game systems get tag references (indices 0-499)
5. Game runs - everything references these indices
```

### What Would Happen if You Load firefight/tags

**Option A: Replace the cache**
```
1. Unload current map tags ← GAME CRASHES (players, weapons, BSP all gone)
2. Load firefight/tags ← New indices 0-N
3. Original map references (0-499) now point to wrong tags ← CORRUPTION
```

**Option B: Append to the cache**
```
1. Current map uses indices 0-499
2. Load firefight/tags at indices 500-550
3. Elite biped at index 500 references:
   - Model at index 501 ← OK
   - Animations at index 502 ← OK
   - Physics at index 503 ← OK
   BUT these indices assume firefight/tags started at 0
   The actual indices are 500-550, not 0-50 ← BROKEN REFERENCES
```

### The Tag Reference Web

An elite biped from `firefight/tags/characters/elite/elite.biped` needs:

```
elite.biped (index ???)
  ├─ model → elite.model (index ???)
  │   ├─ shaders → elite_armor.shader (index ???)
  │   │   └─ bitmap → elite_armor.bitmap (index ???)
  │   └─ nodes → elite_skeleton
  ├─ physics → elite.physics (index ???)
  ├─ collision → elite.collision_model (index ???)
  └─ unit → references actor variant

elite.actor_variant (index ???)
  ├─ character → elite.character (index ???)
  │   ├─ weapon → plasma_rifle.weapon (index ???)
  │   │   ├─ model → plasma_rifle.model (index ???)
  │   │   │   └─ shaders → plasma_rifle.shader (index ???)
  │   │   ├─ animations → plasma_rifle.animations (index ???)
  │   │   └─ effects → plasma_rifle_fire.effect (index ???)
  │   ├─ grenades → plasma_grenade.weapon (index ???)
  │   └─ sounds → elite_idle.sound (index ???), elite_alert.sound (index ???)
  └─ variant definitions
```

**Every arrow (→) is a tag reference (index) that must be valid.**

If you load these tags:
- Their **internal indices** are for a standalone firefight.map
- The **actual indices** in the combined cache are different
- You'd need to **rebind every reference** - thousands of them
- This is what Tool does when **compiling a map** - it's not runtime code

### Mods System Comparison

"But mods load loose tags!" - Not quite:

```c
// Mods DON'T load complete tags
// They load ASSETS (pixels, samples) that REPLACE map data

mod_bitmaps.c:
  - Reads .bitmap file metadata
  - Loads ONLY the pixel data
  - Replaces existing bitmap's pixels
  - Tag structure stays the same ← KEY POINT

mod_sounds.c:
  - Reads .sound file metadata
  - Loads ONLY the audio samples
  - Replaces existing sound's samples
  - Tag structure stays the same ← KEY POINT
```

**Mods work because:**
- They **replace data** of existing tags
- They **don't create new tags**
- They **don't change tag references**
- The tag index stays the same

### What You'd Need to Build

To load loose tags at runtime, you'd need:

1. **Tag Loader**
   - Parse .biped, .actv, .char, .weap, .mode, .bitm, .snd!, etc.
   - Allocate space in tag cache
   - Handle cache exhaustion (only 23 MB)

2. **Tag Reference Rebinder**
   - Walk every loaded tag's data
   - Find all tag references (indices)
   - Translate from file indices to runtime indices
   - Update thousands of references

3. **Dependency Resolver**
   - Load tags recursively (biped needs model needs shader needs bitmap)
   - Detect missing dependencies
   - Handle circular references
   - Manage load order

4. **Resource Loader**
   - Load bitmap pixels
   - Load sound samples
   - Allocate texture memory
   - Upload to GPU

5. **AI Integration**
   - Create runtime actor variants
   - Register with AI system
   - Generate spawn data
   - Link to encounters (which don't exist)

6. **Network Synchronization**
   - Send tag data to all clients (100s of KB)
   - Ensure clients load identical tags
   - Sync tag indices across machines
   - Handle connection during active game

**This is 10,000+ lines of code** replicating what Tool does at compile time.

### Why Tool Exists

Tool (map compiler) does exactly what you want - but at **compile time**:

```bash
# Tool loads loose tags and builds a map
tool build-cache-file levels\firefight\firefight

What Tool does:
1. Loads scenario tag (references 100s of other tags)
2. Recursively loads all dependencies
3. Resolves all tag references
4. Packs everything into one .map file
5. Assigns final indices
6. Rebinds all references to final indices
7. Writes complete, self-contained cache file
```

**You can't do this at runtime because:**
- It takes seconds (not suitable for gameplay)
- It requires 100s of MB of memory
- It needs source tags (not distributed with game)
- Clients would need to do it too (network game)

## The Practical Alternatives

### Alternative 1: Firefight-Specific Maps ✅ RECOMMENDED

**Create specialized Firefight maps** with enemies baked in:

```
bloodgulch_firefight.map
  - Has bloodgulch geometry
  - Has elite biped tags
  - Has grunt biped tags  
  - Has hunter biped tags
  - Has Firefight encounters pre-placed
  - Compiled by Tool (all references valid)
```

**Advantages:**
✅ Works with existing engine (zero new code)
✅ Network-synchronized automatically
✅ Fast loading (normal map load)
✅ All tag references valid
✅ Map authors control quality

**How to create:**

```bash
# 1. Create firefight scenario in Sapien
# 2. Reference covenant tags in your scenario
# 3. Place encounters
# 4. Compile with Tool
tool build-cache-file levels\bloodgulch_firefight\bloodgulch_firefight

# Result: bloodgulch_firefight.map with everything needed
```

### Alternative 2: Universal Firefight Tags in Maps

**Bundle common Firefight enemies in every map:**

Make a template scenario that includes:
- Elite biped (minor, major, ultra, gold)
- Grunt biped (minor, major, ultra)
- Jackal biped (minor, major)
- Hunter biped

Map authors **inherit from this template** when making Firefight maps.

**Advantages:**
✅ Consistent enemy roster
✅ One-time setup
✅ Works with existing engine
✅ Easy for map authors

### Alternative 3: Mod-Style Enemy Replacement

**Use mods to replace existing enemies:**

```
firefight/
  elite_firefight.biped       ← Replaces map's elite
  grunt_firefight.biped       ← Replaces map's grunt
  elite_armor.bitmap          ← Custom elite texture
  elite_voice.sound           ← Custom elite sounds
```

**How it works:**
- Map has standard enemies
- Mod replaces their assets (models, sounds, textures)
- Tag structure stays the same (references valid)
- Works like existing mod system

**Advantages:**
✅ Uses existing mod system
✅ Can customize enemy appearance/sounds
✅ Works on any map with those enemies
✅ Network-compatible (assets load locally)

**Limitations:**
❌ Can only modify existing tags (can't add new enemies)
❌ Map must already have the enemy types
❌ Can't change AI behavior (only assets)

### Alternative 4: Scripted Enemy Spawning

**Use Halo Script to spawn enemies:**

Maps include script:

```lisp
(script continuous firefight_wave_1
  (sleep_until (= firefight_active true))
  (ai_place covenant_squad_1)
  (sleep_until (= (ai_living_count covenant_squad_1) 0))
  (set firefight_wave 2)
)
```

**Advantages:**
✅ Dynamic wave spawning
✅ Script-controlled difficulty
✅ Works with existing engine
✅ Map-specific customization

**Limitations:**
❌ Enemies must be in map tags
❌ Scripts are per-map (not universal)
❌ Limited compared to code

## Recommendation

**Use Alternative 1: Firefight-Specific Maps**

This is the **only practical approach** that:
- Works with existing engine
- Supports full enemy rosters
- Allows custom AI behavior
- Network-synchronizes automatically
- Can be created by community

**Workflow:**

1. **Create base Firefight scenario** with covenant tags
2. **Map authors copy and customize** for their map
3. **Compile with Tool** → complete, standalone map
4. **Distribute** → players download firefight_mapname.map

**Example:**
```
maps/
  bloodgulch.map              ← Original CTF map
  bloodgulch_firefight.map    ← Firefight version with AI
  sidewinder.map              ← Original CTF map
  sidewinder_firefight.map    ← Firefight version with AI
```
## Why Runtime Tag Loading Won't Happen

Microsoft spent **years** building Tool to compile maps correctly. It:
- Handles 100+ tag types
- Resolves complex dependencies
- Optimizes data layout
- Validates all references
- Manages resource limits

**Replicating this at runtime** would require:
- Rewriting Tool's 50,000+ lines of code
- Making it safe for untrusted input
- Making it fast enough for gameplay
- Making it work in network games
- Maintaining it forever

**It's not worth it** when map compilation solves the problem perfectly.

## Conclusion

**Loading enemies from loose tags is architecturally infeasible** due to:
- Single global tag cache
- Tag reference invalidation
- Dependency complexity
- Network synchronization
- Engine limitations

**The solution is Firefight-specific maps** compiled with Tool, which:
- ✅ Works perfectly with existing engine
- ✅ Supports any enemy configuration
- ✅ Network-synchronizes automatically
- ✅ Can be created by community
- ✅ Requires zero new engine code

**Map compilation exists for a reason.** Use it.
