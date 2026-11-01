#ifndef NETWORK_SHADOWRUN_H
#define NETWORK_SHADOWRUN_H
#include "cseries.h"
/* The Shadowrun prototype's buy menu and anti-magic grenades in a network
game (network_shadowrun.c): a client's purchases are made by the host, and
what each player has bought is the host's. */

/* a purchase's kind: an item of the menu, a gift of money to a teammate, a
magazine for the weapon in hand */
enum
{
	_shadowrun_purchase_item,
	_shadowrun_purchase_gift,
	_shadowrun_purchase_magazine,
	NUMBER_OF_SHADOWRUN_PURCHASES
};

void network_shadowrun_reset(void);
/* each frame: the host sends what the players have when it changes; a
client sends its players' anti-magic grenade choice when it changes */
void network_shadowrun_update(void);
/* the host, to a client ready to play: what every player has */
void network_shadowrun_send(long machine_index);
/* a client: its player's purchase, which the host makes */
void network_shadowrun_send_purchase(long player_index, short kind, short item_index, long gift_player_index);
boolean network_shadowrun_handles_message(word const *message, word size);
void network_shadowrun_handle_message(long machine_index, word const *message, word size);
#endif
