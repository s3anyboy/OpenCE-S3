/*
GAME_ENGINE_INFECTION.C

Infection gametype: Alpha Zombies & Infected vs. Un-infected Survivors.
- Infected spawn with the Oddball skull in hand (melee-only, throwing/dropping disabled).
- Configurable initial zombie percentage (5%, 10%, 25%), multi-round matches, and round time.
- Configurable per-role traits (Speed, Gravity, Damage Resistance, Health, Shields, Alpha Camo)
  for Infected, Alpha Zombies, and Un-infected Survivors.

port: custom Infection gametype.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_place.h"
#include "game/players.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "text/unicode.h"
#include "units/units.h"

enum infection_team
{
	_infection_team_survivors = 0,
	_infection_team_infected  = 1,
};

struct infection_globals
{
	long survivor_count;
	long infected_count;
	long current_round;
	long total_rounds;
	long round_ticks_remaining;
	boolean initial_alpha_chosen;
	byte is_alpha_zombie[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	byte last_round_alpha[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short infections[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short bonus_score[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long hits_taken[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
};

static struct infection_globals infection_globals;

static struct infection_variant const *infection_get_variant(void)
{
	struct game_variant const *v = game_engine_get_variant();
	return v ? &v->game_engine_variant.infection : NULL;
}

static boolean infection_player_is_alpha(long player_index)
{
	long slot;
	struct player_datum *player;

	if (player_index == NONE)
		return FALSE;
	player = player_get(player_index);
	if (!player || player->team_index != _infection_team_infected)
		return FALSE;
	slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (slot < 0 || slot >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		return FALSE;
	return infection_globals.is_alpha_zombie[slot] != 0;
}

static real infection_get_player_speed_scale(long player_index)
{
	struct player_datum *player;
	struct infection_variant const *v;
	long pct = 100;

	if (player_index == NONE)
		return 1.0f;
	player = player_get(player_index);
	if (!player)
		return 1.0f;
	v = infection_get_variant();
	if (player->team_index == _infection_team_infected)
	{
		if (infection_player_is_alpha(player_index))
			pct = (v && v->alpha_speed) ? (long)v->alpha_speed : 150;
		else
			pct = (v && v->infected_speed) ? (long)v->infected_speed : 125;
	}
	else
	{
		pct = (v && v->survivor_speed) ? (long)v->survivor_speed : 100;
	}
	return PIN((real)pct / 100.0f, 0.25f, 3.0f);
}

real game_engine_infection_get_gravity_scale(long unit_index)
{
	struct unit_datum *unit;
	long player_index;
	struct player_datum *player;
	struct infection_variant const *v;
	long pct = 100;

	if (!game_engine_is_infection() || unit_index == NONE)
		return 1.0f;
	unit = unit_get(unit_index);
	if (!unit || unit->unit.player_index == NONE)
		return 1.0f;
	player_index = unit->unit.player_index;
	player = player_get(player_index);
	if (!player)
		return 1.0f;
	v = infection_get_variant();
	if (player->team_index == _infection_team_infected)
	{
		if (infection_player_is_alpha(player_index))
			pct = (v && v->alpha_gravity) ? (long)v->alpha_gravity : 50;
		else
			pct = (v && v->infected_gravity) ? (long)v->infected_gravity : 75;
	}
	else
	{
		pct = (v && v->survivor_gravity) ? (long)v->survivor_gravity : 100;
	}
	return PIN((real)pct / 100.0f, 0.25f, 3.0f);
}

boolean game_engine_infection_has_shield(long player_index)
{
	struct player_datum *player;
	struct infection_variant const *v;

	if (player_index == NONE)
		return TRUE;
	player = player_get(player_index);
	if (!player)
		return TRUE;
	v = infection_get_variant();
	if (!v)
		return player->team_index == _infection_team_survivors;

	if (player->team_index == _infection_team_infected)
	{
		if (infection_player_is_alpha(player_index))
			return v->alpha_shields != 0;
		return v->infected_shields != 0;
	}
	/* If variant was zero-initialized without defaults, default survivors to having shields */
	if (v->survivor_health == 0 && v->survivor_speed == 0)
		return TRUE;
	return v->survivor_shields != 0;
}

real game_engine_infection_get_damage_multiplier(
	long damaging_player_index,
	long damaged_player_index)
{
	struct player_datum *victim;
	struct infection_variant const *v;
	long health_pct = 100;
	long resist_pct = 100;
	real denom;

	(void)damaging_player_index;
	if (!game_engine_is_infection() || damaged_player_index == NONE)
		return 1.0f;
	victim = player_get(damaged_player_index);
	if (!victim)
		return 1.0f;
	v = infection_get_variant();
	if (victim->team_index == _infection_team_infected)
	{
		if (infection_player_is_alpha(damaged_player_index))
		{
			health_pct = (v && v->alpha_health) ? (long)v->alpha_health : 150;
			resist_pct = (v && v->alpha_damage_resistance) ? (long)v->alpha_damage_resistance : 150;
		}
		else
		{
			health_pct = (v && v->infected_health) ? (long)v->infected_health : 100;
			resist_pct = (v && v->infected_damage_resistance) ? (long)v->infected_damage_resistance : 100;
		}
	}
	else
	{
		health_pct = (v && v->survivor_health) ? (long)v->survivor_health : 100;
		resist_pct = (v && v->survivor_damage_resistance) ? (long)v->survivor_damage_resistance : 100;
	}
	denom = ((real)health_pct / 100.0f) * ((real)resist_pct / 100.0f);
	if (denom < 0.1f)
		denom = 0.1f;
	return 1.0f / denom;
}

/* Equip a player based on their team and role (Alpha Zombie, Infected, or Survivor) */
static void infection_equip_player(long player_index)
{
	struct player_datum *player;
	struct unit_datum *unit;
	struct infection_variant const *v;

	if (player_index == NONE)
		return;
	player = player_get(player_index);
	if (!player || player->unit_index == NONE)
		return;
	unit = unit_get(player->unit_index);
	if (!unit)
		return;
	v = infection_get_variant();

	/* Clear current/desired weapon indices first so unit_delete_all_weapons deletes all 4 slots */
	unit->unit.current_weapon_index = NONE;
	unit->unit.desired_weapon_index = NONE;
	unit_delete_all_weapons(player->unit_index);
	unit->unit.grenade_counts[0] = 0;
	unit->unit.grenade_counts[1] = 0;
	player->speed_multiplier = infection_get_player_speed_scale(player_index);

	if (player->team_index == _infection_team_infected)
	{
		/* Infected: spawn with Oddball skull in hand (melee-only, cannot be thrown/dropped) */
		struct object_placement_data placement;
		long skull_tag = get_ball_definition_index();
		long weapon_index;

		if (skull_tag == NONE)
			skull_tag = list_index_to_weapon_definition_index(10 /* _weapon_list_ball */);
		if (skull_tag == NONE)
			skull_tag = list_index_to_weapon_definition_index(0 /* fallback if custom map lacks ball */);

		if (skull_tag != NONE)
		{
			object_placement_data_new(&placement, skull_tag, player->unit_index);
			placement.position = unit->object.position;
			weapon_index = object_new(&placement);
			if (weapon_index != NONE)
			{
				if (!unit_add_weapon_to_inventory(player->unit_index, weapon_index, 1 /* _unit_add_weapon_starting */))
					object_delete(weapon_index);
			}
		}

		if (infection_player_is_alpha(player_index) && v && v->alpha_shields == 2)
		{
			unit->object.shield_vitality = 3.0f;
		}
	}
	else
	{
		/* Survivor: Shotgun + Pistol with generous reserve ammo */
		struct object_placement_data placement;
		long pistol_tag = list_index_to_weapon_definition_index(4 /* _weapon_list_pistol */);
		long shotgun_tag = list_index_to_weapon_definition_index(6 /* _weapon_list_shotgun */);
		long weapon_index;

		if (shotgun_tag != NONE)
		{
			object_placement_data_new(&placement, shotgun_tag, NONE);
			weapon_index = object_new(&placement);
			if (weapon_index != NONE)
			{
				struct weapon_datum *weapon;
				if (unit_add_weapon_to_inventory(player->unit_index, weapon_index, 2))
				{
					weapon = weapon_get(weapon_index);
					if (weapon)
					{
						weapon->weapon.magazines[0].rounds_total = 60;
						weapon->weapon.magazines[0].rounds_loaded = 12;
					}
				}
				else
				{
					object_delete(weapon_index);
				}
			}
		}

		if (pistol_tag != NONE)
		{
			object_placement_data_new(&placement, pistol_tag, NONE);
			weapon_index = object_new(&placement);
			if (weapon_index != NONE)
			{
				struct weapon_datum *weapon;
				if (unit_add_weapon_to_inventory(player->unit_index, weapon_index, 1))
				{
					weapon = weapon_get(weapon_index);
					if (weapon)
					{
						weapon->weapon.magazines[0].rounds_total = 999;
						weapon->weapon.magazines[0].rounds_loaded = 12;
					}
				}
				else
				{
					object_delete(weapon_index);
				}
			}
		}
	}
}

static void infection_reset_round_timer(void)
{
	struct infection_variant const *v = infection_get_variant();
	long minutes = v ? (long)v->round_time_minutes : 5;

	infection_globals.total_rounds = (v && v->rounds >= 1) ? (long)v->rounds : 3;
	if (infection_globals.current_round < 1)
		infection_globals.current_round = 1;
	if (minutes > 0)
		infection_globals.round_ticks_remaining = minutes * 60 * 30;
	else
		infection_globals.round_ticks_remaining = -1;
}

static void infection_choose_alpha_zombies(boolean respawn_units)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long player_indices[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long player_slots[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = 0;
	long pct = 10;
	long alpha_count;
	long i;
	struct infection_variant const *v = infection_get_variant();

	if (v && (v->initial_zombie_pct == 5 || v->initial_zombie_pct == 10 || v->initial_zombie_pct == 25))
		pct = (long)v->initial_zombie_pct;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		if (slot >= 0 && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		{
			player_indices[count] = iterator.datum_index;
			player_slots[count] = slot;
			player->team_index = _infection_team_survivors;
			infection_globals.is_alpha_zombie[slot] = 0;
			infection_globals.hits_taken[slot] = 0;
			count++;
		}
	}

	if (count == 0)
		return;

	alpha_count = (count * pct + 50) / 100;
	if (alpha_count < 1)
		alpha_count = 1;
	if (count >= 2 && alpha_count >= count)
		alpha_count = count - 1;

	/* Randomly pick alpha_count distinct players (preferring players who weren't Alpha last round) */
	for (i = 0; i < alpha_count; i++)
	{
		long pick = -1;
		long attempt;

		for (attempt = 0; attempt < 16; attempt++)
		{
			long candidate = seed_random_range(get_global_random_seed_address(), 0, (short)count);
			long c_slot;

			if (candidate < 0 || candidate >= count)
				candidate = 0;
			c_slot = player_slots[candidate];
			if (infection_globals.is_alpha_zombie[c_slot])
				continue;
			if (attempt < 8 && count > alpha_count && infection_globals.last_round_alpha[c_slot])
				continue;
			pick = candidate;
			break;
		}
		if (pick < 0)
		{
			long j;
			for (j = 0; j < count; j++)
			{
				if (!infection_globals.is_alpha_zombie[player_slots[j]])
				{
					pick = j;
					break;
				}
			}
		}
		if (pick >= 0)
		{
			long a_slot = player_slots[pick];
			struct player_datum *alpha_player = player_get(player_indices[pick]);
			infection_globals.is_alpha_zombie[a_slot] = 1;
			if (alpha_player)
				alpha_player->team_index = _infection_team_infected;
		}
	}

	csmemcpy(
		infection_globals.last_round_alpha,
		infection_globals.is_alpha_zombie,
		sizeof(infection_globals.last_round_alpha));
	infection_globals.initial_alpha_chosen = TRUE;

	for (i = 0; i < count; i++)
	{
		struct player_datum *p = player_get(player_indices[i]);
		if (!p)
			continue;
		if (respawn_units)
		{
			if (p->unit_index != NONE)
			{
				long old_unit_index = p->unit_index;
				struct unit_datum *old_unit = unit_try_and_get(old_unit_index);
				if (old_unit)
				{
					old_unit->unit.current_weapon_index = NONE;
					old_unit->unit.desired_weapon_index = NONE;
					old_unit->unit.player_index = NONE;
					unit_set_actively_controlled(old_unit_index, FALSE);
				}
				p->unit_index = NONE;
				p->dead_unit_index = NONE;
				if (p->local_player_index != NONE)
					player_control_new_unit(p->local_player_index, NONE);
				unit_delete_all_weapons(old_unit_index);
				object_delete(old_unit_index);
			}
			p->respawn_timer = 15;
		}
		else if (p->unit_index != NONE)
		{
			infection_equip_player(player_indices[i]);
		}
	}
}

/* Remove all map-spawned weapons, equipment, and vehicles not held by a unit */
static void infection_clear_map_spawns(void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	if (network_game_distributed_client())
		return;

	object_iterator_new(
		&iterator,
		_object_mask_weapon | _object_mask_equipment | _object_mask_vehicle,
		0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (object->object.parent_object_index == NONE)
		{
			object_delete(iterator.index);
		}
	}
}

static void infection_engine_dispose(void) {}

static boolean infection_engine_initialize_for_new_map(void)
{
	csmemset(&infection_globals, 0, sizeof(infection_globals));
	infection_globals.current_round = 1;
	infection_reset_round_timer();
	infection_clear_map_spawns();
	return TRUE;
}

static void infection_engine_dispose_from_old_map(void)
{
	csmemset(&infection_globals, 0, sizeof(infection_globals));
}

static void infection_engine_player_added(long player_index)
{
	struct player_datum *player = player_get(player_index);
	long slot;

	if (!player)
		return;
	slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	if (!infection_globals.initial_alpha_chosen)
	{
		infection_choose_alpha_zombies(FALSE);
	}
	else
	{
		/* Mid-round joiners spawn as Survivors if only 1 infected exists and >0 survivors, else Infected */
		if (infection_globals.infected_count == 0)
		{
			player->team_index = _infection_team_infected;
			if (slot >= 0 && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
				infection_globals.is_alpha_zombie[slot] = 1;
		}
		else
		{
			player->team_index = _infection_team_survivors;
			if (slot >= 0 && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
				infection_globals.is_alpha_zombie[slot] = 0;
		}
	}
}

static void infection_engine_game_ending(void) {}

static void infection_engine_game_starting(void)
{
	infection_globals.current_round = 1;
	infection_reset_round_timer();
	infection_clear_map_spawns();
	infection_choose_alpha_zombies(FALSE);
}

static void infection_engine_statistics_append(long statistic)
{
	(void)statistic;
}

static void infection_engine_handle_client_message(void *message)
{
	(void)message;
}

static void infection_engine_handle_server_message(void *message)
{
	(void)message;
}

static void infection_engine_pregame_post_rasterize(void) {}
static void infection_engine_post_rasterize(void) {}

static void infection_engine_player_update_each_tick(long player_index)
{
	struct player_datum *player;
	struct unit_datum *unit;

	if (player_index == NONE)
		return;
	player = player_get(player_index);
	if (!player || player->unit_index == NONE)
		return;
	unit = unit_try_and_get(player->unit_index);
	if (!unit)
		return;

	player->speed_multiplier = infection_get_player_speed_scale(player_index);

	if (player->team_index == _infection_team_infected)
	{
		/* Ensure living Infected players always have the Oddball skull in hand */
		if (!network_game_distributed_client() &&
			!TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			unit->object.parent_object_index == NONE &&
			unit_inventory_get_must_be_readied_weapon(player->unit_index) == NONE)
		{
			infection_equip_player(player_index);
		}
	}
	else if (player->team_index == _infection_team_survivors)
	{
		/* Keep survivor pistol reserve ammo topped up */
		short slot;
		for (slot = 0; slot < 4; slot++)
		{
			long weapon_index = unit_inventory_get_weapon(player->unit_index, slot);
			if (weapon_index != NONE)
			{
				struct weapon_datum *weapon = weapon_get(weapon_index);
				if (weapon && weapon->weapon.magazines[0].rounds_total > 100 &&
					weapon->weapon.magazines[0].rounds_total < 500)
				{
					weapon->weapon.magazines[0].rounds_total = 999;
				}
			}
		}
	}
}

static void infection_engine_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long survivors = 0;
	long infected = 0;
	long total_players = 0;
	boolean round_over = FALSE;
	boolean time_expired = FALSE;
	struct infection_variant const *v = infection_get_variant();

	infection_globals.total_rounds = (v && v->rounds >= 1) ? (long)v->rounds : 3;
	if (infection_globals.current_round < 1)
		infection_globals.current_round = 1;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		total_players++;
		if (player->team_index == _infection_team_survivors)
			survivors++;
		else if (player->team_index == _infection_team_infected)
			infected++;
	}

	/* If a second player joined and everyone was on one team before the round really started, re-balance Alpha */
	if (total_players >= 2 && (infected == 0 || (!infection_globals.initial_alpha_chosen && survivors == 0)))
	{
		infection_choose_alpha_zombies(FALSE);
		survivors = 0;
		infected = 0;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->team_index == _infection_team_survivors)
				survivors++;
			else if (player->team_index == _infection_team_infected)
				infected++;
		}
	}

	infection_globals.survivor_count = survivors;
	infection_globals.infected_count = infected;

	if (infection_globals.round_ticks_remaining > 0)
	{
		infection_globals.round_ticks_remaining--;
		if (infection_globals.round_ticks_remaining == 0)
		{
			time_expired = TRUE;
			round_over = TRUE;
		}
	}

	if (infection_globals.initial_alpha_chosen && total_players >= 2 && survivors == 0 && infected > 0)
	{
		round_over = TRUE;
	}

	if (round_over)
	{
		if (time_expired && survivors > 0)
		{
			/* Award survival bonus to remaining survivors */
			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				if (player->team_index == _infection_team_survivors)
				{
					long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
					if (slot >= 0 && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
						infection_globals.bonus_score[slot] += 5;
				}
			}
		}

		if (infection_globals.current_round < infection_globals.total_rounds)
		{
			infection_globals.current_round++;
			infection_reset_round_timer();
			infection_choose_alpha_zombies(TRUE);
			infection_clear_map_spawns();
		}
		else
		{
			game_engine_end_game();
		}
	}
}

static long infection_get_score(long player_index, enum get_score_type score_type)
{
	struct player_datum *player = player_get(player_index);
	long slot;

	if (!player)
		return 0;
	if (score_type == _get_score_team)
		return (player->team_index == _infection_team_survivors) ? infection_globals.survivor_count : infection_globals.infected_count;
	slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (slot >= 0 && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
	{
		return (long)player->statistics.kills[0] +
			(long)infection_globals.infections[slot] * 2 +
			(long)infection_globals.bonus_score[slot];
	}
	return (long)player->statistics.kills[0];
}

static wchar_t *infection_get_score_string(long player_index, wchar_t *string)
{
	long score = infection_get_score(player_index, _get_score_individual);
	usprintf(string, L"%ld", score);
	return string;
}

static wchar_t *infection_get_score_header_string(wchar_t *string)
{
	ustrncpy(string, L"SCORE", 31);
	string[31] = 0;
	return string;
}

static wchar_t *infection_get_team_score_string(long team_index, wchar_t *string)
{
	if (team_index == _infection_team_survivors)
		usprintf(string, L"%ld SURVIVORS", infection_globals.survivor_count);
	else
		usprintf(string, L"%ld INFECTED", infection_globals.infected_count);
	return string;
}

static boolean infection_engine_allow_pick_up(long unit_index, long weapon_index)
{
	struct unit_datum *unit;
	struct player_datum *player;

	if (unit_index == NONE || weapon_index == NONE)
		return FALSE;
	unit = unit_get(unit_index);
	if (!unit || unit->unit.player_index == NONE)
		return FALSE;
	player = player_get(unit->unit.player_index);
	if (!player)
		return FALSE;
	/* Infected can only hold/use the Oddball skull; Survivors can only hold/use non-skull weapons */
	if (player->team_index == _infection_team_infected)
		return weapon_must_be_readied(weapon_index);
	if (weapon_must_be_readied(weapon_index))
		return FALSE;
	return TRUE;
}

static void infection_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type)
{
	(void)damaging_player_index;
	(void)dead_player_index;
	(void)damage_type;
}

static void infection_engine_player_killed_player(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire)
{
	struct player_datum *victim;
	long dead_slot;

	(void)killing_object_index;
	(void)friendly_fire;
	if (dead_player_index == NONE)
		return;
	victim = player_get(dead_player_index);
	if (!victim)
		return;
	dead_slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(dead_player_index);

	/* Convert dead survivor to base infected team (not Alpha) */
	if (victim->team_index == _infection_team_survivors)
	{
		victim->team_index = _infection_team_infected;
		if (dead_slot >= 0 && dead_slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			infection_globals.is_alpha_zombie[dead_slot] = 0;

		if (killing_player_index != NONE && killing_player_index != dead_player_index)
		{
			long killer_slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(killing_player_index);
			if (killer_slot >= 0 && killer_slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
				infection_globals.infections[killer_slot]++;
		}
	}
}

static boolean infection_engine_format_message(
	long player_index,
	long message,
	long message_data,
	wchar_t *buffer,
	long buffer_size)
{
	(void)player_index;
	(void)message;
	(void)message_data;
	(void)buffer;
	(void)buffer_size;
	return FALSE;
}

static real infection_engine_starting_location_rating(
	long player_index,
	struct player_starting_location const *starting_location)
{
	(void)player_index;
	(void)starting_location;
	return 1.0f;
}

static void infection_engine_prespawn_player_update(long player_index)
{
	(void)player_index;
}

static void infection_engine_player_update(long player_index)
{
	infection_equip_player(player_index);
}

static boolean infection_test_flag(long flag)
{
	if (flag == 0)
		return TRUE;
	return FALSE;
}

static boolean infection_test_trait(long player_index, long trait)
{
	struct infection_variant const *v;

	if (trait == 1 /* _game_trait_invisible */)
	{
		v = infection_get_variant();
		if (infection_player_is_alpha(player_index) && v && v->alpha_camo != 0)
			return TRUE;
	}
	return FALSE;
}

struct game_engine infection_engine =
{
	"infection",
	game_engine_infection,
	infection_engine_dispose,
	infection_engine_initialize_for_new_map,
	infection_engine_dispose_from_old_map,
	infection_engine_player_added,
	infection_engine_game_ending,
	infection_engine_game_starting,
	infection_engine_statistics_append,
	infection_engine_handle_client_message,
	infection_engine_handle_server_message,
	infection_engine_pregame_post_rasterize,
	infection_engine_post_rasterize,
	infection_engine_player_update_each_tick,
	NULL,
	NULL,
	NULL,
	infection_engine_update,
	infection_get_score,
	infection_get_score_string,
	infection_get_score_header_string,
	infection_get_team_score_string,
	infection_engine_allow_pick_up,
	infection_engine_player_damaged_player,
	infection_engine_player_killed_player,
	infection_engine_format_message,
	infection_engine_starting_location_rating,
	infection_engine_prespawn_player_update,
	infection_engine_player_update,
	NULL,
	NULL,
	infection_test_flag,
	infection_test_trait,
	NULL,
};

long game_engine_infection_write_network_state(byte *buffer, long size)
{
	if (size < (long)sizeof(infection_globals))
		return 0;
	csmemcpy(buffer, &infection_globals, sizeof(infection_globals));
	return sizeof(infection_globals);
}

boolean game_engine_infection_read_network_state(byte const *buffer, long size, boolean first)
{
	(void)first;
	if (size != (long)sizeof(infection_globals))
		return FALSE;
	csmemcpy(&infection_globals, buffer, sizeof(infection_globals));
	return TRUE;
}

long game_engine_infection_get_player_infections(long player_index)
{
	long slot;
	if (player_index == NONE)
		return 0;
	slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (slot < 0 || slot >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		return 0;
	return (long)infection_globals.infections[slot];
}

void game_engine_infection_get_status(
	long *survivor_count,
	long *infected_count,
	long *current_round,
	long *total_rounds,
	long *round_seconds_remaining)
{
	if (survivor_count)
		*survivor_count = infection_globals.survivor_count;
	if (infected_count)
		*infected_count = infection_globals.infected_count;
	if (current_round)
		*current_round = infection_globals.current_round > 0 ? infection_globals.current_round : 1;
	if (total_rounds)
		*total_rounds = infection_globals.total_rounds > 0 ? infection_globals.total_rounds : 1;
	if (round_seconds_remaining)
	{
		if (infection_globals.round_ticks_remaining >= 0)
			*round_seconds_remaining = (infection_globals.round_ticks_remaining + 29) / 30;
		else
			*round_seconds_remaining = -1;
	}
}
