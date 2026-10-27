/*
FIREFIGHT.C

Firefight, a cooperative wave-based survival mode built on CTF (game_modes.c).
Players spawn at CTF red team spawns and defend against waves of AI enemies.
The mode uses the CTF game engine to leverage its spawn points, but flags are
disabled and the objective is pure survival.

- Teams: Red team (players) vs Blue team (AI enemies)
- Spawns: Players at red CTF points, AI at blue CTF points (via encounters)
- Waves: Maps define encounters named "firefight_wave_1", "firefight_wave_2", etc.
- Equipment: Customizable loadouts with generous ammo
- Rules: Cooperative gameplay, shared score, time-limited rounds

Encounter-Based AI Spawning:
Maps designed for Firefight include pre-placed encounters at blue team spawn
points. Firefight activates these encounters in sequence as waves progress.
Maps without Firefight encounters work as defense/survival without AI.

Only the host tracks wave progression and spawns AI. The game ends when the
time limit expires or all players are eliminated.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "units/units.h"
#include "scenario/scenario.h"
#include "ai/ai_scenario_definitions.h"
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
	struct scenario *scenario;
	short wave;
	char encounter_name[64];
	
	if (firefight.encounters_scanned)
		return;
	
	scenario = global_scenario_get();
	if (!scenario)
		return;
	
	firefight.wave_count = 0;
	for (wave = 0; wave < FIREFIGHT_MAXIMUM_WAVES; wave++)
	{
		long encounter_index;
		
		firefight.wave_encounters[wave] = NONE;
		sprintf(encounter_name, "firefight_wave_%d", (int)(wave + 1));
		encounter_index = scenario_get_encounter_by_name(scenario, encounter_name);
		
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
	
	/* Host-only state */
	if (network_game_is_host())
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

/* count living enemies in a wave's encounter */
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
		long unit_index = actor->unit_index;
		struct unit_datum *unit = unit_try_and_get(unit_index);
		
		/* count alive units only */
		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			count++;
	}
	
	return count;
}

/* spawn a wave by activating its encounter */
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
	
	/* activate the encounter - AI spawns at their defined points */
	ai_place();
	
	firefight.enemies_remaining = firefight_count_wave_enemies(encounter_index);
	platform_log("firefight: wave %d spawned, %d enemies",
		(int)wave_number, (int)firefight.enemies_remaining);
}

void firefight_update(
	void)
{
	long current_time;
	
	if (!firefight_enabled() || !network_game_is_host())
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

/* ---- player spawning */

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
	if (!firefight_enabled() || !network_game_is_host())
		return;
	
	/* Future: check if all players are dead, end round if so */
	/* Track player deaths for scoring */
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
