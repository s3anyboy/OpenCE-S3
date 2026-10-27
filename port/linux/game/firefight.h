/* firefight.h: Firefight gametype (port/linux/game/firefight.c)

Firefight is a cooperative wave-based survival mode built on the CTF game
engine to leverage CTF spawn points. Players spawn at the red team's CTF
spawns and defend against waves of enemies, which the map's encounters place
at the blue team's.

Maps designed for Firefight include pre-placed encounters named
"firefight_wave_1", "firefight_wave_2", and so on, whose squads start at the
blue team's CTF spawn points. Firefight spawns them in order, one wave at a
time. A map without them plays as a defense (players at the red base, no AI).

Team lives (the gametype's firefight_team_lives option, 0 for infinite) are a
pool every player shares: a death spends one, and a player whose team has none
left does not respawn. */
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

/* player killed: spend a team life (firefight.c) */
void firefight_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire);

/* whether a player may respawn: FALSE once the team's lives are gone */
boolean firefight_can_respawn(long player_index);

/* the team's lives left, -1 when unlimited (host state) */
short firefight_team_lives_remaining(void);

/* whether the team ran out of lives */
boolean firefight_lives_exhausted(void);

/* the built-in Firefight gametype variant and options */
void firefight_gametype(struct game_variant *variant, struct game_variant_options *options);

#endif
