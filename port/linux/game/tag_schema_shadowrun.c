/*
TAG_SCHEMA_SHADOWRUN.C

The schemas (tag_schema.h) of the Shadowrun prototype's own groups that the
game reads (shadowrun_definitions.h): srud, srwd, m1ed, m2ed, m3ed, t2ed
and buym. Only its maps have tags of them. The rest of its groups (srac,
t1ed, t3ed) nothing reads yet; they are left as they are.
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_schema.h"
#include "shadowrun_definitions.h"

/* ---------- definitions */

/* a spell's level (numbers alone) */

static struct tag_schema_field const shadowrun_spell_level_fields[] =
{
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_spell_level_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_spell_level, struct shadowrun_spell_level, shadowrun_spell_level_fields);

/* m1ed */

static struct tag_schema_field const shadowrun_magic1_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_windup_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_windup_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_before_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_before_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_after_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, teleport_after_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic1_definition, teleport_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, gust_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, gust_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, gust_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, gust_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic1_definition, gust_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, gust_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_of_life_scenery, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_of_life_heal_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_of_life_heal_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_of_life_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic1_definition, tree_of_life_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic1_definition, tree_of_life_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_magic1_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_magic1, struct shadowrun_magic1_definition, shadowrun_magic1_fields);

/* m2ed */

static struct tag_schema_field const shadowrun_magic2_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_finish_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_finish_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic2_definition, resurrection_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_ball, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_doomed_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_corpse_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, resurrection_dead_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, strangle_scenery, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, strangle_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, strangle_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, strangle_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, strangle_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic2_definition, strangle_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_birth_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_birth_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic2_definition, summon_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_names, TAG_SCHEMA_GROUPS('ustr')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, summon_dying_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_scenery, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic2_definition, firewall_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_magic2_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_magic2, struct shadowrun_magic2_definition, shadowrun_magic2_fields);

/* m3ed */

static struct tag_schema_field const shadowrun_magic3_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic3_definition, spirit_animal_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_biped, TAG_SCHEMA_GROUPS('bipd')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_cast_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, spirit_animal_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, smoke_cast_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, smoke_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_magic3_definition, smoke_cast_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_BLOCK(struct shadowrun_magic3_definition, smoke_levels, shadowrun_spell_level_schema,
		MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_magic3_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_magic3, struct shadowrun_magic3_definition, shadowrun_magic3_fields);

/* t2ed */

static struct tag_schema_field const shadowrun_tech2_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, projectile, TAG_SCHEMA_GROUPS('proj')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, scenery, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, fire_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, hit_success_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, hit_success_effect_2, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, hit_bounce_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, hit_bounce_effect_2, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, travel_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, player_travel_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, player_attach_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, player_release_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, cable_release_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, cable_snap_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_tech2_definition, glider_scenery, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_tech2_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_tech2, struct shadowrun_tech2_definition, shadowrun_tech2_fields);

/* srud */

static struct tag_schema_field const shadowrun_race_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_race, head, TAG_SCHEMA_GROUPS('scen')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_race, regeneration_damage, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_race, regeneration_effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_race_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_race, struct shadowrun_race, shadowrun_race_fields);

static struct tag_schema_field const shadowrun_unit_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, magic1, TAG_SCHEMA_GROUPS(SHADOWRUN_MAGIC1_DEFINITION_TAG)),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, magic2, TAG_SCHEMA_GROUPS(SHADOWRUN_MAGIC2_DEFINITION_TAG)),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, magic3, TAG_SCHEMA_GROUPS(SHADOWRUN_MAGIC3_DEFINITION_TAG)),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, tech1, TAG_SCHEMA_GROUPS('t1ed')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, tech2, TAG_SCHEMA_GROUPS('t2ed')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, tech3, TAG_SCHEMA_GROUPS('t3ed')),
	TAG_SCHEMA_BLOCK(struct shadowrun_unit_definition, races, shadowrun_race_schema, MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, default_weapon_data, TAG_SCHEMA_GROUPS(SHADOWRUN_WEAPON_DATA_TAG)),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, buy_menu_bitmap, TAG_SCHEMA_GROUPS('bitm')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[0], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[1], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[2], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[3], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[4], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[5], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[6], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[7], TAG_SCHEMA_GROUPS('weap')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, shop_weapons[8], TAG_SCHEMA_GROUPS('weap')),
	/* port: the buym is a group with a schema, and so a parent group of the
	sruds that name it, which the validator gives every tag's (recomp's are
	corrected to it, else) */
	TAG_SCHEMA_REFERENCE(struct shadowrun_unit_definition, buy_menu, TAG_SCHEMA_GROUPS(SHADOWRUN_BUY_MENU_DEFINITION_TAG)),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_unit_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_unit, struct shadowrun_unit_definition, shadowrun_unit_fields);

/* srwd */

static struct tag_schema_field const shadowrun_weapon_data_race_fields[] =
{
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_weapon_data_race_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_weapon_data_race, struct shadowrun_weapon_data_race,
		shadowrun_weapon_data_race_fields);

static struct tag_schema_field const shadowrun_weapon_data_fields[] =
{
	TAG_SCHEMA_BLOCK(struct shadowrun_weapon_data, races, shadowrun_weapon_data_race_schema, MAXIMUM_SHADOWRUN_RACES),
	TAG_SCHEMA_REFERENCE(struct shadowrun_weapon_data, smartlinked, TAG_SCHEMA_GROUPS(SHADOWRUN_WEAPON_DATA_TAG)),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_weapon_data_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_weapon_data, struct shadowrun_weapon_data, shadowrun_weapon_data_fields);

/* buym */

static struct tag_schema_field const shadowrun_buy_menu_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, open_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, close_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, page_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, bought_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, learned_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, tech_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, cannot_afford_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, cursor_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, cannot_buy_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, tech_owned_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, known_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, ammunition_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, ammunition_failed_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_REFERENCE(struct shadowrun_buy_menu_definition, cast_failed_sound, TAG_SCHEMA_GROUPS('snd!')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const shadowrun_buy_menu_schema =
	TAG_SCHEMA_DEFINITION(shadowrun_buy_menu, struct shadowrun_buy_menu_definition, shadowrun_buy_menu_fields);

/* ---------- globals */

struct tag_schema_group const tag_schema_shadowrun_groups[] =
{
	/* (a buym is the parent group of the sruds that sell by it) */
	{ SHADOWRUN_UNIT_DEFINITION_TAG, { SHADOWRUN_BUY_MENU_DEFINITION_TAG, NONE }, &shadowrun_unit_schema },
	{ SHADOWRUN_MAGIC1_DEFINITION_TAG, { NONE, NONE }, &shadowrun_magic1_schema },
	{ SHADOWRUN_MAGIC2_DEFINITION_TAG, { NONE, NONE }, &shadowrun_magic2_schema },
	{ SHADOWRUN_MAGIC3_DEFINITION_TAG, { NONE, NONE }, &shadowrun_magic3_schema },
	{ SHADOWRUN_TECH2_DEFINITION_TAG, { NONE, NONE }, &shadowrun_tech2_schema },
	{ SHADOWRUN_WEAPON_DATA_TAG, { NONE, NONE }, &shadowrun_weapon_data_schema },
	{ SHADOWRUN_BUY_MENU_DEFINITION_TAG, { NONE, NONE }, &shadowrun_buy_menu_schema },
	{ 0 }
};
