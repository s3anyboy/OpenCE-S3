/*
CAMPAIGN_SCORE.H

The campaign's score (display.campaign_score: campaign_score.c): each
enemy a player kills is worth points by its actor variant.
*/

#ifndef __CAMPAIGN_SCORE_H
#define __CAMPAIGN_SCORE_H

/* display.campaign_score, and a campaign level being played */
boolean campaign_score_enabled(void);

/* (game_state.c's game_state_revert) a checkpoint was gone back to, from the
game time `from_time`: the level's time goes on from there */
void campaign_score_reverted(long from_time);

/* the level's time: the game time with the time that going back to
checkpoints took off it put back, so that it never goes back (hud.c's
campaign timer, and the time a kill's points fall with) */
long campaign_score_level_time(void);

/* (interface.c's hud_initialize_for_new_map) a map loads: a new level's
score begins; a level just won has its summary shown over this one's start */
void campaign_score_new_map(void);

/* (damage.c's object_damage_aftermath) a unit's body was depleted: if it
was an enemy, `killing_player_index` (NONE for no player) is given the
points of its actor variant */
void campaign_score_record_kill(
	long unit_index,
	long killing_player_index);

/* (main.c's main_won_map) the level is won: its summary is written to the
log and kept for the next level's start */
void campaign_score_level_won(void);

/* (main.c's main_won_map_private) a single player level was won and the
game is about to go back to the main menu: TRUE while the level's summary is
shown over it, until A or B is pressed; the level is left only when FALSE */
boolean campaign_score_postgame_wait(void);

/* (interface.c's interface_draw_fullscreen_overlays) the last level's summary */
void campaign_score_draw(void);

/* (hud.c's hud_draw_screen) a player's score, above the match clock at the
bottom right of the view being drawn */
void campaign_score_draw_view(long player_index);

/* (game_engine.c's co-op scoreboard) a player's own score (the points of
their kills, whatever network.coop_score_type), kills and deaths (each time
their unit went away), drawn beside their name; FALSE when the campaign is not
scored */
boolean campaign_score_player_stats(long player_index, long *score, long *kills, long *deaths);

/* (network_coop.c) network co-op: the host scores the kills and sends each
to the clients, and when the level is won */
void campaign_score_network_kill(short player_index, long variant_index, short points, boolean competitive,
	long game_time);
/* (network_coop.c's network_coop_reverted) a network co-op host went back to a
checkpoint saved at `restored_time`: the kills after it are forgotten, and its
clients are told; and on a client, as the host tells it */
void campaign_score_coop_reverted(long restored_time);
void campaign_score_network_reverted(long restored_time);
void campaign_score_network_won(boolean competitive);

#endif
