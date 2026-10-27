/*
CAMERA_THIRD_PERSON.C

The camera held in third person while a melee weapon is in hand
(camera/director.c's director_desired_perspective). Halo's only third-person
camera is a vehicle seat's: off a seat a unit is always first person, whatever
weapon it holds, so a melee weapon such as Shadowrun's katana swings unseen.
With game.third_person_melee set ("auto" by default: on for a Shadowrun
prototype map, as the stock game has it elsewhere) a unit whose held weapon
has no magazines, which is how the port already tells a melee weapon from a
gun (gun_game.c), is followed by the third-person camera.

Only the local player's own view follows the rule: the deterministic camera
the netcode aims projectiles from (director_camera_deterministic) stays first
person, so that every machine computes the same shots.
*/

#include "cseries.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "main/main.h"
#include "objects/objects.h"
#include "units/units.h"
#include "custom_edition_maps.h" /* port: port/linux/game/custom_edition_maps.c */
#include "camera_third_person.h"

/* port_config.c's */
unsigned long config_changes(void);
char const *config_string(char const *name);

/* the setting, read again when the settings change: "auto" (the default) is
third person for a Shadowrun prototype map's weapons, first person as the
stock game has it elsewhere, and "true"/"false" hold either everywhere */
static boolean camera_third_person_enabled(
	void)
{
	static boolean enabled = FALSE;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		char const *value;

		read_at = config_changes();
		value = config_string("game.third_person_melee");
		if (value && (!csstrcasecmp(value, "true") || !csstrcasecmp(value, "on")))
			enabled = TRUE;
		else if (value && (!csstrcasecmp(value, "false") || !csstrcasecmp(value, "off")))
			enabled = FALSE;
		else
			enabled = shadowrun_level_name(main_get_map_name()); /* "auto" */
	}
	return enabled;
}

/* whether the unit's held weapon is a melee one: one with no magazines, as a
katana or fists */
static boolean camera_third_person_melee_weapon(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long weapon_index;

	if (!unit)
		return FALSE;
	weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
	if (weapon_index == NONE)
		return FALSE;
	return weapon_definition_get(object_get(weapon_index)->definition_index)->weapon.magazines.count <= 0;
}

boolean camera_third_person_wanted(
	long unit_index)
{
	return camera_third_person_enabled() && camera_third_person_melee_weapon(unit_index);
}
