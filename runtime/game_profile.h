// Experimental game selection is independent of the BOTW version/site tables.
#pragma once
#include <stdbool.h>

#define TOTK_100_BUILD_ID "082ce09b06e33a12"
#define TOTK_100_SDK_LSE_FLAG_OFF 0xdd5780ull  // read-only experiment observation, not a forced feature flag

typedef enum { GAME_BOTW, GAME_TOTK } GameProfile;
#ifdef __cplusplus
extern "C" {
#endif
extern GameProfile g_game_profile;
bool game_select(const char *name);
const char *game_profile_name(void);
#ifdef __cplusplus
}
#endif

static inline bool game_is_botw(void) { return g_game_profile == GAME_BOTW; }
