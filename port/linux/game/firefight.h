/* firefight.h: Firefight gametype (port/linux/game/firefight.c)

Firefight is a cooperative wave-based survival mode built on the CTF game
engine to leverage CTF spawn points. Players spawn at red team CTF spawns and
defend against waves of enemies spawned from scenario encounters. The mode is
team-based (red team only) with cooperative gameplay, generous loadouts, and
time-limited rounds.

Encounter-Based AI:
Maps designed for Firefight include pre-placed encounters named
"firefight_wave_1", "firefight_wave_2", etc., with AI squads positioned at
blue team CTF spawn points. Firefight activates these encounters in sequence
as waves. Maps without Firefight encounters work as defense/survival mode
without AI enemies. */

#ifndef HALO_FIREFIGHT_H
#define HALO_FIREFIGHT_H

struct game_engine;
struct game_variant;
struct game_variant_options;

/* whether Firefight is the current game mode */
boolean firefight_enabled(void);

/* dispose of Firefight state */
void firefight_dispose(void);

/* the game engine with Firefight's overrides (if CTF) */
struct game_engine *firefight_engine(struct game_engine *engine);

/* update wave state (host only): spawns waves, tracks enemies */
void firefight_update(void);

/* current wave number (host state) */
short firefight_wave_number(void);

/* enemies remaining in current wave (host state) */
short firefight_enemies_remaining(void);

/* player spawned: apply Firefight equipment */
void firefight_postspawn(long player_index);

/* player killed: check round end conditions */
void firefight_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire);

/* the built-in Firefight gametype variant and options */
void firefight_gametype(struct game_variant *variant, struct game_variant_options *options);

#endif
