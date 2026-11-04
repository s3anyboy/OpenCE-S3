/*
SABER_TOGGLE.H

The Back button switching the remastered models on and off in the game
(saber_toggle.c): each press draws the map's own models, or Halo: Combat
Evolved Anniversary's again.
*/

#ifndef __SABER_TOGGLE_H
#define __SABER_TOGGLE_H

/* ---------- prototypes/SABER_TOGGLE.C */

/* (mods.c, as a map's tags load) the map is drawn as its settings say */
void saber_toggle_initialize(void);
/* called each frame of a game (render_objects.c, as it draws): each press of
the first player's Back switches the models */
void saber_toggle_update(void);
/* (saber_models.c, saber_models_tags_loaded) whether the package's models are
made as a map loads: display.anniversary_models */
int saber_toggle_models_wanted(void);
/* (saber_models.c, render_model_parts) whether the map's own models are drawn
in the templates' place: Back, pressed, switches them */
int saber_toggle_classic_models(void);

#endif
