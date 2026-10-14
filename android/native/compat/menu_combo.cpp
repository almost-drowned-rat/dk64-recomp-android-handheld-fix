/*
 * menu_combo.cpp — Options-Menü per Start + Select halten. Siehe menu_combo.h.
 */
#include "menu_combo.h"

#include <atomic>
#include <chrono>
#include <android/log.h>

#include "SDL2/SDL.h"
#include "recompinput/input_state.h"

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "DK64Recomp", __VA_ARGS__)

namespace androidport::menucombo {

namespace {

using clock = std::chrono::steady_clock;

// Haltedauer für die Kombination.
constexpr auto kHoldTime = std::chrono::milliseconds(1000);
// Start geht erst nach dieser Zeit ans Spiel: Select darf so lange nach Start
// kommen, ohne dass die DK64-Pause ausgelöst wird.
constexpr auto kStartDelay = std::chrono::milliseconds(100);
// Kurzer Tipp auf Start (kürzer als kStartDelay): Start wird nach dem
// Loslassen so lange ans Spiel gegeben, damit das Spiel ihn sicher sieht.
constexpr auto kStartPulse = std::chrono::milliseconds(100);

// N64 START (siehe n64_button_values in recompinput).
constexpr uint16_t kN64Start = 0x1000;

bool physical(SDL_GameControllerButton button) {
    return recompinput::get_input_digital(0, recompinput::InputField::controller_digital(button));
}

// Render-Thread.
clock::time_point g_combo_since{};
bool g_combo_held = false;
bool g_armed = true; // erst nach Loslassen beider Tasten erneut auslösbar

// Spiel-Thread.
clock::time_point g_start_onset{};
clock::time_point g_pulse_until{};
bool g_start_down = false;
bool g_start_tainted = false;   // Select kam während dieses Start-Drucks dazu
bool g_start_forwarded = false; // Start wurde in diesem Druck schon ans Spiel gegeben

} // namespace

bool update_ui(bool config_open, bool game_started) {
    const bool start = physical(SDL_CONTROLLER_BUTTON_START);
    const bool back = physical(SDL_CONTROLLER_BUTTON_BACK);
    const auto now = clock::now();

    if (!start && !back) {
        g_armed = true;
    }

    // Im Menü (oder im Launcher) nie auslösen: sonst würde dieselbe
    // Kombination, die das Menü per Select schließt, es nach 1 s wieder öffnen.
    if (config_open || !game_started) {
        g_armed = false;
        g_combo_held = false;
        return false;
    }

    if (!(start && back)) {
        g_combo_held = false;
        return false;
    }

    if (!g_combo_held) {
        g_combo_held = true;
        g_combo_since = now;
    }

    if (g_armed && now - g_combo_since >= kHoldTime) {
        g_armed = false;
        ALOGI("menu combo: Start+Select gehalten -> Options-Menü");
        return true;
    }
    return false;
}

void filter_n64_input(uint16_t* buttons) {
    const bool start = (*buttons & kN64Start) != 0;
    const bool back = physical(SDL_CONTROLLER_BUTTON_BACK);
    const auto now = clock::now();
    bool forward = false;

    if (start) {
        if (!g_start_down) {
            g_start_down = true;
            g_start_onset = now;
            g_start_tainted = false;
            g_start_forwarded = false;
        }
        if (back) {
            g_start_tainted = true;
        }
        forward = !g_start_tainted && now - g_start_onset >= kStartDelay;
        g_start_forwarded |= forward;
    }
    else if (g_start_down) {
        g_start_down = false;
        if (!g_start_tainted && !g_start_forwarded) {
            g_pulse_until = now + kStartPulse;
        }
    }

    if (!back && now < g_pulse_until) {
        forward = true;
    }

    if (forward) {
        *buttons |= kN64Start;
    }
    else {
        *buttons &= ~kN64Start;
    }
}

} // namespace androidport::menucombo
