// firefight.c

const Gametype_t* get_firefight_gametype() {
    static const Gametype_t firefight = {
        .name = "Firefight",
        // The initializer now uses the defined ruleset
        .initialize = (void (*)(void *))&firefight_init, 
        .handle_game_loop = &firefight_loop,
        .cleanup = NULL // No cleanup needed for this module example
    };
    return &firefight;
}

// The initialization function now needs access to the ruleset
void firefight_init(void *context) {
    printf("--- Firefight Gametype Initializing ---\n");
    // 1. Pass the rule structure data to the global game state/context
    // 2. Load assets specific to high-combat scenarios (e.g., more ammo, different spawn points).
}

int firefight_loop(void *context) {
    printf("--- Firefight Game Loop Running ---\n");
    // Gameplay logic uses the rules defined in 'firefight_default_rules'.
    return 1; 
}


// Global definition for the Firefight rule set
const struct game_variant_options firefight_default_rules = {
    /* time_limit */ 15;                                  // Example: 15-minute round limit
	/* friendly_fire */ 1;                                // Friendly fire is allowed (1)
	/* friendly_fire_penalty */ 5;                         // Penalty of 5 seconds for team kills
	/* vehicle_respawn_time */ 0;                          // Vehicles do not respawn quickly
    /* auto_team_balance */ true;                          // Should the system balance teams? Yes.
	/* radar_players */ 0;                                 // All players visible (all)
	/* red's and blue's */ {1, 1};                         // Both teams have vehicles
	/* vehicle_counts */ {{2}, {2}};                       // Two vehicles per team
    /* loadout */ _loadout_category;                        // Use category default loadouts
    /* primary_weapon */ _loadout_weapon_assault_rifle;     // Example: Start with an Assault Rifle
	/* secondary_weapon */ _loadout_weapon_pistol;         // Example: And a Pistol
	/* no_map_weapons */ false;                             // Weapons spawn on map
};

