/*
FIREFIGHT.C

Firefight is a cooperative wave-based survival mode built on CTF (game_modes.c).
Players spawn at CTF red team spawns and defend against waves of AI enemies.
The mode uses the CTF game engine to leverage its spawn points, but flags are
disabled and the objective is pure survival.

- Teams: Red team (players) vs blue (AI enemies)
- Spawns: Players at the red CTF spawns; enemies at blue team spawns, from the
  map's own encounters (see below)
- Waves: The map's encounters named "firefight_wave_1", "firefight_wave_2",
  ... spawned in order, 5 seconds before the first and 30 between them
- Lives: An optional pool the whole team shares (firefight_team_lives), after
  which a dead player no longer respawns
- Equipment: generous loadouts, map weapons and vehicles

The enemies come from the map's tags and its encounters, not from another
map's: an encounter places the actors its squads name at their starting
locations, which for Firefight are the blue team's CTF spawn points. A map
without those encounters still plays (players defend the red base, no AI).

Only the host scans the encounters, spawns the waves, counts the enemies and
spends the lives; the AI and the players' units reach every machine as the
game's own objects and actors already do (network_actors.c, network_objects.c).
The round's time limit is the variant's.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "units/units.h"
#include "scenario/scenario.h"
#include "ai/encounters.h"
#include "ai/actors.h"
#include "game_modes.h"
#include "firefight.h"

#include <string.h>
#include <stdio.h>

void platform_log(char const *format, ...);

/* Firefight's default settings */
enum
{
	FIREFIGHT_DEFAULT_TIME_LIMIT = 15,        /* 15 minutes per round */
	FIREFIGHT_DEFAULT_RESPAWN_TIME = 5,       /* 5 seconds */
	FIREFIGHT_DEFAULT_STARTING_GRENADES = 4,  /* 4 grenades of each type */
	FIREFIGHT_DEFAULT_TEAM_LIVES = 0,         /* 0 = unlimited lives */
	FIREFIGHT_WAVE_START_DELAY = 5,           /* 5 seconds before first wave */
	FIREFIGHT_WAVE_INTERVAL = 30,             /* 30 seconds between waves */
	FIREFIGHT_MAXIMUM_WAVES = 10,             /* scan for up to 10 waves */
};

static struct
{
	struct game_engine engine;
	struct game_engine ctf;
	/* the host's: wave number, enemies remaining, round active */
	short wave_number;
	short enemies_remaining;
	boolean round_active;
	/* encounter indices for each wave (NONE if map doesn't have them) */
	long wave_encounters[FIREFIGHT_MAXIMUM_WAVES];
	short wave_count;
	/* timing: ticks until next wave spawns */
	long next_wave_time;
	/* whether the initial scan has been done */
	boolean encounters_scanned;
	/* team lives: shared pool of lives for all players */
	short team_lives_remaining;
	short team_lives_maximum;
	/* whether team lives are enabled (0 = unlimited) */
	boolean team_lives_enabled;
	/* whether the round has ended because the lives ran out */
	boolean lives_exhausted;
} firefight;

boolean firefight_enabled(
	void)
{
	return game_modes_mode() == _game_mode_firefight;
}

/* ---- initialization and cleanup */

/* scan the scenario for Firefight encounters ("firefight_wave_1", etc.) */
static void firefight_scan_encounters(
	void)
{
	short wave;
	char encounter_name[64];
	
	if (firefight.encounters_scanned)
		return;
	
	firefight.wave_count = 0;
	for (wave = 0; wave < FIREFIGHT_MAXIMUM_WAVES; wave++)
	{
		long encounter_index;
		
		firefight.wave_encounters[wave] = NONE;
		sprintf(encounter_name, "firefight_wave_%d", (int)(wave + 1));
		encounter_index = encounter_get_by_name(encounter_name);
		
		if (encounter_index != NONE)
		{
			firefight.wave_encounters[wave] = encounter_index;
			firefight.wave_count = wave + 1;
			platform_log("firefight: found encounter '%s' at index %ld",
				encounter_name, encounter_index);
		}
	}
	
	if (firefight.wave_count > 0)
	{
		platform_log("firefight: map has %d wave encounters", (int)firefight.wave_count);
	}
	else
	{
		platform_log("firefight: map has no wave encounters (AI disabled)");
	}
	
	firefight.encounters_scanned = TRUE;
}

static void firefight_initialize(
	void)
{
	struct game_variant_options const *options;
	
	csmemset(&firefight, 0, sizeof(firefight));
	
	/* CTF engine provides the base */
	firefight.ctf = *game_engine_get();
	firefight.engine = firefight.ctf;
	
	/* Override with Firefight-specific behavior */
	firefight.engine.initialize = firefight_initialize;
	firefight.engine.dispose = NULL;
	firefight.engine.update = NULL;
	
	/* Scan for wave encounters */
	firefight_scan_encounters();
	
	/* Initialize team lives system */
	options = game_variant_options_get();
	if (options && options->firefight_team_lives > 0)
	{
		firefight.team_lives_enabled = TRUE;
		firefight.team_lives_maximum = options->firefight_team_lives;
		firefight.team_lives_remaining = options->firefight_team_lives;
		platform_log("firefight: team lives enabled, %d lives shared",
			(int)firefight.team_lives_remaining);
	}
	else
	{
		firefight.team_lives_enabled = FALSE;
		platform_log("firefight: unlimited lives");
	}
	
	firefight.lives_exhausted = FALSE;
	
	/* (the host's: the clients take its waves and lives as they come) */
	if (!network_game_distributed_client())
	{
		firefight.wave_number = 0;  /* waves start at 1, 0 means "not started" */
		firefight.enemies_remaining = 0;
		firefight.round_active = TRUE;
		firefight.next_wave_time = game_time_get() + FIREFIGHT_WAVE_START_DELAY * TICKS_PER_SECOND;
	}
}

void firefight_dispose(
	void)
{
	csmemset(&firefight, 0, sizeof(firefight));
}

/* ---- game engine hooks */

struct game_engine *firefight_engine(
	struct game_engine *engine)
{
	if (engine->game_engine_index != game_engine_ctf)
		return engine;
	
	firefight.ctf = *engine;
	firefight.engine = firefight.ctf;
	
	/* Override specific CTF behaviors for Firefight */
	firefight.engine.initialize = firefight_initialize;
	
	return &firefight.engine;
}

/* ---- wave management (host only) */

/* count living enemies in a wave's encounter: their units are the actors'
ones, which an actor's unit_index may leave behind once it is deleted */
static short firefight_count_wave_enemies(
	long encounter_index)
{
	struct encounter_actor_iterator iterator;
	struct actor_datum *actor;
	short count;
	
	if (encounter_index == NONE)
		return 0;
	
	count = 0;
	encounter_actor_iterator_new(&iterator, encounter_index);
	while ((actor = encounter_actor_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit = actor->meta.unit_index != NONE ?
			unit_try_and_get(actor->meta.unit_index) : NULL;
		
		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			count++;
	}
	
	return count;
}

/* spawn a wave by creating its encounter's actors: encounter_create places
them at the squad's starting locations (the blue team's CTF spawns), as
ai_place does for every encounter when a campaign map loads (ai.c) */
static void firefight_spawn_wave(
	short wave_number)
{
	short wave_index;
	long encounter_index;
	
	if (wave_number < 1 || wave_number > firefight.wave_count)
		return;
	
	wave_index = wave_number - 1;
	encounter_index = firefight.wave_encounters[wave_index];
	
	if (encounter_index == NONE)
		return;
	
	encounter_create(encounter_index, NONE, NONE);
	
	firefight.enemies_remaining = firefight_count_wave_enemies(encounter_index);
	platform_log("firefight: wave %d spawned, %d enemies",
		(int)wave_number, (int)firefight.enemies_remaining);
}

void firefight_update(
	void)
{
	long current_time;
	
	if (!firefight_enabled() || network_game_distributed_client())
		return;
	
	/* no waves if map has no encounters */
	if (firefight.wave_count == 0)
		return;
	
	current_time = game_time_get();
	
	/* time to spawn the next wave? */
	if (firefight.next_wave_time != NONE && current_time >= firefight.next_wave_time)
	{
		short next_wave = firefight.wave_number + 1;
	
		if (next_wave <= firefight.wave_count)
		{
			firefight.wave_number = next_wave;
			firefight_spawn_wave(next_wave);
			firefight.next_wave_time = NONE;  /* wait for wave to clear */
		}
		else
		{
			/* all waves spawned */
			firefight.next_wave_time = NONE;
			platform_log("firefight: all %d waves spawned", (int)firefight.wave_count);
		}
	}
	
	/* update enemy count for current wave */
	if (firefight.wave_number > 0 && firefight.wave_number <= firefight.wave_count)
	{
		long encounter_index = firefight.wave_encounters[firefight.wave_number - 1];
		short alive = firefight_count_wave_enemies(encounter_index);
		
		/* wave cleared? schedule next wave */
		if (alive == 0 && firefight.enemies_remaining > 0)
		{
			platform_log("firefight: wave %d cleared", (int)firefight.wave_number);
			firefight.enemies_remaining = 0;
			
			/* schedule next wave after interval */
			if (firefight.wave_number < firefight.wave_count)
			{
				firefight.next_wave_time = current_time + FIREFIGHT_WAVE_INTERVAL * TICKS_PER_SECOND;
				platform_log("firefight: next wave in %d seconds", (int)FIREFIGHT_WAVE_INTERVAL);
			}
		}
		else
		{
			firefight.enemies_remaining = alive;
		}
	}
}

short firefight_wave_number(
	void)
{
	return firefight.wave_number;
}

short firefight_enemies_remaining(
	void)
{
	return firefight.enemies_remaining;
}

short firefight_team_lives_remaining(
	void)
{
	return firefight.team_lives_enabled ? firefight.team_lives_remaining : -1;
}

boolean firefight_lives_exhausted(
	void)
{
	return firefight.lives_exhausted;
}

/* ---- player spawning */

/* whether a player can respawn (team lives check) */
boolean firefight_can_respawn(
	long player_index)
{
	if (!firefight_enabled())
		return TRUE;
	
	/* unlimited lives: always allow respawn */
	if (!firefight.team_lives_enabled)
		return TRUE;
	
	/* lives remaining: allow respawn */
	if (firefight.team_lives_remaining > 0)
		return TRUE;
	
	/* no lives left: cannot respawn */
	platform_log("firefight: player %ld cannot respawn, no team lives remaining",
		player_index);
	return FALSE;
}

void firefight_postspawn(
	long player_index)
{
	struct player_datum *player;
	long unit_index;
	struct unit_datum *unit;
	
	if (!firefight_enabled())
		return;
	
	player = player_get(player_index);
	if (!player)
		return;
	
	unit_index = player->unit_index;
	unit = unit_try_and_get(unit_index);
	if (!unit)
		return;
	
	/* port: Firefight players get standard CTF loadout
	Future: custom equipment, ammo bonuses, health bonuses */
}

/* ---- player death */

void firefight_player_killed(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	if (!firefight_enabled() || network_game_distributed_client())
		return;
	
	/* team lives: deduct from shared pool */
	if (firefight.team_lives_enabled && !friendly_fire)
	{
		if (firefight.team_lives_remaining > 0)
		{
			firefight.team_lives_remaining--;
			platform_log("firefight: player death, %d team lives remaining",
				(int)firefight.team_lives_remaining);
			
			/* check if lives exhausted */
			if (firefight.team_lives_remaining == 0)
			{
				platform_log("firefight: team lives exhausted, round ends");
				firefight.lives_exhausted = TRUE;
				firefight.round_active = FALSE;
				/* port: could trigger game end here */
			}
		}
	}
}

/* ---- built-in gametype */

void firefight_gametype(
	struct game_variant *variant,
	struct game_variant_options *options)
{
	struct game_variant temporary;
	
	/* Base on CTF to use CTF spawn points */
	*variant = *build_game_variant_ctf(&temporary);
	game_variant_options_default(variant, options);
	
	/* Firefight-specific settings */
	options->mode = _game_mode_firefight;
	
	/* Use teams, players on red */
	variant->universal_variant.teams = TRUE;
	
	/* Disable CTF-specific features */
	variant->game_engine_variant.ctf.assault = FALSE;
	variant->game_engine_variant.ctf.reset_on_capture = FALSE;
	variant->game_engine_variant.ctf.flag_must_reset = FALSE;
	variant->game_engine_variant.ctf.flag_at_home_to_score = FALSE;
	variant->game_engine_variant.ctf.single_flag_time = 0;
	
	/* Time limit and respawn */
	options->time_limit = FIREFIGHT_DEFAULT_TIME_LIMIT;
	variant->universal_variant.respawn_time = FIREFIGHT_DEFAULT_RESPAWN_TIME * TICKS_PER_SECOND;
	variant->universal_variant.suicide_penalty = 0;
	
	/* No score limit - survival based */
	variant->universal_variant.score_to_win = 0;
	
	/* Cooperative settings */
	options->friendly_fire = _friendly_fire_off;
	options->auto_team_balance = FALSE;
	
	/* Team lives: unlimited by default (0 = unlimited) */
	options->firefight_team_lives = FIREFIGHT_DEFAULT_TEAM_LIVES;
	
	/* Equipment: generous loadout */
	SET_FLAG(variant->universal_variant.flags, _game_variant_generic_starting_equipment_bit, TRUE);
	options->loadout = _loadout_custom;
	options->primary_weapon = _loadout_weapon_assault_rifle;
	options->secondary_weapon = _loadout_weapon_pistol;
	options->starting_grenades_frag = FIREFIGHT_DEFAULT_STARTING_GRENADES;
	options->starting_grenades_plasma = FIREFIGHT_DEFAULT_STARTING_GRENADES;
	
	/* Keep map weapons available */
	options->no_map_weapons = FALSE;
	
	/* Motion sensor enabled for cooperative play */
	SET_FLAG(variant->universal_variant.flags, _game_variant_draw_object_in_motion_sensor_bit, TRUE);
	options->radar_players = _radar_players_all;
	
	/* Vehicles allowed */
	variant->universal_variant.vehicle_set = 0; /* default vehicle set */
	options->vehicle_set[0] = 0;
	options->vehicle_set[1] = 0;
	
	/* Set the variant name */
	csmemset(variant->human_readable_game_description, 0, 
		sizeof(variant->human_readable_game_description));
	ustrncpy(variant->human_readable_game_description, L"Firefight",
		NUMBEROF(variant->human_readable_game_description) - 1);
}
