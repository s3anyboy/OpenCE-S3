/* The Shadowrun prototype's buy menu and anti-magic grenades in a network
game (co-op on its campaign maps, or its multiplayer). What a player has
bought (its money, spells, techs, bought mana and its unit's anti-magic
grenades) is the host's: a client's menu judges a purchase (its sound) and
asks the host, which makes it (shadowrun_buy_menu.c), and the host sends
every client what every player has, reliably, when it changes and when the
client is ready. A gift of money, and a magazine for the weapon in hand, go
the same way.

A client's anti-magic grenades chosen (the grenade control's third kind,
shadowrun_tech.c) are sent to the host when the choice changes: the host
throws its copy of the client's unit's grenades (the projectile, the count),
so it throws the anti-magic grenade the client chose.

What each machine chooses for itself is not sent back: a client's spell and
tech chosen, and its mana, are its own. */

#include "cseries.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "objects/objects.h"
#include "units/units.h"
#include "networking/network_game_globals.h"
#include "networking/network_connection.h"
#include "network_distributed.h"
#include "network_shadowrun.h"
#include "shadowrun.h"
#include "shadowrun_definitions.h"
#include "shadowrun_units.h"
#include <string.h>

/* (a purchase message's kind past the purchases: the anti-magic grenades
chosen, or not) */
#define SHADOWRUN_MESSAGE_GRENADE_CHOICE NUMBER_OF_SHADOWRUN_PURCHASES

struct shadowrun_purchase_message
{
	struct distributed_message_header header;
	byte kind;
	byte player_index;
	byte gift_player_index;
	byte chosen;
	short item_index;
	word reserved;
};

struct shadowrun_player_entry
{
	byte player_index;
	byte known_tech_count;
	byte mana_capacity_bonus;
	byte given_money;
	long money;
	long known_techs[MAXIMUM_SHADOWRUN_PLAYER_TECHS];
	byte known_powers[MAXIMUM_SHADOWRUN_POWERS];
	char antimagic_grenades;
	byte reserved;
};

struct shadowrun_players_message
{
	struct distributed_message_header header;
	struct shadowrun_player_entry entries[MAXIMUM_TRACKED_PLAYERS];
};

/* (what goes on the wire, as it is on every machine) */
typedef char shadowrun_purchase_message_size_assert[
	sizeof(struct shadowrun_purchase_message) == sizeof(struct distributed_message_header) + 8 ? 1 : -1];
typedef char shadowrun_player_entry_size_assert[
	sizeof(struct shadowrun_player_entry) == 8 + 4 * MAXIMUM_SHADOWRUN_PLAYER_TECHS + MAXIMUM_SHADOWRUN_POWERS + 2 ? 1 : -1];

/* the host's: what it last sent its clients */
static struct shadowrun_player_entry sent_entries[MAXIMUM_TRACKED_PLAYERS];
static short sent_count = NONE;
/* a client's: its local players' anti-magic grenade choice it last sent
(NONE: none sent) */
static short sent_choices[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

void network_shadowrun_reset(void)
{
	short index;

	sent_count = NONE;
	memset(sent_entries, 0, sizeof(sent_entries));
	for (index = 0; index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; index++)
		sent_choices[index] = NONE;
}

/* every player's purchases, in entries: their count */
static short shadowrun_players_make(struct shadowrun_player_entry *entries)
{
	struct data_iterator iterator;
	short count = 0;

	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator) && count < MAXIMUM_TRACKED_PLAYERS)
	{
		struct shadowrun_player_purchases purchases;
		struct shadowrun_player_entry *entry = &entries[count];
		short index;

		if (!shadowrun_player_purchases_get(iterator.datum_index, &purchases))
			continue;
		memset(entry, 0, sizeof(*entry));
		entry->player_index = distributed_player_to_byte(iterator.datum_index);
		entry->known_tech_count = (byte)purchases.known_tech_count;
		entry->mana_capacity_bonus = purchases.mana_capacity_bonus;
		entry->given_money = purchases.given_money ? 1 : 0;
		entry->money = purchases.money;
		for (index = 0; index < MAXIMUM_SHADOWRUN_PLAYER_TECHS; index++)
			entry->known_techs[index] = purchases.known_techs[index];
		memcpy(entry->known_powers, purchases.known_powers, sizeof(entry->known_powers));
		entry->antimagic_grenades = purchases.antimagic_grenades;
		count++;
	}
	return count;
}

static boolean shadowrun_network_playing(void)
{
	return game_in_progress() && !main_menu_is_active() && shadowrun_map_loaded();
}

void network_shadowrun_send(long machine_index)
{
	struct shadowrun_players_message message;
	short count;

	if (game_connection() != _game_connection_network_server || !shadowrun_network_playing())
		return;
	memset(&message, 0, sizeof(message));
	count = shadowrun_players_make(message.entries);
	if (count <= 0)
		return;
	distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_shadowrun_players, count,
		(word)(sizeof(message.header) + count * sizeof(message.entries[0])));
}

void network_shadowrun_send_purchase(long player_index, short kind, short item_index, long gift_player_index)
{
	struct shadowrun_purchase_message message;

	if (game_connection() != _game_connection_network_client || player_index == NONE ||
		kind < 0 || kind >= NUMBER_OF_SHADOWRUN_PURCHASES)
		return;
	memset(&message, 0, sizeof(message));
	message.kind = (byte)kind;
	message.player_index = distributed_player_to_byte(player_index);
	message.gift_player_index = gift_player_index != NONE ? distributed_player_to_byte(gift_player_index) : NO_PLAYER;
	message.item_index = item_index;
	distributed_send(&message, _distributed_message_shadowrun_purchase, 1, sizeof(message),
		_distributed_to_host_reliably);
}

void network_shadowrun_update(void)
{
	if (!shadowrun_network_playing())
		return;
	if (game_connection() == _game_connection_network_server)
	{
		/* (what every player has, sent to every client when any of it changes) */
		struct shadowrun_players_message message;
		short count;

		memset(&message, 0, sizeof(message));
		count = shadowrun_players_make(message.entries);
		if (count != sent_count ||
			(count > 0 && memcmp(message.entries, sent_entries, count * sizeof(message.entries[0]))))
		{
			sent_count = count;
			memcpy(sent_entries, message.entries, count * sizeof(message.entries[0]));
			if (count > 0)
			{
				distributed_send(&message, _distributed_message_shadowrun_players, count,
					(word)(sizeof(message.header) + count * sizeof(message.entries[0])),
					_distributed_to_clients_reliably);
			}
		}
	}
	else if (game_connection() == _game_connection_network_client)
	{
		/* (its players' anti-magic grenade choice, sent to the host when it
		changes) */
		short local_player_index;

		for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
		{
			long player_index = local_player_get_player_index(local_player_index);
			struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
			short chosen;
			struct shadowrun_purchase_message message;

			if (!player || player->unit_index == NONE)
				continue;
			chosen = shadowrun_unit_antimagic_grenade_chosen(player->unit_index) ? 1 : 0;
			if (chosen == sent_choices[local_player_index])
				continue;
			sent_choices[local_player_index] = chosen;
			memset(&message, 0, sizeof(message));
			message.kind = SHADOWRUN_MESSAGE_GRENADE_CHOICE;
			message.player_index = distributed_player_to_byte(player_index);
			message.gift_player_index = NO_PLAYER;
			message.chosen = (byte)chosen;
			message.item_index = NONE;
			distributed_send(&message, _distributed_message_shadowrun_purchase, 1, sizeof(message),
				_distributed_to_host_reliably);
		}
	}
}

boolean network_shadowrun_handles_message(word const *message, word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_shadowrun_purchase ||
		header.type == _distributed_message_shadowrun_players;
}

/* (over the sender's stream, whole: a client's purchase only for its own
players, the host's purchases only from the host) */
void network_shadowrun_handle_message(long machine_index, word const *data, word size)
{
	struct distributed_message_header header;

	if (network_connection_last_read_was_unreliable() || size < sizeof(header) || !shadowrun_network_playing())
		return;
	memcpy(&header, data, sizeof(header));
	if (GET_MESSAGE_TYPE(header.header) != 2)
		return;
	if (header.type == _distributed_message_shadowrun_purchase)
	{
		struct shadowrun_purchase_message message;
		long player_index;
		struct player_datum *player;

		if (machine_index == NONE || game_connection() != _game_connection_network_server ||
			size != sizeof(message) || header.count != 1)
			return;
		memcpy(&message, data, sizeof(message));
		player_index = distributed_player_from_byte(message.player_index);
		player = player_index != NONE ? player_try_and_get(player_index) : NULL;
		if (!player || !distributed_machine_has_player(machine_index, (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)))
			return;
		if (message.kind == SHADOWRUN_MESSAGE_GRENADE_CHOICE)
		{
			shadowrun_unit_antimagic_grenade_choose(player->unit_index, message.chosen != 0);
		}
		else if (message.kind < NUMBER_OF_SHADOWRUN_PURCHASES)
		{
			shadowrun_buy_menu_network_purchase(player_index, message.kind, message.item_index,
				message.gift_player_index != NO_PLAYER ? distributed_player_from_byte(message.gift_player_index) : NONE);
		}
	}
	else if (header.type == _distributed_message_shadowrun_players)
	{
		struct shadowrun_players_message message;
		short index;

		if (machine_index != NONE || game_connection() != _game_connection_network_client ||
			header.count == 0 || header.count > MAXIMUM_TRACKED_PLAYERS ||
			size != sizeof(message.header) + header.count * sizeof(message.entries[0]))
			return;
		memset(&message, 0, sizeof(message));
		memcpy(&message, data, size);
		for (index = 0; index < header.count; index++)
		{
			struct shadowrun_player_entry const *entry = &message.entries[index];
			struct shadowrun_player_purchases purchases;
			long player_index = distributed_player_from_byte(entry->player_index);
			short tech;

			if (player_index == NONE || entry->known_tech_count > MAXIMUM_SHADOWRUN_PLAYER_TECHS)
				continue;
			memset(&purchases, 0, sizeof(purchases));
			purchases.money = entry->money;
			purchases.known_tech_count = entry->known_tech_count;
			for (tech = 0; tech < MAXIMUM_SHADOWRUN_PLAYER_TECHS; tech++)
				purchases.known_techs[tech] = entry->known_techs[tech];
			purchases.mana_capacity_bonus = entry->mana_capacity_bonus;
			purchases.given_money = entry->given_money ? TRUE : FALSE;
			memcpy(purchases.known_powers, entry->known_powers, sizeof(purchases.known_powers));
			purchases.antimagic_grenades = entry->antimagic_grenades;
			shadowrun_player_purchases_set(player_index, &purchases);
		}
	}
}
