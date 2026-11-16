/*
WARZONE.C

Warzone, a mode of team King of the Hill (game_modes.c). Three hills are in
play at once (King's Multi-Hill, in place), and the teams spawn as CTF's
do, each at its own base (FORCE TEAM SPAWNS). A team scores:

- a point a second for each hill its players alone are on (one both teams
  are on is contested, and scores for nobody);
- for a kill of an enemy: 25 for a player; for the AI by its kind, a grunt
  5, a jackal 10, a Flood combat form 10, an elite 15, a hunter 25, an elite
  commander 100, any other 5; and more for a vehicle's driver killed at its
  controls, who is worth his own as well: 25 a Ghost's or a Banshee's, 50 a
  Wraith's. A vehicle destroyed scores its enemy riders, who die with it
  (as acts of god, which no kill reports), each their own, its driver the
  vehicle's as well.

The first team to the points to win (a hundred for each of the gametype's
score to win, which King counts in minutes: 10, 1000 points) wins, as does
the team ahead when the time runs out.

The points are King's team scores, in King's units (a tick a tick on the
hill: TICKS_PER_SECOND a point), so the host sends them to its clients as it
does King's (game_engine_king_write_network_state). A player's own points,
their time on the hills and their kills', are their King statistics' time
on the hill, which the host sends with their other statistics.

Kinds are told by the tags' types, never by their names: the AI's by its
actor's type, a vehicle's by its own (a Banshee is the alien fighter; the
Ghost and the Wraith are both alien scouts, the Ghost's guns fixed forward,
its AI weapon cannot rotate, and the Wraith's mortar turning). An elite
commander is an Elite whose actor variant
cannot use ranged weapons (a sword's), or whose shields are the strongest of
the map's Elite variants and at least twice the weakest's (b30's: the
commander's 300, the major's 150 and the minor's 100).

Only the host scores; its clients show what it sends.
*/

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "ai/actor_types.h"
#include "ai/actors.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_king.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "objects/objects.h"
#include "text/unicode.h"
#include "units/units.h"
#include "game_modes.h"
#include "warzone.h"

/* network_game_globals.c's */
boolean network_game_distributed_client(void);

/* the vehicle tag's flags and type, as vehicles.c and actor_combat.c read
them (its layout is not the public header's) */
struct warzone_vehicle_definition
{
	byte __unknown0[0x2F0];
	unsigned long flags;
	short vehicle_type;
};

typedef char warzone_vehicle_type_offset_assert[
	offsetof(struct warzone_vehicle_definition, vehicle_type) == 0x2F4 ? 1 : -1];
typedef char warzone_vehicle_flags_offset_assert[
	offsetof(struct warzone_vehicle_definition, flags) == 0x2F0 ? 1 : -1];

/* (vehicles.c's vehicle types: a Ghost's and a Wraith's, a Banshee's; and
actor_combat.c's flag of the Ghost's fixed guns) */
enum
{
	_vehicle_type_alien_scout = 4,
	_vehicle_type_alien_fighter = 5,
	_vehicle_ai_weapon_cannot_rotate_bit = 8
};

/* King's messages (game_engine_king.h's king_message) */
enum
{
	WARZONE_MESSAGE_FIRST = king_message_enemy_on_the_hill,
	WARZONE_MESSAGE_LAST = king_message_you_are_on_the_hill
};

static struct
{
	struct game_engine engine;
	struct game_engine king;
	/* the map's elite commanders: an Elite variant's shields this strong
	or more (0: none is) */
	real commander_shields;
} warzone;

/* ---- the points */

/* the points to win, in King's units */
static long warzone_score_to_win(
	void)
{
	return game_engine_get_variant()->universal_variant.score_to_win *
		WARZONE_POINTS_PER_SCORE_TO_WIN * TICKS_PER_SECOND;
}

/* whether an actor variant is an elite commander's */
static boolean warzone_variant_is_commander(
	struct actor_variant_definition const *variant)
{
	if (TEST_FLAG(variant->flags, _actor_variant_definition_cannot_use_ranged_weapons_bit))
		return TRUE;
	return warzone.commander_shields > 0.f && variant->unit.maximum_shield_vitality >= warzone.commander_shields;
}

/* the AI's points, by its actor's kind (0: no actor's) */
static long warzone_actor_points(
	long actor_index)
{
	struct actor_datum *actor = actor_index == NONE ? NULL : actor_try_and_get(actor_index);

	if (!actor)
		return 0;
	switch (actor->meta.type)
	{
	case _actor_grunt:
		return WARZONE_POINTS_GRUNT;
	case _actor_jackal:
		return WARZONE_POINTS_JACKAL;
	case _actor_hunter:
		return WARZONE_POINTS_HUNTER;
	case _actor_combat_form:
		return WARZONE_POINTS_FLOOD_COMBAT_FORM;
	case _actor_elite:
		if (actor->meta.variant_definition_index != NONE &&
			warzone_variant_is_commander(actor_variant_definition_get(actor->meta.variant_definition_index)))
		{
			return WARZONE_POINTS_ELITE_COMMANDER;
		}
		return WARZONE_POINTS_ELITE;
	}
	return WARZONE_POINTS_OTHER_AI;
}

long warzone_unit_points(
	long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	struct unit_datum *vehicle;
	long points;

	if (!unit)
		return 0;
	/* (a vehicle's own death is scored as its riders': warzone_unit_killed) */
	if (unit->object.type != _object_type_biped)
		return 0;
	if (player_index_from_unit_index(unit_index) != NONE)
		points = WARZONE_POINTS_PLAYER;
	else
		points = warzone_actor_points(unit->unit.actor_index);
	if (!points)
		return 0;
	/* (at a Ghost's, a Banshee's or a Wraith's controls) */
	vehicle = unit->object.parent_object_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(unit->object.parent_object_index, _object_mask_vehicle);
	if (vehicle && vehicle->unit.driver_object_index == unit_index)
	{
		struct warzone_vehicle_definition const *definition =
			(struct warzone_vehicle_definition *)tag_get('vehi', vehicle->definition_index);

		if (definition->vehicle_type == _vehicle_type_alien_fighter)
			points += WARZONE_POINTS_VEHICLE;
		else if (definition->vehicle_type == _vehicle_type_alien_scout)
		{
			points += TEST_FLAG(definition->flags, _vehicle_ai_weapon_cannot_rotate_bit) ?
				WARZONE_POINTS_VEHICLE : WARZONE_POINTS_WRAITH;
		}
	}
	return points;
}

/* a team's points, and its player's, scored (the host's) */
static void warzone_score(
	long team_index,
	long player_index,
	long points)
{
	struct player_datum *player = player_index == NONE ? NULL : player_try_and_get(player_index);

	game_engine_king_add_score(team_index, points * TICKS_PER_SECOND);
	if (player)
		player->statistics.multiplayer_statistics.king_statistics.time_on_hill += points * TICKS_PER_SECOND;
	if (game_engine_king_team_score(team_index) >= warzone_score_to_win())
		game_engine_end_game();
}

/* whether a unit killed was the killer's enemy: a player of the other
team, or the AI against the killer's */
static boolean warzone_unit_is_enemy(
	long unit_index,
	struct unit_datum const *unit,
	struct player_datum const *killer,
	long killing_player_index,
	short killing_team_index)
{
	long dead_player_index = player_index_from_unit_index(unit_index);

	if (dead_player_index != NONE)
	{
		struct player_datum *dead = player_try_and_get(dead_player_index);

		return dead && dead_player_index != killing_player_index && dead->team_index != killer->team_index;
	}
	return game_team_is_enemy(killing_team_index, unit->object.owner_team_index);
}

/* the riders a vehicle's death scored (who die with it after, as acts of
god: damage.c's unit_kill), so that a death of their own then is not
scored again */
static struct
{
	long unit_index;
	long time;
} warzone_scored_riders[16];
static long warzone_scored_rider_next;

static boolean warzone_rider_scored(
	long unit_index)
{
	long index;

	for (index = 0; index < NUMBEROF(warzone_scored_riders); index++)
	{
		if (warzone_scored_riders[index].unit_index == unit_index &&
			game_time_get() - warzone_scored_riders[index].time <= 2 * TICKS_PER_SECOND)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void warzone_unit_killed(
	long unit_index,
	long killing_player_index,
	short killing_team_index)
{
	struct player_datum *killer;
	struct unit_datum *unit;
	long points = 0;

	if (game_modes_mode() != _game_mode_warzone || network_game_distributed_client() || !game_engine_can_score())
		return;
	killer = killing_player_index == NONE ? NULL : player_try_and_get(killing_player_index);
	unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	if (!killer || !unit)
		return;
	if (unit->object.type == _object_type_vehicle)
	{
		/* (a vehicle destroyed: its riders die with it, as acts of god
		with no killer, so its killer scores them now, each its own and
		its driver the vehicle's as well: warzone_unit_points) */
		long rider_index = unit->object.first_child_object_index;

		while (rider_index != NONE)
		{
			struct unit_datum *rider = (struct unit_datum *)object_get(rider_index);

			if (rider->object.type == _object_type_biped && rider->unit.parent_seat_index != NONE &&
				!TEST_FLAG(rider->object.damage_flags, _object_dead_bit) &&
				warzone_unit_is_enemy(rider_index, rider, killer, killing_player_index, killing_team_index))
			{
				long rider_points = warzone_unit_points(rider_index);

				if (rider_points)
				{
					points += rider_points;
					warzone_scored_riders[warzone_scored_rider_next].unit_index = rider_index;
					warzone_scored_riders[warzone_scored_rider_next].time = game_time_get();
					warzone_scored_rider_next = (warzone_scored_rider_next + 1) % NUMBEROF(warzone_scored_riders);
				}
			}
			rider_index = rider->object.next_object_index;
		}
	}
	else if (!warzone_rider_scored(unit_index) &&
		warzone_unit_is_enemy(unit_index, unit, killer, killing_player_index, killing_team_index))
	{
		points = warzone_unit_points(unit_index);
	}
	if (points)
		warzone_score(killer->team_index, killing_player_index, points);
}

/* ---- the engine */

/* the map's elite commanders: the strongest shields of its Elite variants,
if at least twice the weakest's */
static void warzone_find_commanders(
	void)
{
	struct tag_iterator iterator;
	long tag_index;
	real weakest = 0.f;
	real strongest = 0.f;

	warzone.commander_shields = 0.f;
	tag_iterator_new(&iterator, ACTOR_VARIANT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct actor_variant_definition const *variant = actor_variant_definition_get(tag_index);
		real shields = variant->unit.maximum_shield_vitality;

		if (variant->actor_reference.index == NONE ||
			actor_definition_get(variant->actor_reference.index)->type != _actor_elite || shields <= 0.f)
		{
			continue;
		}
		if (weakest <= 0.f || shields < weakest)
			weakest = shields;
		if (shields > strongest)
			strongest = shields;
	}
	if (weakest > 0.f && strongest >= 2.f * weakest)
		warzone.commander_shields = strongest;
}

static boolean warzone_initialize_for_new_map(
	void)
{
	warzone_find_commanders();
	return warzone.king.initialize_for_new_map ? warzone.king.initialize_for_new_map() : TRUE;
}

static void warzone_update(
	void)
{
	short hill_index;

	warzone.king.update();
	if (network_game_distributed_client() || !game_engine_can_score())
		return;
	/* (each hill a team alone is on, its point a second: one of King's
	units a tick) */
	for (hill_index = 0; hill_index < game_engine_king_hill_count(); hill_index++)
	{
		long team_index = game_engine_king_hill_holder(hill_index);

		if (team_index != NONE)
		{
			game_engine_king_add_score(team_index, WARZONE_POINTS_HILL);
			if (game_engine_king_team_score(team_index) >= warzone_score_to_win())
				game_engine_end_game();
		}
	}
}

static wchar_t *warzone_format_player_score(
	long player_index,
	wchar_t *string)
{
	struct player_datum *player = player_get(player_index);

	usnprintf(string, 8, L"%ld",
		player->statistics.multiplayer_statistics.king_statistics.time_on_hill / TICKS_PER_SECOND);
	return string;
}

static wchar_t *warzone_format_score_name(
	wchar_t *string)
{
	ustrncpy(string, L"Points", 8);
	return string;
}

static wchar_t *warzone_format_team_score(
	long team_index,
	wchar_t *string)
{
	usnprintf(string, 8, L"%ld", game_engine_king_team_score(team_index) / TICKS_PER_SECOND);
	return string;
}

static boolean warzone_format_message(
	long player_index,
	long message,
	long message_player_index,
	wchar_t *buffer,
	long buffer_size)
{
	struct player_datum *other;
	long points;

	if (message < WARZONE_MESSAGE_FIRST || message > WARZONE_MESSAGE_LAST)
		return warzone.king.format_message ?
			warzone.king.format_message(player_index, message, message_player_index, buffer, buffer_size) : FALSE;
	other = player_try_and_get(message_player_index);
	if (!other)
		return FALSE;
	points = game_engine_king_team_score(other->team_index) / TICKS_PER_SECOND;
	switch (message)
	{
	case king_message_you_are_on_the_hill:
		usnprintf(buffer, buffer_size, L"On a hill - %ld points", points);
		break;
	case king_message_ally_on_the_hill:
		usnprintf(buffer, buffer_size, L"%s is on a hill - %ld points", other->name, points);
		break;
	default:
		usnprintf(buffer, buffer_size, L"%s is on a hill - %ld points", other->name, points);
		break;
	}
	return TRUE;
}

struct game_engine *warzone_engine_get(
	struct game_engine const *king)
{
	warzone.king = *king;
	warzone.engine = *king;
	warzone.engine.initialize_for_new_map = warzone_initialize_for_new_map;
	warzone.engine.update = warzone_update;
	warzone.engine.format_player_score = warzone_format_player_score;
	warzone.engine.format_score_name = warzone_format_score_name;
	warzone.engine.format_team_name = warzone_format_team_score;
	warzone.engine.format_message = warzone_format_message;
	return &warzone.engine;
}
