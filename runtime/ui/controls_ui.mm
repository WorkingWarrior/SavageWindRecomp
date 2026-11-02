// Control remapping editor (runtime/input_bind.h), shared by the in-game menu's Controls tab (menu.mm) and the start
// screen's Controls page (launcher.mm): a table of Switch inputs with the controller button and the key bound to
// each. Select a binding, then press the new button / key (Esc or 5 s without a press cancels; a button already in
// use swaps over). Drawn in whichever ImGui context is current.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "imgui.h"

extern "C" {
#include "../input_bind.h"
#include "../input_status.h"
#include "../motion.h"
#include "../pad_policy.h"
#include "../settings.h"
#include "../vibration.h"
}
extern "C" bool control_enabled(void);  // runtime/control.c: is the test endpoint on (SAVAGEWIND_CONTROL)?

static void cell(int dev, int slot, int fam, int cap_dev, int cap_slot, int ms_left) {
    char lbl[96];
    bool waiting = cap_dev == dev && cap_slot == slot;
    if (waiting) snprintf(lbl, sizeof lbl, "%s  %d##%d_%d", tr(dev == BIND_DEV_PAD ? "Press a button..." : "Press a key..."),
                          (ms_left + 999) / 1000, dev, slot);
    else snprintf(lbl, sizeof lbl, "%s##%d_%d", tr(dev == BIND_DEV_PAD ? bind_pad_label(g_bind_pad[slot], fam) :
                                                    bind_key_label(g_bind_key[slot])), dev, slot);
    // changed from the default: gold text; the same button / key on two inputs (a hand-edited savagewind.ini): red
    const int16_t *t = dev == BIND_DEV_PAD ? g_bind_pad : g_bind_key;
    int pushed = 0;
    bool dup = false;
    for (int i = 0; i < BIND_KEY_COUNT; i++)
        dup |= i != slot && (dev == BIND_DEV_KEY ? bind_key_same_context(i, slot) : bind_pad_slot(i)) && t[i] == t[slot] &&
               t[slot] > (dev == BIND_DEV_PAD ? -1 : 0);
    if (waiting) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.42f, 0.12f, 1)); pushed++; }
    else if (dup) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.16f, 0.12f, 1)); pushed++; }
    if (!bind_is_default_slot(dev, slot)) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.82f, 0.38f, 1)); pushed++; }
    if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) {
        if (waiting) bind_capture_cancel();
        else bind_capture_begin(dev, slot);
    }
    ImGui::PopStyleColor(pushed);
    if (waiting && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);  // (a capture started by a test hook)
}

// Which controller does what, and how each part of it is doing: everything here is live, so a player can see why motion or
// vibration isn't there.
static void controller_panel(int fam) {
    InputStatus st;
    MotionStatus ms;
    input_status(&st);
    motion_status(&ms);
    bool changed = false;
    if (ImGui::CollapsingHeader(tr("Controllers"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::RadioButton(tr("Automatic: the one used last"), !g_settings.pad_preferred[0])) {
            g_settings.pad_preferred[0] = 0;
            changed = true;
        }
        for (int i = 0; i < st.count && i < 8; i++) {
            const PadRow &r = st.rows[i];
            if (r.steam_virtual && st.pair == PAD_PAIR_OK) continue;  // (half of the real one, in its line)
            char line[160];
            snprintf(line, sizeof line, "%s##%d", r.name, i);
            ImGui::PushID(i);
            if (ImGui::RadioButton(line, !strcmp(g_settings.pad_preferred, r.guid))) {
                snprintf(g_settings.pad_preferred, sizeof g_settings.pad_preferred, "%s", r.guid);
                changed = true;
            }
            ImGui::PopID();
        }
        const char *in = NULL, *mo = NULL, *ha = NULL;
        bool virt_in = false;
        for (int i = 0; i < st.count && i < 8; i++) {
            if (st.rows[i].input) in = st.rows[i].name, virt_in = st.rows[i].steam_virtual;
            if (st.rows[i].sensors) mo = st.rows[i].name;
            if (st.rows[i].haptics) ha = st.rows[i].name;
        }
        ImGui::Text("%s: %s%s", tr("Buttons and sticks"), in ? in : tr("none"), virt_in ? " (Steam)" : "");
        ImGui::Text("%s: %s", tr("Motion"), mo ? mo : tr("none"));
        ImGui::Text("%s: %s", tr("Vibration"), ha ? ha : tr("none"));
        if (st.steam_launch) {
            static const char *const modes[STEAM_INPUT_COUNT] = {"Automatic", "The controller itself", "Steam's layout"};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
            if (ImGui::BeginCombo(tr("Buttons under Steam"), tr(modes[g_settings.steam_input]))) {
                for (int i = 0; i < STEAM_INPUT_COUNT; i++)
                    if (ImGui::Selectable(tr(modes[i]), g_settings.steam_input == i)) g_settings.steam_input = i, changed = true;
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("%s", st.pair == PAD_PAIR_OK ? (st.virtual_live ? tr("Steam's virtual pad works.") : tr("Steam's virtual pad has sent nothing yet: the controller itself is used."))
                                    : st.pair == PAD_PAIR_AMBIGUOUS ? tr("Several controllers under Steam can't be told apart: each is used by itself.") : tr("Started through Steam."));
            if (!ms.sensors) ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "%s", tr("Steam's virtual pad has no motion sensors."));
        }
    }
    if (ImGui::CollapsingHeader(tr("Motion"))) {
        bool on = !g_settings.motion_off;
        if (ImGui::Checkbox(tr("Motion controls"), &on)) g_settings.motion_off = !on, changed = true;
#ifdef __linux__
        static const char *const srcs[MOTION_SOURCE_COUNT] = {"Automatic", "The controller itself", "DSU server"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::BeginCombo(tr("Motion source"), tr(srcs[g_settings.motion_source]))) {
            for (int i = 0; i < MOTION_SOURCE_COUNT; i++)
                if (ImGui::Selectable(tr(srcs[i]), g_settings.motion_source == i)) g_settings.motion_source = i, changed = true;
            ImGui::EndCombo();
        }
#endif
        if (ImGui::Button(tr("Recalibrate"))) motion_recalibrate();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ms.calibrating ? tr("Hold the controller still...") : tr("Lay it down or hold it still, then press."));
        ImGui::Text("%s  %s", ms.source == MOTION_SOURCE_DSU ? "DSU" : ms.source == MOTION_SOURCE_SDL ? "SDL" : tr("none"),
                    ms.stalled ? tr("stopped: turning it on again") : "");
        ImGui::Text("%u Hz, %.0f ms", ms.rate, ms.age_ms < 0 ? 0 : ms.age_ms);
        ImGui::Text("gyro %+.3f %+.3f %+.3f rad/s", ms.gyro[0], ms.gyro[1], ms.gyro[2]);
        ImGui::Text("accel %+.2f %+.2f %+.2f m/s2", ms.accel[0], ms.accel[1], ms.accel[2]);
        ImGui::Text("bias %+.2f %+.2f %+.2f deg/s", ms.bias[0], ms.bias[1], ms.bias[2]);
        if (ms.recovered) ImGui::TextDisabled("%s: %d", tr("Sensor stops recovered"), ms.recovered);
    }
    if (ImGui::CollapsingHeader(tr("Vibration"))) {
        bool on = !g_settings.vibration_off;
        if (ImGui::Checkbox(tr("Vibration"), &on)) g_settings.vibration_off = !on, changed = true;
        int pct = (int)(g_settings.vibration_intensity * 100 + 0.5f);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::SliderInt(tr("Intensity"), &pct, 0, 200, "%d%%")) g_settings.vibration_intensity = (float)pct / 100, changed = true;
        static const char *const q[VIBRATION_COUNT] = {"Automatic", "Standard", "Native"};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::BeginCombo(tr("Quality"), tr(q[g_settings.vibration_quality]))) {
            for (int i = 0; i < VIBRATION_COUNT; i++)
                if (ImGui::Selectable(tr(q[i]), g_settings.vibration_quality == i)) g_settings.vibration_quality = i, changed = true;
            ImGui::EndCombo();
        }
        if (ImGui::Button(tr("Test vibration"))) vibration_test();
        int r = vibration_route();
        static const char *const routes[] = {"none", "Motors", "Steam Controller tone", "Nintendo HD rumble"};
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr(routes[r]));
    }
    if (ImGui::CollapsingHeader(tr("Diagnostics"))) {
        uint32_t held;
        int16_t ax[6];
        input_live(&held, ax);
        char line[400];
        int n = 0;
        line[0] = 0;
        for (int i = 0; i < PADB_HOST_COUNT && n < (int)sizeof line - 40; i++)
            if (held >> i & 1) n += snprintf(line + n, sizeof line - (size_t)n, "%s  ", bind_pad_label(i, fam));
        ImGui::Text("%s: %s", tr("Held"), line);
        ImGui::Text("L %6d %6d   R %6d %6d   T %6d %6d", ax[0], ax[1], ax[2], ax[3], ax[4], ax[5]);
        if (st.error[0]) ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "%s", st.error);
        ImGui::TextDisabled("%s", st.steam_launch ? tr("Started through Steam.") : tr("Started outside Steam."));
        if (st.steam_controller && !st.steam_launch) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", tr("If a button opens Steam's on-screen keyboard here, that is Steam's Desktop Layout. Change it in Steam's Controller settings."));
            ImGui::PopTextWrapPos();
        }
    }
    if (changed) {
        input_settings_changed();
        settings_save();
    }
    ImGui::Spacing();
}

extern "C" void controls_bindings_draw(float scale) {
    (void)scale;
    int fam = bind_label_family();
    controller_panel(fam);
    static const char *const fam_name[] = {"Nintendo", "Xbox", "PlayStation", "Steam Controller"};
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    const char *pad_name = ids && n > 0 ? SDL_GetGamepadNameForID(ids[0]) : NULL;
    char line[300];
    if (n > 0) snprintf(line, sizeof line, tr("Controller: %s  (%s button names)"), pad_name ? pad_name : tr("connected"), fam_name[fam & 3]);
    else snprintf(line, sizeof line, tr("No controller connected  (%s button names shown)"), fam_name[fam & 3]);
    SDL_free(ids);
    ImGui::TextUnformatted(line);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr("Select a binding, then press the new button or key. Esc (or 5 seconds) cancels. A button "
                                 "that's already in use swaps with the old one."));
    ImGui::PopTextWrapPos();

    int cd, cs, ms;
    bind_capture_active(&cd, &cs, &ms);
    // a finished capture: say what happened, keep savagewind.ini current
    static int seen_serial = -1;
    static char msg[400];
    int rdev, rslot, rother, serial;
    int kind = bind_last_result(&rdev, &rslot, &rother, &serial);
    if (seen_serial < 0) seen_serial = serial;
    if (serial != seen_serial) {
        seen_serial = serial;
        const char *now = rslot < 0 ? "" : tr(rdev == BIND_DEV_PAD ? bind_pad_label(g_bind_pad[rslot], fam) : bind_key_label(g_bind_key[rslot]));
        if (kind == 5)
            snprintf(msg, sizeof msg, "%s", tr(rdev == BIND_DEV_PAD ? "Controller bindings reset to the defaults."
                                                                     : "Keyboard bindings reset to the defaults."));
        else if (kind == 1) snprintf(msg, sizeof msg, tr("%s is now %s."), tr(bind_input_name(rslot)), now);
        else if (kind == 2)
            snprintf(msg, sizeof msg, tr("%s is now %s; %s moved to %s (swapped)."), tr(bind_input_name(rslot)), now,
                     tr(bind_input_name(rother)), tr(rdev == BIND_DEV_PAD ? bind_pad_label(g_bind_pad[rother], fam) : bind_key_label(g_bind_key[rother])));
        else if (kind == 3) snprintf(msg, sizeof msg, tr("Cancelled: %s unchanged."), tr(bind_input_name(rslot)));
        else if (kind == 4) snprintf(msg, sizeof msg, tr("Nothing pressed: %s unchanged."), tr(bind_input_name(rslot)));
        if (kind == 1 || kind == 2 || kind == 5) settings_save();
    }
    if (msg[0]) ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "%s", msg);

    if (ImGui::BeginTable("##binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Switch", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Controller"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(tr("Keyboard"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableHeadersRow();
        // in the order a player thinks of them: face buttons, shoulders, Plus / Minus, d-pad, sticks
        static const int order[] = {BIND_A, BIND_B, BIND_X, BIND_Y, BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_PLUS,
                                    BIND_MINUS, BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT, BIND_LSTICK, BIND_RSTICK,
                                    BIND_LS_UP, BIND_LS_DOWN, BIND_LS_LEFT, BIND_LS_RIGHT, BIND_RS_UP, BIND_RS_DOWN,
                                    BIND_RS_LEFT, BIND_RS_RIGHT, BIND_WALK, BIND_TILT, BIND_GYRO_UP, BIND_GYRO_DOWN,
                                    BIND_GYRO_LEFT, BIND_GYRO_RIGHT, BIND_GYRO_TURN_LEFT, BIND_GYRO_TURN_RIGHT,
                                    BIND_GYRO_CENTER, BIND_FLY};
        const int rows = (int)(sizeof order / sizeof *order) - (control_enabled() ? 0 : 1);  // (the fly row: test builds only)
        for (int row = 0; row < rows; row++) {
            const int slot = order[row];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(bind_input_name(slot)));
            ImGui::TableNextColumn();
            if (bind_pad_slot(slot)) cell(BIND_DEV_PAD, slot, fam, cd, cs, ms);
            else ImGui::TextDisabled("%s", tr(slot == BIND_WALK ? "Tilt the stick" : slot == BIND_TILT ? "Motion sensors" :
                                              bind_key_gyro(slot) ? "Motion sensors" : slot < BIND_RS_UP ? "Left stick" : "Right stick"));
            ImGui::TableNextColumn();
            cell(BIND_DEV_KEY, slot, fam, cd, cs, ms);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(bind_is_default(BIND_DEV_PAD));
    if (ImGui::Button(tr("Reset controller to defaults"))) { bind_capture_cancel(); bind_reset(BIND_DEV_PAD); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(bind_is_default(BIND_DEV_KEY));
    if (ImGui::Button(tr("Reset keyboard to defaults"))) { bind_capture_cancel(); bind_reset(BIND_DEV_KEY); }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s %s", tr("Bindings apply to the game only. Menus keep the fixed layout (right face button / Enter "
                                 "select, bottom face button / Esc back), and the in-game menu always opens with F1 or Esc, or "
                                 "the controller's Back + Start held together, or its Home / Guide button - so this page can't be "
                                 "locked out. The game's button icons follow the controller bindings."),
                       tr("In the title, pause and map menus, unbound arrow keys act as the left stick and Enter selects."));
    ImGui::PopTextWrapPos();
}
