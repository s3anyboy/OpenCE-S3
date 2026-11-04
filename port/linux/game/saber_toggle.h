/*
SABER_TOGGLE.H

The Back button switching the remastered models on and off in the game
(saber_toggle.c): Halo: Combat Evolved Anniversary's models, and its level
geometry, over the map's while Back is held.
*/

#ifndef __SABER_TOGGLE_H
#define __SABER_TOGGLE_H

/* ---------- prototypes/SABER_TOGGLE.C */

/* called each frame of a game (port/linux/src/xinput_sdl.c): while the first
player holds the controller's Back, the package's models stay on, if they are
off; let go of, they go back off */
void saber_toggle_update(void);
/* (models.c, render_model_parts) whether the model drawn is not a template's:
while Back is held the map's own models are drawn in the templates' place */
int saber_toggle_classic_models(void);
/* (saber_models.c, saber_models_level_drawn) whether the level is drawn as
the package's while Back is held, though display.remastered_bsp is off */
int saber_toggle_level_wanted(void);

#endif
