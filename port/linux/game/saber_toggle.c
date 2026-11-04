/*
SABER_TOGGLE.C

The Back button switching the remastered models on and off in the game
(saber_toggle.h). Each press of it draws the map's own models, instead of
Halo: Combat Evolved Anniversary's, or them again. The models' tags are
written as the press says: each template's geometries are written into the
map's model and taken out again (saber.h's saber_models_anniversary_set), so
that everything that draws the model reads either the package's geometry or
the map's own, its shaders, regions and nodes as the map's. The level's
structure and the settings are as they are (display.remastered_bsp still
draws the remastered geometry). The package's models are made as the map
loads when display.anniversary_models asks for them, so the switch only says
which are drawn.

The press is read as the game draws (render_objects.c).
*/

/* ---------- headers */

#include "saber_toggle.h"

#include "cseries/cseries.h"
#include "input/input.h"
#include "halo_keyboard.h"

#include <xtl.h>

/* ---------- the models (port/linux/game/saber_models.c) */

long saber_models_anniversary_set(int shown);
long saber_models_geometry_count(void);

/* ---------- the platform layer's (port/linux/src/port_config.c) */

int config_boolean(const char *name);
void platform_log(const char *format, ...);

/* ---------- globals */

static struct
{
	/* Back was held when the game was last looked at */
	boolean held;
	/* the map's own models are drawn, instead of the package's */
	boolean classic;
} saber_toggle;

/* ---------- private code */

/* whether Back is held: the first controller's (the keyboard's F1 presses
it and Back, port/linux/src/xinput_sdl.c), or the keyboard's scoreboard key */
static boolean saber_toggle_held(
	void)
{
	struct gamepad_state const *pad = input_get_gamepad_state(0);

	if (pad && pad->buttons[_gamepad_binary_button_back] > 0)
		return TRUE;

	return (halo_keyboard_actions(0) & (1UL << HALO_KEYBOARD_SCOREBOARD)) != 0;
}

/* ---------- public code */

void saber_toggle_initialize(
	void)
{
	csmemset(&saber_toggle, 0, sizeof(saber_toggle));
}

void saber_toggle_update(
	void)
{
	boolean held = saber_toggle_held();

	/* (a press: Back, let go of, is pressed again) */
	if (!held || saber_toggle.held)
	{
		saber_toggle.held = held;
		return;
	}
	saber_toggle.held = held;
	saber_toggle.classic = saber_toggle.classic ? FALSE : TRUE;
	{
		/* (the tags written over, before the next frame reads them) */
		long models = saber_models_anniversary_set(saber_toggle.classic ? FALSE : TRUE);
		long geometries = saber_models_geometry_count();

		platform_log("anniversary: Back pressed: the models are drawn as %s: %ld models of the map's %ld geometries",
			saber_toggle.classic ? "the map's" : "templates", models, geometries);
	}
}

int saber_toggle_models_wanted(
	void)
{
	return config_boolean("display.anniversary_models");
}

int saber_toggle_classic_models(
	void)
{
	return saber_toggle.classic;
}
