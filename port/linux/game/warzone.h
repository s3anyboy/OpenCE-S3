/* warzone.h: Warzone, a mode of team King of the Hill (port/linux/game/warzone.c) */
#ifndef HALO_WARZONE_H
#define HALO_WARZONE_H

struct game_engine;

/* the points a kill of each kind is worth */
enum
{
	WARZONE_POINTS_PLAYER = 25,
	WARZONE_POINTS_GRUNT = 5,
	WARZONE_POINTS_JACKAL = 10,
	WARZONE_POINTS_ELITE = 15,
	WARZONE_POINTS_HUNTER = 25,
	WARZONE_POINTS_ELITE_COMMANDER = 50,
	/* the AI of any other kind (a marine, the Flood, a sentinel) */
	WARZONE_POINTS_OTHER_AI = 5,
	/* a Ghost's or a Banshee's driver killed at its controls: more */
	WARZONE_POINTS_VEHICLE = 25,
	/* a hill a team alone is on: a second's */
	WARZONE_POINTS_HILL = 1,
	/* the points to win, a hundred for each of the gametype's score to win
	(King's minutes: 1, 2, 5, 10, 15) */
	WARZONE_POINTS_PER_SCORE_TO_WIN = 100,
	/* the hills in play at once (King's Multi-Hill) */
	WARZONE_HILLS = 3
};

/* King's engine with Warzone's rules (game_modes_engine) */
struct game_engine *warzone_engine_get(struct game_engine const *king);
/* a unit killed (damage.c, by game_modes_unit_killed): the killer's team
scores it, if it was their enemy (the host's) */
void warzone_unit_killed(long unit_index, long killing_player_index, short killing_team_index);
/* the points a unit's kill is worth, its driving included (0: none) */
long warzone_unit_points(long unit_index);

#endif
