/*
 * menu_combo.h — Options-Menü per Start + Select (gedrückt halten).
 *
 * Handheld-Fork: Select allein öffnet das Menü nicht mehr (zu leicht
 * versehentlich gedrückt). Stattdessen öffnet Start + Select, ca. 1 s
 * gehalten, das Options-Menü — nur im Spiel, der Launcher bleibt unverändert.
 *
 *  - update_ui(): jeden Frame im draw_hook (patch ui_state.cpp), Render-Thread.
 *    Erkennt das Halten und öffnet das Menü.
 *  - filter_n64_input(): im get_input-Wrapper (main.cpp), Spiel-Thread.
 *    Hält Start vom Spiel fern, solange Select dazu gedrückt ist, damit die
 *    Kombination nicht die DK64-Pause auslöst.
 *
 * Schließen geht wie gehabt im Menü (Select / Zurück), Escape auf der
 * Tastatur öffnet weiterhin direkt.
 */
#pragma once

#include <cstdint>

namespace androidport::menucombo {

// Render-Thread. config_open: Options-Menü gerade sichtbar.
// Rückgabe true = Menü jetzt öffnen.
bool update_ui(bool config_open, bool game_started);

// Spiel-Thread. Entfernt bzw. verzögert das N64-START-Bit in *buttons.
void filter_n64_input(uint16_t* buttons);

} // namespace androidport::menucombo
