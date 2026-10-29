#include "game_profile.h"
#include <string.h>

GameProfile g_game_profile = GAME_BOTW;

bool game_select(const char *name) {
    if (!strcmp(name, "botw")) g_game_profile = GAME_BOTW;
    else if (!strcmp(name, "totk")) g_game_profile = GAME_TOTK;
    else return false;
    return true;
}

const char *game_profile_name(void) { return game_is_botw() ? "botw" : "totk"; }
