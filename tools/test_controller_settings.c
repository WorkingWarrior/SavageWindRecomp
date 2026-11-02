// The controller settings in savagewind.ini: an old file gets the defaults, values survive a save, nonsense doesn't, and a
// bind_pad_fly someone chose stays while the default moved off "...". make test-controller-settings
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/settings.c"
#include "../runtime/hle/bindings.c"

const TextLanguage g_text_languages[] = {{"Auto", NULL, NULL}};
const VoiceLanguage g_voice_languages[] = {{"Auto", NULL}};
const int g_ntext_languages = 1, g_nvoice_languages = 1;
int input_pad_family(void) { return 0; }
bool fsr3_upscaler_enabled(void) { return false; }

int main(int argc, char **argv) {
    assert(argc == 3);
    setenv("SAVAGEWIND_SETTINGS", argv[1], 1);
    const char *mode = argv[2];
    settings_load();
    if (!strcmp(mode, "old")) {
        assert(!g_settings.motion_off && !g_settings.vibration_off);
        assert(g_settings.vibration_intensity == 1 && g_settings.vibration_quality == VIBRATION_AUTO);
        assert(g_settings.motion_source == MOTION_SOURCE_AUTO && g_settings.steam_input == STEAM_INPUT_AUTO);
        assert(!g_settings.pad_preferred[0]);
        assert(g_bind_pad[BIND_FLY] == SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
        g_settings.motion_off = g_settings.vibration_off = true;
        g_settings.vibration_intensity = .75f, g_settings.vibration_quality = VIBRATION_NATIVE;
        g_settings.motion_source = MOTION_SOURCE_DSU, g_settings.steam_input = STEAM_INPUT_STEAM;
        strcpy(g_settings.pad_preferred, "03002854de2800000413000002006800");
        settings_save();
    } else if (!strcmp(mode, "saved")) {
        assert(g_settings.motion_off && g_settings.vibration_off);
        assert(g_settings.vibration_intensity == .75f && g_settings.vibration_quality == VIBRATION_NATIVE);
        assert(g_settings.motion_source == MOTION_SOURCE_DSU && g_settings.steam_input == STEAM_INPUT_STEAM);
        assert(!strcmp(g_settings.pad_preferred, "03002854de2800000413000002006800"));
    } else if (!strcmp(mode, "fly")) {
        assert(g_bind_pad[BIND_FLY] == SDL_GAMEPAD_BUTTON_MISC1);
    } else {  // nonsense
        assert(g_settings.vibration_intensity == 1 && g_settings.vibration_quality == VIBRATION_AUTO);
        assert(g_settings.motion_source == MOTION_SOURCE_AUTO && g_settings.steam_input == STEAM_INPUT_AUTO);
    }
    printf("controller settings: %s PASS\n", mode);
}
