/*
SHADOWRUN_BUY_MENU.C

The Shadowrun prototype's buy menu, as its executable (cachesym.xbe) has it
for its multiplayer games: a list of pages (weapons, tech, magic, a gift to
a teammate) whose items a player buys with its money. Its prices, the
weapons it sells and its sounds are the player's srud's and its buy menu's
(buym). A player's money is given it as it first spawns.

The executable opened it at the start of its own multiplayer games' rounds,
and in a campaign showed the globals' loadouts instead, with nothing to open
it. port: here the Back control opens and closes it on the prototype's maps,
and it is the multiplayer one wherever it is opened. A player's srud gives
it its prices, its weapons and its sounds; where the map gives the player
none (recomp.map: docs/shadowrun_caches.md), what it sells is the stock
menu's, the first srud a map of the prototype's build has, or the
prototype's prices where the map has none at all, and its buym's sounds the
first the map has. While it is open the
movement stick (or the d-pad) moves through it, the jump control buys, the melee control
goes back a page (or closes it), and the action control buys a magazine of
the weapon in hand; the player does nothing else. What is bought is the
buying machine's: a co-op client's purchases are not sent to the host.

Tech is not played, so its items cannot be bought (nor the anti-magic
grenade, which this build has no grenade type for).
*/

/* ---------- headers */

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "main/console.h" /* port: the buy menu's report of why it opens */
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/interface.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"

#include "render/render.h"
#include "sound/game_sound.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the menu's items, its pages, and the most it lists of a page (29 with
	port: gecko, the executable's menu does not list) */
	NUMBER_OF_SHADOWRUN_SHOP_ITEMS = 30,
	_shadowrun_shop_page_main = 0,
	_shadowrun_shop_page_tech,
	_shadowrun_shop_page_magic,
	_shadowrun_shop_page_weapons,
	_shadowrun_shop_page_gift,
	_shadowrun_shop_page_empty,
	MAXIMUM_SHADOWRUN_SHOP_LIST = 20,
	SHADOWRUN_SHOP_ROWS = 5,

	/* the items' actions (the executable's) */
	_shadowrun_shop_done = 0,
	_shadowrun_shop_open_tech = 2,
	_shadowrun_shop_open_magic = 3,
	_shadowrun_shop_open_weapons = 4,
	_shadowrun_shop_open_main = 5,
	_shadowrun_shop_open_gift = 6,
	_shadowrun_shop_open_empty = 7,
	_shadowrun_shop_grenade = 8,
	_shadowrun_shop_anti_magic_grenade = 9,
	/* port: the grenade type the menu's plain Grenade is (units.h's first) */
	_shadowrun_shop_grenade_type = 0,
	_shadowrun_shop_first_magic = 10,
	_shadowrun_shop_last_magic = 16,
	_shadowrun_shop_razorclaws = 17,
	_shadowrun_shop_katana = 18,
	_shadowrun_shop_first_tech = 19,
	_shadowrun_shop_last_tech = 27,
	_shadowrun_shop_first_weapon = 28,
	_shadowrun_shop_last_weapon = 34,

	/* what a gift to a teammate costs, how long a player waits between
	purchases (ticks), how many grenades of a kind it may buy up to, and
	the money a player is given (the executable's player_new gives every
	player this, over its globals' starting money) */
	SHADOWRUN_SHOP_GIFT = 250,
	SHADOWRUN_SHOP_PURCHASE_WAIT = 30,
	SHADOWRUN_SHOP_MAXIMUM_GRENADES = 2,
	SHADOWRUN_STARTING_MONEY = 5000000,
	/* the selection blinks so often (ticks) */
	SHADOWRUN_SHOP_BLINK_TICKS = 15,

	/* draw_string.c's justifications (its enum is its own) */
	_shadowrun_shop_text_left = 0,
	_shadowrun_shop_text_right,
	_shadowrun_shop_text_center,

	/* the price of none */
	NO_SHADOWRUN_PRICE = -1,
};

/* the stick past this moves the selection, and under this is centred */
#define SHADOWRUN_SHOP_STICK_MOVE 0.5f
#define SHADOWRUN_SHOP_STICK_CENTERED 0.25f

/* port: the prices where the map gives the player no srud of its own
(recomp.map, whose `characters\sr_player\sr_player` names none): its
`shadowrunstuff\generic_unit` srud's, which is the one the prototype's
multiplayer players bought by (read from recomp.map; 99999 is that srud's
'never', a price past any money a player has) */
static long const shadowrun_shop_stock_prices[NUMBER_OF_SHADOWRUN_PRICES] =
{
	2250,   /* 0:  (the srud's own, unused) */
	2000,   /* 1:  Teleport */
	9999,   /* 2:  Tree of Life */
	2000,   /* 3:  Summon Elemental */
	99999,  /* 4:  Strangle */
	2000,   /* 5:  Razorclaws */
	2000,   /* 6:  Enhanced Vision */
	99999,  /* 7:  Glider */
	300,    /* 8 */
	200,    /* 9:  Grenade */
	500,    /* 10: Flechette Pistol */
	2500,   /* 11: Assault Rifle */
	1250,   /* 12: Sniper Rifle */
	3250,   /* 13: Mini Gun */
	750,    /* 14: Rocket Launcher */
	2000,   /* 15: Shotgun */
	2000,   /* 16: Gust */
	50,     /* 17: Smoke */
	100,    /* 18: a Flechette Pistol's magazine */
	200,    /* 19: an Assault Rifle's magazine */
	200,    /* 20: a Sniper Rifle's magazine */
	1000,   /* 21: a Mini Gun's magazine */
	200,    /* 22: a Rocket Launcher's magazine */
	0,      /* 23: a Shotgun's magazine (not sold) */
	1000,   /* 24: a Rifle's magazine */
	1000,   /* 25: Enhanced Vision's removal */
	99999,  /* 26: Glider's removal */
	99999,  /* 27 */
	99999,  /* 28: Anti-Magic Grenade */
	99999,  /* 29 */
	2000,   /* 30: Hologram */
	1000,   /* 31 */
	1000,   /* 32 */
	1000,   /* 33: Smartlink, Gecko, Reflexes */
	1000,   /* 34 */
	1000,   /* 35: Hologram's removal */
	2000,   /* 36 */
	1250,   /* 37 */
	750,    /* 38: those removals' */
	50,     /* 39: Resurrection */
	99999,  /* 40: Katana */
	99999,  /* 41: Rifle */
	99999,  /* 42: a Rifle's magazine */
	99999,  /* 43 */
	1000,   /* 44 */
	0,      /* 45 */
	0,      /* 46: Grapple */
	0,      /* 47: Grapple's removal */
};

/* the sounds' indices, in the buy menu's order */
enum
{
	_shadowrun_shop_sound_open = 0,
	_shadowrun_shop_sound_close,
	_shadowrun_shop_sound_page,
	_shadowrun_shop_sound_bought,
	_shadowrun_shop_sound_learned,
	_shadowrun_shop_sound_tech,
	_shadowrun_shop_sound_cannot_afford,
	_shadowrun_shop_sound_cursor,
	_shadowrun_shop_sound_cannot_buy,
	_shadowrun_shop_sound_tech_owned,
	_shadowrun_shop_sound_known,
	_shadowrun_shop_sound_magazine,
	_shadowrun_shop_sound_magazine_failed,
};

/* ---------- structures */

/* an item of the menu (the executable's table at 0x485E4C), and its price's
and its removal's (tech's) in the srud's prices */
struct shadowrun_shop_item
{
	short action;
	short page;
	char const *name;
	short price_index;
	short removal_price_index;
};

/* a local player's menu (the executable's, at 0x793F44) */
struct shadowrun_shop_state
{
	boolean open;
	/* giving to a teammate (0x793F4C) */
	boolean gift;
	/* the stick must come back to the middle before it moves again
	(0x793F65, 0x793F67) */
	boolean stick_up_held;
	boolean stick_down_held;
	/* the page (0x793F48) and the item chosen (its index in the items, or
	the teammate's in the list: 0x793F50) */
	short page;
	short selection;
	/* the selection's blink (0x793F68, 0x793F6C), and the last purchase
	(0x793F78) */
	boolean blink;
	byte pad[3];
	long blink_time;
	long purchase_time;
};

/* ---------- globals */

static struct shadowrun_shop_item const shadowrun_shop_items[NUMBER_OF_SHADOWRUN_SHOP_ITEMS] =
{
	{ _shadowrun_shop_open_weapons, _shadowrun_shop_page_main, "Weapon", NONE, NONE },
	{ _shadowrun_shop_open_tech, _shadowrun_shop_page_main, "Tech", NONE, NONE },
	{ _shadowrun_shop_open_magic, _shadowrun_shop_page_main, "Magic", NONE, NONE },
	{ _shadowrun_shop_open_gift, _shadowrun_shop_page_main, "Gift", NONE, NONE },
	{ _shadowrun_shop_done, _shadowrun_shop_page_main, "Done", NONE, NONE },
	/* port: a spell the player has may be removed for what it cost (the
	executable's menu only removed tech; missionlet's prices are empty,
	which shadowrun_shop_price reads as 0) */
	{ 14, _shadowrun_shop_page_magic, "Smoke", 17, 17 },
	{ 15, _shadowrun_shop_page_magic, "Gust", 16, 16 },
	{ 16, _shadowrun_shop_page_magic, "Resurrection", 39, 39 },
	{ 13, _shadowrun_shop_page_magic, "Strangle", 4, 4 },
	{ 10, _shadowrun_shop_page_magic, "Teleport", 1, 1 },
	{ 11, _shadowrun_shop_page_magic, "Tree of Life", 2, 2 },
	{ 12, _shadowrun_shop_page_magic, "Summon Elemental", 3, 3 },
	{ 28, _shadowrun_shop_page_weapons, "Assault Rifle", 11, NONE },
	{ 32, _shadowrun_shop_page_weapons, "Flechette Pistol", 10, NONE },
	{ _shadowrun_shop_razorclaws, _shadowrun_shop_page_weapons, "Razorclaws", 5, NONE },
	{ _shadowrun_shop_katana, _shadowrun_shop_page_weapons, "Katana", 40, NONE },
	{ 34, _shadowrun_shop_page_weapons, "Rifle", 41, NONE },
	{ 33, _shadowrun_shop_page_weapons, "Shotgun", 15, NONE },
	{ 30, _shadowrun_shop_page_weapons, "Mini Gun", 13, NONE },
	{ 29, _shadowrun_shop_page_weapons, "Sniper Rifle", 12, NONE },
	{ 31, _shadowrun_shop_page_weapons, "Rocket Launcher", 14, NONE },
	{ _shadowrun_shop_grenade, _shadowrun_shop_page_weapons, "Grenade", 9, NONE },
	{ _shadowrun_shop_anti_magic_grenade, _shadowrun_shop_page_weapons, "Anti-Magic Grenade", 28, NONE },
	{ 22, _shadowrun_shop_page_tech, "Grapple", 46, 47 },
	{ 20, _shadowrun_shop_page_tech, "Glider", 7, 26 },
	{ 24, _shadowrun_shop_page_tech, "Hologram", 30, 35 },
	{ 27, _shadowrun_shop_page_tech, "Smartlink", 33, 38 },
	{ 19, _shadowrun_shop_page_tech, "Enhanced Vision", 6, 25 },
	/* port: two the executable's menu does not list, though its shop gives
	them (gecko, and reflexes, which its scripts number 8) */
	{ 21, _shadowrun_shop_page_tech, "Gecko", 33, 38 },
	{ 23, _shadowrun_shop_page_tech, "Reflexes", 33, 38 },
};

/* the spell each magic item teaches (by action, from the first magic's) */
static short const shadowrun_shop_spells[_shadowrun_shop_last_magic - _shadowrun_shop_first_magic + 1] =
{
	_shadowrun_spell_teleport,
	_shadowrun_spell_tree_of_life,
	_shadowrun_spell_summon_elemental,
	_shadowrun_spell_strangle,
	_shadowrun_spell_smoke,
	_shadowrun_spell_gust,
	_shadowrun_spell_resurrection,
};

/* the tech each item sells, by its action (19 to 27): the executable's ids
(units.c, FUN_0027d4e0), and its items' places. The port's items: Enhanced
Vision 19, Glider 20, Grapple 22, Hologram 24, Smartlink 27. A tech the port
does not play is NONE, and is not sold */
static short const shadowrun_shop_techs[_shadowrun_shop_last_tech - _shadowrun_shop_first_tech + 1] =
{
	_shadowrun_tech_vision_id, /* 19: Enhanced Vision */
	_shadowrun_tech_glider_id, /* 20: Glider */
	_shadowrun_tech_gecko_id, /* 21: Gecko */
	_shadowrun_tech_grapple_id, /* 22: Grapple */
	_shadowrun_tech_reflexes_id, /* 23: Reflexes */
	_shadowrun_tech_hologram_id, /* 24: Hologram */
	NONE, /* 25 */
	NONE, /* 26 */
	_shadowrun_tech_smartlink_id, /* 27: Smartlink */
};

/* the weapon each weapon item sells (by action, from the first weapon's) */
static short const shadowrun_shop_weapons[_shadowrun_shop_last_weapon - _shadowrun_shop_first_weapon + 1] =
{
	_shadowrun_shop_weapon_assault_rifle,
	_shadowrun_shop_weapon_sniper_rifle,
	_shadowrun_shop_weapon_minigun,
	_shadowrun_shop_weapon_rocket_launcher,
	_shadowrun_shop_weapon_flechette_pistol,
	_shadowrun_shop_weapon_shotgun,
	_shadowrun_shop_weapon_rifle,
};

/* a magazine's price of each weapon the menu sells (its index in the srud's
prices; NONE, none sold) */
static short const shadowrun_shop_magazine_prices[NUMBER_OF_SHADOWRUN_SHOP_WEAPONS] =
{
	19, 20, 21, 22, 18, 23, NONE, NONE, 42,
};

static struct shadowrun_shop_state shadowrun_shop_states[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

/* ---------- private prototypes */

static struct player_datum *shadowrun_shop_player(
	short local_player_index);
static struct shadowrun_unit_definition *shadowrun_shop_unit_definition(
	short local_player_index);
static struct shadowrun_buy_menu_definition *shadowrun_shop_buy_menu(
	short local_player_index);
static void shadowrun_shop_sound(
	short local_player_index,
	short sound);
static long shadowrun_shop_price(
	short local_player_index,
	struct shadowrun_unit_definition *definition,
	short price_index);
static long shadowrun_shop_weapon_definition(
	short local_player_index,
	short weapon);
static void shadowrun_shop_open(
	short local_player_index);
static void shadowrun_shop_close(
	short local_player_index);
static boolean shadowrun_shop_back(
	short local_player_index);
static void shadowrun_shop_set_page(
	short local_player_index,
	short page);
static void shadowrun_shop_move(
	short local_player_index,
	real forward,
	real strafe);
static void shadowrun_shop_buy(
	short local_player_index);
static void shadowrun_shop_buy_magazine(
	short local_player_index);
static short shadowrun_shop_list(
	short local_player_index,
	long *list);
static boolean shadowrun_shop_owned(
	short local_player_index,
	long unit_index,
	struct shadowrun_unit_definition *definition,
	short item);
static boolean shadowrun_shop_available(
	short local_player_index,
	long unit_index,
	struct shadowrun_unit_definition *definition,
	short item);
static short shadowrun_shop_tech_id(
	short item);
static boolean shadowrun_unit_has_weapon(
	long unit_index,
	long weapon_definition_index);
static void shadowrun_unit_give_weapon(
	long unit_index,
	long weapon_definition_index);

/* ---------- public code */

void shadowrun_buy_menu_initialize_for_new_map(
	void)
{
	csmemset(shadowrun_shop_states, 0, sizeof(shadowrun_shop_states));

	return;
}

/* a player's money, as it first spawns */
void shadowrun_buy_menu_player_spawned(
	long player_index)
{
	struct shadowrun_player *shadowrun_player = shadowrun_player_get(player_index);

	if (shadowrun_player && !shadowrun_player->given_money)
	{
		shadowrun_player->money = SHADOWRUN_STARTING_MONEY;
		shadowrun_player->given_money = TRUE;
	}

	return;
}

/* port: a local player's controls, pressed this tick: the Back control
opens or closes the menu; while it is open, the stick moves through it, the
jump control buys, the melee control goes back, the action control buys a
magazine. TRUE while it is open (the player does nothing else) */
boolean shadowrun_buy_menu_input(
	short local_player_index,
	boolean toggle,
	boolean buy,
	boolean back,
	boolean magazine,
	real forward,
	real strafe)
{
	struct shadowrun_shop_state *state;
	boolean result = FALSE;

	if (shadowrun_map_loaded() &&
		local_player_index >= 0 && local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS)
	{
		state = &shadowrun_shop_states[local_player_index];
		/* port: the menu opens on the prototype's maps for a player whose unit
		has a Shadowrun unit definition (srud), its own or, where the map gives
		the player none (recomp.map), the one the map sells by
		(shadowrun_unit_definition_for_shopping: docs/shadowrun_caches.md) */
		{
			struct player_datum *debug_player = shadowrun_shop_player(local_player_index);
			struct shadowrun_unit_definition *debug_definition = shadowrun_shop_unit_definition(local_player_index);
			long debug_unit_index = debug_player ? debug_player->unit_index : NONE;

			if (toggle)
			{
				/* port: the map's srud lookup runs before the first line is printed:
				it borrows a line of the terminal's output buffer, which printing the
				first line would overwrite (the line came out cut, and with the rest
				of the format in place of the name) */
				char const *first_name = shadowrun_unit_definition_first_name();
				boolean has_generic_unit = shadowrun_unit_definition_matches(SHADOWRUN_GENERIC_UNIT_DEFINITION);

				console_printf(FALSE, "buy menu: map_loaded %d, unit %ld, own srud %d, shopping srud %d",
					shadowrun_map_loaded(), debug_unit_index,
					debug_unit_index != NONE && shadowrun_unit_definition_get(debug_unit_index) != NULL,
					debug_definition != NULL);
				console_printf(FALSE, "buy menu: map has generic_unit %d, first srud \"%s\"",
					has_generic_unit, first_name);
			}
		}
		if (!shadowrun_shop_unit_definition(local_player_index) || cinematic_in_progress())
		{
			if (toggle)
			{
				/* port: why it did not open (the lines above are the lookup's) */
				console_printf(FALSE, "buy menu: it does not open: no srud %d, cinematic %d",
					shadowrun_shop_unit_definition(local_player_index) == NULL, cinematic_in_progress());
			}
			/* 0x0FD0C0: without a unit of its own it closes */
			if (state->open)
			{
				shadowrun_shop_close(local_player_index);
			}
		}
		else
		{
			if (toggle)
			{
				if (state->open)
				{
					shadowrun_shop_close(local_player_index);
				}
				else
				{
					shadowrun_shop_open(local_player_index);
				}
			}
			else if (state->open)
			{
				shadowrun_shop_move(local_player_index, forward, strafe);
				if (back)
				{
					shadowrun_shop_back(local_player_index);
				}
				else if (buy)
				{
					shadowrun_shop_buy(local_player_index);
				}
				else if (magazine)
				{
					shadowrun_shop_buy_magazine(local_player_index);
				}
			}
			result = state->open;
		}
	}

	return result;
}

/* 0x100DB0: the open menu over the local player's view: the player's name
and money, and the page's items about the one chosen (five), each with its
price, a star if the player has it, and a spell's mana; the chosen one
blinks, one the player cannot afford is grey, one it has green, one it
cannot buy faint */
void shadowrun_buy_menu_draw(
	short local_player_index)
{
	struct shadowrun_shop_state *state;
	struct shadowrun_unit_definition *definition;
	struct player_datum *player;
	struct shadowrun_player *shadowrun_player;
	long font_index;
	struct font_header *font;
	long list[MAXIMUM_SHADOWRUN_SHOP_LIST];
	short count;
	short chosen = NONE;
	short first;
	short last;
	short row;
	short line_height;
	short y;
	short middle;
	rectangle2d bounds;
	rectangle2d text_bounds;
	real_argb_color normal_color;
	char text[128];
	short index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS ||
		!shadowrun_shop_states[local_player_index].open)
	{
		return;
	}
	state = &shadowrun_shop_states[local_player_index];
	definition = shadowrun_shop_unit_definition(local_player_index);
	player = shadowrun_shop_player(local_player_index);
	shadowrun_player = player ? shadowrun_player_get(local_player_get_player_index(local_player_index)) : NULL;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (!definition || !shadowrun_player || font_index == NONE)
	{
		return;
	}
	font = font_definition_get(font_index);
	line_height = local_player_count() > 1 ?
		(short)(font->ascending_height + font->leading_height) :
		(short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
	{
		return;
	}

	count = shadowrun_shop_list(local_player_index, list);
	if (count == 0)
	{
		shadowrun_shop_back(local_player_index);
		return;
	}
	if (state->gift)
	{
		state->selection = (short)(((state->selection % count) + count) % count);
		chosen = state->selection;
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			if (list[index] == state->selection)
			{
				chosen = index;
				break;
			}
		}
		if (chosen == NONE)
		{
			chosen = 0;
			state->selection = (short)list[0];
		}
	}

	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	middle = (short)((bounds.x0 + bounds.x1) / 2);
	normal_color.alpha = 0.75f;
	normal_color.red = 0.459f;
	normal_color.green = 0.729f;
	normal_color.blue = 1.f;

	/* the player and its money */
	{
		char name[sizeof(player->name) / sizeof(player->name[0]) + 1];

		for (index = 0; index < (short)(sizeof(player->name) / sizeof(player->name[0])) && player->name[index]; index++)
		{
			name[index] = player->name[index] < 0x80 ? (char)player->name[index] : '?';
		}
		name[index] = 0;
		snprintf(text, sizeof(text), "%s: $%ld", name, shadowrun_player->money);
	}
	text_bounds = bounds;
	text_bounds.y0 = (short)(bounds.y0 + line_height * 2);
	text_bounds.y1 = (short)(text_bounds.y0 + line_height);
	draw_string_set_draw_mode(font_index, NONE, _shadowrun_shop_text_center, 0, &normal_color);
	rasterizer_draw_string(&text_bounds, NULL, NULL, 0, text);

	/* five rows about the chosen one (fewer, at the page's ends) */
	first = (short)MAX(chosen - SHADOWRUN_SHOP_ROWS / 2, 0);
	last = (short)(first + SHADOWRUN_SHOP_ROWS - 1);
	while (last >= count)
	{
		last--;
		first--;
	}
	y = (short)(bounds.y0 + line_height * 3 + ((count & 1) == 0 ? line_height / 2 : 0));
	if (game_time_get() > state->blink_time + SHADOWRUN_SHOP_BLINK_TICKS)
	{
		state->blink_time = game_time_get();
		state->blink = !state->blink;
	}
	for (row = first; row <= last; row++, y = (short)(y + line_height))
	{
		real_argb_color color = normal_color;
		long price = 0;
		boolean owned = FALSE;
		short cells = 0;
		char price_text[32];
		point2d cursor;

		if (row < 0)
		{
			continue;
		}
		price_text[0] = 0;
		if (state->gift)
		{
			struct player_datum *teammate = player_try_and_get(list[row]);

			text[0] = 0;
			if (teammate)
			{
				for (index = 0; index < (short)(sizeof(teammate->name) / sizeof(teammate->name[0])) && teammate->name[index]; index++)
				{
					text[index] = teammate->name[index] < 0x80 ? (char)teammate->name[index] : '?';
				}
				text[index] = 0;
			}
			snprintf(price_text, sizeof(price_text), "%d", SHADOWRUN_SHOP_GIFT);
			if (row != chosen && shadowrun_player->money < SHADOWRUN_SHOP_GIFT)
			{
				color.alpha = 0.5f;
				color.red = color.green = color.blue = 0.25f;
			}
		}
		else
		{
			struct shadowrun_shop_item const *item = &shadowrun_shop_items[list[row]];

			price = shadowrun_shop_price(local_player_index, definition, item->price_index);
			owned = shadowrun_shop_owned(local_player_index, player->unit_index, definition, (short)list[row]);
			snprintf(text, sizeof(text), "%s", item->name);
			if (owned && item->page == _shadowrun_shop_page_tech)
			{
				snprintf(price_text, sizeof(price_text), "%ld remove",
					shadowrun_shop_price(local_player_index, definition, item->removal_price_index));
			}
			else
			{
				snprintf(price_text, sizeof(price_text), "%ld", price);
			}
			if (owned)
			{
				strcat(price_text, "*");
			}
			/* 0x102510: a spell's mana (tech's capacity is not played) */
			if (item->action >= _shadowrun_shop_first_magic && item->action <= _shadowrun_shop_last_magic)
			{
				long mana_cost = 0;

				shadowrun_spell_get(player->unit_index, shadowrun_shop_spells[item->action - _shadowrun_shop_first_magic],
					&mana_cost, NULL, NULL, NULL, NULL);
				cells = (short)PIN(mana_cost, 0, 20);
			}
			if (row != chosen)
			{
				if (shadowrun_player->money < price)
				{
					color.alpha = 0.5f;
					color.red = color.green = color.blue = 0.25f;
				}
				else if (owned)
				{
					color.red = 0.f;
					color.green = 1.f;
					color.blue = 0.f;
				}
				if (!shadowrun_shop_available(local_player_index, player->unit_index, definition, (short)list[row]))
				{
					color.alpha = 0.5f;
				}
			}
		}
		if (row == chosen)
		{
			color.alpha = 0.75f;
			color.red = color.green = color.blue = state->blink ? 1.f : 0.f;
		}

		text_bounds.y0 = y;
		text_bounds.y1 = (short)(y + line_height);
		text_bounds.x0 = bounds.x0;
		text_bounds.x1 = (short)(middle - 20);
		draw_string_set_draw_mode(font_index, NONE, _shadowrun_shop_text_right, 0, &color);
		rasterizer_draw_string(&text_bounds, NULL, NULL, 0, text);
		if (state->gift || price != 0)
		{
			text_bounds.x0 = (short)(middle + 20);
			text_bounds.x1 = bounds.x1;
			cursor.x = text_bounds.x0;
			cursor.y = text_bounds.y0;
			draw_string_set_draw_mode(font_index, NONE, _shadowrun_shop_text_left, 0, &color);
			rasterizer_draw_string(&text_bounds, NULL, &cursor, 0, price_text);
			for (index = 0; index < cells; index++)
			{
				rectangle2d cell;

				cell.x0 = (short)(cursor.x + 10 + index * 8);
				cell.x1 = (short)(cell.x0 + 6);
				cell.y1 = (short)(y + line_height - 4);
				cell.y0 = (short)(cell.y1 - 8);
				draw_quad(&cell, ((pixel32)(color.alpha * 255.f) << 24) | 0x0073BAFF);
			}
		}
	}

	return;
}

/* ---------- private code */

static struct player_datum *shadowrun_shop_player(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);

	return player_index != NONE ? player_try_and_get(player_index) : NULL;
}

/* port: the srud the menu sells by for the local player: the player's own,
else the first the map has (recomp.map gives its players none), else NULL,
and the prices are the prototype's (shadowrun_shop_price) */
static struct shadowrun_unit_definition *shadowrun_shop_unit_definition(
	short local_player_index)
{
	struct player_datum *player = shadowrun_shop_player(local_player_index);
	struct shadowrun_unit_definition *definition;

	if (player && player->unit_index != NONE && (definition = shadowrun_unit_definition_get(player->unit_index)) != NULL)
	{
		return definition;
	}

	return shadowrun_unit_definition_for_shopping();
}

/* port: the buy menu definitions a sound may be played from: the player's
owned, else the map's first, else NULL (its sounds are not played) */
static struct shadowrun_buy_menu_definition *shadowrun_shop_buy_menu(
	short local_player_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_shop_unit_definition(local_player_index);

	return definition && definition->buy_menu.index != NONE ?
		tag_get(SHADOWRUN_BUY_MENU_DEFINITION_TAG, definition->buy_menu.index) : NULL;
}

/* one of the buy menu's sounds (its index among them), on this machine */
static void shadowrun_shop_sound(
	short local_player_index,
	short sound)
{
	struct shadowrun_buy_menu_definition *buy_menu = shadowrun_shop_buy_menu(local_player_index);

	if (buy_menu)
	{
		struct tag_reference const *reference = &(&buy_menu->open_sound)[sound];

		if (reference->index != NONE)
		{
			unspatialized_impulse_sound_new(reference->index, 1.f);
		}
	}

	return;
}

/* 0x0FEAE0: an item's price, the srud's (0 for none). port: a map that gives
the player no srud of its own (recomp.map) sells at the prototype's
(shadowrun_shop_stock_prices) */
static long shadowrun_shop_price(
	short local_player_index,
	struct shadowrun_unit_definition *definition,
	short price_index)
{
	struct player_datum *player = shadowrun_shop_player(local_player_index);

	if (price_index < 0 || price_index >= NUMBER_OF_SHADOWRUN_PRICES)
	{
		return 0;
	}
	if (player && player->unit_index != NONE && definition == shadowrun_unit_definition_get(player->unit_index))
	{
		return definition->prices[price_index];
	}

	return shadowrun_shop_stock_prices[price_index];
}

/* port: the weapon definition the menu's weapon `weapon`
(NUMBER_OF_SHADOWRUN_SHOP_WEAPONS' order) is: the player's srud's, else the
one the prices come from (the map's first), else NONE */
static long shadowrun_shop_weapon_definition(
	short local_player_index,
	short weapon)
{
	struct shadowrun_unit_definition *definition = shadowrun_shop_unit_definition(local_player_index);

	return definition && weapon >= 0 && weapon < NUMBER_OF_SHADOWRUN_SHOP_WEAPONS ?
		definition->shop_weapons[weapon].index : NONE;
}

/* 0x0FF030: the menu opens at its first page */
static void shadowrun_shop_open(
	short local_player_index)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];

	shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_open);
	csmemset(state, 0, sizeof(*state));
	state->open = TRUE;
	state->page = _shadowrun_shop_page_main;
	state->selection = 0;
	state->stick_up_held = TRUE;
	state->stick_down_held = TRUE;
	state->blink = TRUE;
	state->blink_time = game_time_get();
	state->purchase_time = game_time_get();

	return;
}

/* 0x0FF220 */
static void shadowrun_shop_close(
	short local_player_index)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];

	if (state->open)
	{
		shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_close);
	}
	state->open = FALSE;

	return;
}

/* 0x0FE5E0: back from a page to the first, or from the first closed.
TRUE when it closed */
static boolean shadowrun_shop_back(
	short local_player_index)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	boolean result = FALSE;

	state->gift = FALSE;
	shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_page);
	if (state->page == _shadowrun_shop_page_main)
	{
		shadowrun_shop_close(local_player_index);
		result = TRUE;
	}
	else
	{
		state->page = _shadowrun_shop_page_main;
		state->selection = 0;
	}

	return result;
}

/* a page, with its first item chosen */
static void shadowrun_shop_set_page(
	short local_player_index,
	short page)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	short item;

	shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_page);
	state->page = page;
	for (item = 0; item < NUMBER_OF_SHADOWRUN_SHOP_ITEMS; item++)
	{
		if (shadowrun_shop_items[item].page == page)
		{
			state->selection = item;
			return;
		}
	}

	return;
}

/* 0x0ECDC0: the stick forward chooses the item before, back the one after
(round the page); it moves again once it has come back to the middle */
static void shadowrun_shop_move(
	short local_player_index,
	real forward,
	real strafe)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	short step = 0;

	if (fabs(forward) < SHADOWRUN_SHOP_STICK_CENTERED && fabs(strafe) < SHADOWRUN_SHOP_STICK_CENTERED)
	{
		state->stick_up_held = FALSE;
		state->stick_down_held = FALSE;
	}
	if (forward < -SHADOWRUN_SHOP_STICK_MOVE && fabs(strafe) < SHADOWRUN_SHOP_STICK_CENTERED && !state->stick_down_held)
	{
		step = 1;
		state->stick_down_held = TRUE;
	}
	else if (forward > SHADOWRUN_SHOP_STICK_MOVE && fabs(strafe) < SHADOWRUN_SHOP_STICK_CENTERED && !state->stick_up_held)
	{
		step = -1;
		state->stick_up_held = TRUE;
	}
	if (step != 0)
	{
		if (state->gift)
		{
			state->selection = (short)(state->selection + step);
		}
		else
		{
			short page = shadowrun_shop_items[state->selection].page;
			short item = state->selection;

			do
			{
				item = (short)((item + step + NUMBER_OF_SHADOWRUN_SHOP_ITEMS) % NUMBER_OF_SHADOWRUN_SHOP_ITEMS);
			}
			while (shadowrun_shop_items[item].page != page);
			state->selection = item;
		}
		shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cursor);
	}

	return;
}

/* 0x0FD0C0: the chosen item bought, if the player can afford it: a page
opened, a grenade (up to two), a spell learned, a weapon (the one in hand
dropped for it, if the player's hands are full), a gift to a teammate; what
cannot be bought (one the player has, tech) is not paid for. Purchases wait
a second after the last */
static void shadowrun_shop_buy(
	short local_player_index)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	struct shadowrun_unit_definition *definition = shadowrun_shop_unit_definition(local_player_index);
	struct player_datum *player = shadowrun_shop_player(local_player_index);
	struct shadowrun_player *shadowrun_player =
		shadowrun_player_get(local_player_get_player_index(local_player_index));
	boolean waiting = game_time_get() < state->purchase_time + SHADOWRUN_SHOP_PURCHASE_WAIT;

	if (!definition || !player || !shadowrun_player)
	{
		return;
	}
	if (state->gift)
	{
		long list[MAXIMUM_SHADOWRUN_SHOP_LIST];
		short count = shadowrun_shop_list(local_player_index, list);

		if (shadowrun_player->money < SHADOWRUN_SHOP_GIFT)
		{
			shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_afford);
		}
		else if (!waiting && count > 0)
		{
			struct shadowrun_player *teammate =
				shadowrun_player_get(list[((state->selection % count) + count) % count]);

			if (teammate)
			{
				shadowrun_player->money -= SHADOWRUN_SHOP_GIFT;
				teammate->money += SHADOWRUN_SHOP_GIFT;
				state->purchase_time = game_time_get();
			}
		}
	}
	else
	{
		struct shadowrun_shop_item const *item = &shadowrun_shop_items[state->selection];
		long price = shadowrun_shop_price(local_player_index, definition, item->price_index);
		long unit_index = player->unit_index;
		short action = item->action;

		if (shadowrun_player->money < price && price > 0)
		{
			shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_afford);
		}
		else if (action == _shadowrun_shop_done)
		{
			shadowrun_shop_close(local_player_index);
		}
		else if (action == _shadowrun_shop_open_tech)
		{
			shadowrun_shop_set_page(local_player_index, _shadowrun_shop_page_tech);
		}
		else if (action == _shadowrun_shop_open_magic)
		{
			shadowrun_shop_set_page(local_player_index, _shadowrun_shop_page_magic);
		}
		else if (action == _shadowrun_shop_open_weapons)
		{
			shadowrun_shop_set_page(local_player_index, _shadowrun_shop_page_weapons);
		}
		else if (action == _shadowrun_shop_open_main)
		{
			shadowrun_shop_set_page(local_player_index, _shadowrun_shop_page_main);
		}
		else if (action == _shadowrun_shop_open_gift)
		{
			shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_page);
			state->page = _shadowrun_shop_page_gift;
			state->gift = TRUE;
			state->selection = 0;
		}
		else if (action == _shadowrun_shop_open_empty)
		{
			shadowrun_shop_set_page(local_player_index, _shadowrun_shop_page_empty);
		}
		else if (waiting)
		{
			/* (another purchase so soon is not made) */
		}
		else if (action == _shadowrun_shop_grenade || action == _shadowrun_shop_anti_magic_grenade)
		{
			struct unit_datum *unit = unit_get(unit_index);
			/* port: the anti-magic grenade is the unit's second type of grenade
			(shadowrun_tech.h): it is carried and thrown as one */
			short type = action == _shadowrun_shop_anti_magic_grenade ?
				SHADOWRUN_ANTIMAGIC_GRENADE_TYPE : _shadowrun_shop_grenade_type;

			if (unit->unit.grenade_counts[type] < SHADOWRUN_SHOP_MAXIMUM_GRENADES)
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_bought);
				unit->unit.grenade_counts[type]++;
				shadowrun_player->money -= price;
				state->purchase_time = game_time_get();
			}
			else
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_buy);
			}
		}
		else if (action >= _shadowrun_shop_first_magic && action <= _shadowrun_shop_last_magic)
		{
			short spell = shadowrun_shop_spells[action - _shadowrun_shop_first_magic];

			if (shadowrun_shop_owned(local_player_index, unit_index, definition, state->selection))
			{
				/* port: had: the removal's price forgets it (as a tech's takes it
				off), so the player may buy another in its place */
				long removal_price = shadowrun_shop_price(local_player_index, definition,
					shadowrun_shop_items[state->selection].removal_price_index);

				if (shadowrun_player->money < removal_price)
				{
					shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_afford);
				}
				else
				{
					shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_known);
					shadowrun_unit_forget_power(unit_index, spell);
					shadowrun_player->money -= removal_price;
					state->purchase_time = game_time_get();
				}
			}
			else
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_learned);
				shadowrun_unit_learn_power(unit_index, spell);
				shadowrun_player->money -= price;
				state->purchase_time = game_time_get();
			}
		}
		else if (action == _shadowrun_shop_razorclaws || action == _shadowrun_shop_katana ||
			(action >= _shadowrun_shop_first_weapon && action <= _shadowrun_shop_last_weapon))
		{
			short weapon = action == _shadowrun_shop_razorclaws ? _shadowrun_shop_weapon_razorclaws :
				action == _shadowrun_shop_katana ? _shadowrun_shop_weapon_katana :
				shadowrun_shop_weapons[action - _shadowrun_shop_first_weapon];
			long weapon_definition_index = shadowrun_shop_weapon_definition(local_player_index, weapon);

			if (weapon_definition_index != NONE && !shadowrun_unit_has_weapon(unit_index, weapon_definition_index))
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_bought);
				shadowrun_unit_give_weapon(unit_index, weapon_definition_index);
				shadowrun_player->money -= price;
				state->purchase_time = game_time_get();
			}
			else
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_buy);
			}
		}
		else if (action >= _shadowrun_shop_first_tech && action <= _shadowrun_shop_last_tech)
		{
			/* port: a tech the port plays is learned (the executable's shop gives
			the unit the tech's pointers, its code's place) */
			short tech = shadowrun_shop_tech_id(state->selection);

			if (tech != NONE)
			{
				if (shadowrun_player_has_tech(shadowrun_player, tech))
				{
					/* (had: the removal's price takes it off, as the menu says;
					the techs it also has stay) */
					long removal_price = shadowrun_shop_price(local_player_index, definition,
						shadowrun_shop_items[state->selection].removal_price_index);

					if (shadowrun_player->money < removal_price)
					{
						shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_afford);
					}
					else
					{
						shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_tech_owned);
						shadowrun_player_remove_tech(shadowrun_player, unit_index, tech);
						shadowrun_player->money -= removal_price;
						state->purchase_time = game_time_get();
					}
				}
				else
				{
					shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_tech);
					shadowrun_player_buy_tech(shadowrun_player, unit_index, tech);
					shadowrun_player->money -= price;
					state->purchase_time = game_time_get();
				}
			}
			else
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_buy);
			}
		}
		else
		{
			/* (an item the menu has no way to sell) */
			shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_cannot_buy);
		}
	}

	return;
}

/* 0x0FE790: a magazine for the weapon in hand, if the menu sells one for it
and it has room (a magazine's rounds, a shotgun's four) */
static void shadowrun_shop_buy_magazine(
	short local_player_index)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	struct shadowrun_unit_definition *definition = shadowrun_shop_unit_definition(local_player_index);
	struct player_datum *player = shadowrun_shop_player(local_player_index);
	struct shadowrun_player *shadowrun_player =
		shadowrun_player_get(local_player_get_player_index(local_player_index));

	if (definition && player && shadowrun_player &&
		game_time_get() >= state->purchase_time + SHADOWRUN_SHOP_PURCHASE_WAIT)
	{
		struct unit_datum *unit = unit_get(player->unit_index);
		long weapon_index = unit->unit.current_weapon_index != NONE ?
			unit_inventory_get_weapon(player->unit_index, unit->unit.current_weapon_index) : NONE;
		long price = NONE;
		short weapon;

		if (weapon_index != NONE)
		{
			long definition_index = object_get(weapon_index)->definition_index;

			for (weapon = 0; weapon < NUMBER_OF_SHADOWRUN_SHOP_WEAPONS; weapon++)
			{
				if (shadowrun_shop_weapon_definition(local_player_index, weapon) == definition_index &&
					shadowrun_shop_magazine_prices[weapon] != NONE)
				{
					price = shadowrun_shop_price(local_player_index, definition, shadowrun_shop_magazine_prices[weapon]);
					break;
				}
			}
		}
		if (price >= 0)
		{
			if (shadowrun_player->money < price)
			{
				shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_magazine_failed);
			}
			else
			{
				struct weapon_datum *weapon_datum = weapon_get(weapon_index);
				struct weapon_definition *weapon_definition = weapon_definition_get(weapon_datum->definition_index);
				boolean added = FALSE;

				/* 0x1772A0 */
				if (weapon_definition->weapon.magazines.count > 0)
				{
					struct weapon_magazine_definition *magazine_definition = TAG_BLOCK_GET_ELEMENT(
						&weapon_definition->weapon.magazines, 0, struct weapon_magazine_definition);
					struct weapon_magazine *magazine = &weapon_datum->weapon.magazines[0];

					if (magazine->rounds_total != magazine_definition->rounds_total_maximum)
					{
						short rounds = magazine_definition->rounds_reloaded;

						if (weapon_definition->weapon.weapon_type == 1)
						{
							rounds *= 4;
						}
						magazine->rounds_total = (short)MIN(magazine->rounds_total + rounds,
							magazine_definition->rounds_total_maximum);
						added = TRUE;
					}
				}
				if (added)
				{
					shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_magazine);
					shadowrun_player->money -= price;
					state->purchase_time = game_time_get();
				}
				else
				{
					shadowrun_shop_sound(local_player_index, _shadowrun_shop_sound_magazine_failed);
				}
			}
		}
	}

	return;
}

/* the page's items (or, giving, the player's teammates): their count */
static short shadowrun_shop_list(
	short local_player_index,
	long *list)
{
	struct shadowrun_shop_state *state = &shadowrun_shop_states[local_player_index];
	short count = 0;

	if (state->gift)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_try_and_get(player_index);
		struct data_iterator iterator;
		struct player_datum *teammate;

		if (player)
		{
			data_iterator_new(&iterator, player_data);
			while ((teammate = data_iterator_next(&iterator)) != NULL && count < MAXIMUM_SHADOWRUN_SHOP_LIST)
			{
				if (iterator.datum_index != player_index && teammate->team_index == player->team_index)
				{
					list[count++] = iterator.datum_index;
				}
			}
		}
	}
	else
	{
		short item;

		for (item = 0; item < NUMBER_OF_SHADOWRUN_SHOP_ITEMS && count < MAXIMUM_SHADOWRUN_SHOP_LIST; item++)
		{
			if (shadowrun_shop_items[item].page == state->page)
			{
				list[count++] = item;
			}
		}
	}

	return count;
}

/* 0x0FF3A0: whether the player has the item: grenades of its kind, the
spell, the weapon (port: and the tech, which it wears and may take off) */
static boolean shadowrun_shop_owned(
	short local_player_index,
	long unit_index,
	struct shadowrun_unit_definition *definition,
	short item)
{
	short action = shadowrun_shop_items[item].action;
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = FALSE;

	if (action == _shadowrun_shop_grenade)
	{
		result = unit_get_grenade_count(unit_index, _shadowrun_shop_grenade_type) > 0;
	}
	else if (action == _shadowrun_shop_anti_magic_grenade)
	{
		/* port: the unit's second type of grenade */
		result = unit_get_grenade_count(unit_index, SHADOWRUN_ANTIMAGIC_GRENADE_TYPE) > 0;
	}
	else if (action >= _shadowrun_shop_first_tech && action <= _shadowrun_shop_last_tech)
	{
		/* port: the tech the item sells, which the player has bought (it may
		have several) */
		short tech = shadowrun_shop_tech_id(item);

		result = tech != NONE && shadowrun_player_has_tech(
			shadowrun_player_get(player_index_from_unit_index(unit_index)), tech);
	}
	else if (action >= _shadowrun_shop_first_magic && action <= _shadowrun_shop_last_magic)
	{
		result = shadowrun_unit &&
			shadowrun_unit->known_powers[shadowrun_shop_spells[action - _shadowrun_shop_first_magic]] > 0;
	}
	else if (action == _shadowrun_shop_razorclaws || action == _shadowrun_shop_katana ||
		(action >= _shadowrun_shop_first_weapon && action <= _shadowrun_shop_last_weapon))
	{
		short weapon = action == _shadowrun_shop_razorclaws ? _shadowrun_shop_weapon_razorclaws :
			action == _shadowrun_shop_katana ? _shadowrun_shop_weapon_katana :
			shadowrun_shop_weapons[action - _shadowrun_shop_first_weapon];

		result = shadowrun_shop_weapon_definition(local_player_index, weapon) != NONE &&
			shadowrun_unit_has_weapon(unit_index, shadowrun_shop_weapon_definition(local_player_index, weapon));
	}

	return result;
}

/* port: the tech an item of the tech page sells, or NONE (not played) */
static short shadowrun_shop_tech_id(
	short item)
{
	short action = shadowrun_shop_items[item].action;

	return action >= _shadowrun_shop_first_tech && action <= _shadowrun_shop_last_tech ?
		shadowrun_shop_techs[action - _shadowrun_shop_first_tech] : NONE;
}

/* 0x101F70: whether the player may buy the item: fewer than two grenades
of its kind, a spell it does not know, a weapon it does not have (port: and
not tech, nor an anti-magic grenade) */
static boolean shadowrun_shop_available(
	short local_player_index,
	long unit_index,
	struct shadowrun_unit_definition *definition,
	short item)
{
	short action = shadowrun_shop_items[item].action;
	boolean result;

	if (action == _shadowrun_shop_grenade)
	{
		result = unit_get_grenade_count(unit_index, _shadowrun_shop_grenade_type) < SHADOWRUN_SHOP_MAXIMUM_GRENADES;
	}
	else if (action == _shadowrun_shop_anti_magic_grenade)
	{
		/* port: up to two, as a grenade's kind is */
		result = unit_get_grenade_count(unit_index, SHADOWRUN_ANTIMAGIC_GRENADE_TYPE) <
			SHADOWRUN_SHOP_MAXIMUM_GRENADES;
	}
	else if (action >= _shadowrun_shop_first_tech && action <= _shadowrun_shop_last_tech)
	{
		/* port: a tech the port plays is sold once, until it is taken off, and
		no more than the player's two (port/linux/game/shadowrun_units.h) */
		struct shadowrun_player *shadowrun_player =
			shadowrun_player_get(player_index_from_unit_index(unit_index));

		result = shadowrun_shop_tech_id((short)item) != NONE &&
			!shadowrun_shop_owned(local_player_index, unit_index, definition, item) && shadowrun_player &&
			shadowrun_player->known_tech_count < MAXIMUM_SHADOWRUN_PLAYER_TECHS;
	}
	else if (action >= _shadowrun_shop_first_magic && action <= _shadowrun_shop_last_magic)
	{
		/* port: a spell it does not know, while the player has fewer than its
		two */
		result = !shadowrun_shop_owned(local_player_index, unit_index, definition, item) &&
			shadowrun_player_known_spell_count(
				shadowrun_player_get(player_index_from_unit_index(unit_index))) < MAXIMUM_SHADOWRUN_PLAYER_SPELLS;
	}
	else if (action < _shadowrun_shop_grenade)
	{
		result = TRUE;
	}
	else
	{
		result = !shadowrun_shop_owned(local_player_index, unit_index, definition, item);
	}

	return result;
}

/* 0x27EEC0: whether the unit carries a weapon of the definition */
static boolean shadowrun_unit_has_weapon(
	long unit_index,
	long weapon_definition_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	boolean result = FALSE;
	short index;

	for (index = 0; index < MAXIMUM_WEAPONS_PER_UNIT; index++)
	{
		long weapon_index = unit->unit.weapon_object_indices[index];

		if (weapon_index != NONE && object_get(weapon_index)->definition_index == weapon_definition_index)
		{
			result = TRUE;
		}
	}

	return result;
}

/* 0x27B9F0: a new weapon of the definition to the unit; with two weapons
that count already, the one in hand is dropped for it. (The executable
drops the first of the two when it is in hand, else the second) */
static void shadowrun_unit_give_weapon(
	long unit_index,
	long weapon_definition_index)
{
	struct object_placement_data placement;
	long weapon_index;

	object_placement_data_new(&placement, weapon_definition_index, unit_index);
	weapon_index = object_new(&placement);
	if (weapon_index != NONE)
	{
		struct unit_datum *unit = unit_get(unit_index);
		struct weapon_definition *definition = weapon_definition_get(weapon_definition_index);
		short counted = 0;
		short index;

		for (index = 0; index < MAXIMUM_WEAPONS_PER_UNIT; index++)
		{
			long held_index = unit->unit.weapon_object_indices[index];

			if (held_index != NONE &&
				!TEST_FLAG(weapon_definition_get(object_get(held_index)->definition_index)->weapon.flags,
					_weapon_doesnt_count_toward_maximum_bit))
			{
				counted++;
			}
		}
		if (counted > 1 && !TEST_FLAG(definition->weapon.flags, _weapon_doesnt_count_toward_maximum_bit))
		{
			unit_drop_current_weapon(unit_index, TRUE);
		}
		if (!unit_add_weapon_to_inventory(unit_index, weapon_index, _unit_add_weapon_starting))
		{
			object_delete(weapon_index);
		}
	}

	return;
}
