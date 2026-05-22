// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#ifndef _DASH_SETTINGS_H
#define _DASH_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lithiumx.h"

#define DASH_SETTINGS_VERSION 0x03
#define DASH_SETTINGS_MAGIC (0xBEEF0000+DASH_SETTINGS_VERSION)
typedef struct dash_settings {
    unsigned int magic;
    bool use_fahrenheit;
    bool auto_launch_dvd;
    bool show_debug_info;
    int startup_page_index;
    int theme_colour;
    int max_recent_items;
    int items_per_row;
    char earliest_recent_date[20]; //"YYYY-MM-DD HH:MM:SS"
    char sort_strings[4096]; //Like "Games=1 Apps=1\0" etc.
    // v0x03: achtergrondkleur los van theme colour
    // Bit 24 (0x01000000) = "ingesteld" vlag. Zonder deze vlag wordt de
    // standaard theme gradient gebruikt, ongeacht de laagste 24 bits.
    // Sla op als: 0x01RRGGBB = aangepaste kleur, 0x00000000 = niet ingesteld.
    // Gebruik DASH_BG_COLOUR_SET/GET macros om de vlag te beheren.
    int background_colour;
} dash_settings_t;

// Achtergrondkleur helpers
#define DASH_BG_COLOUR_FLAG     (0x01000000)
#define DASH_BG_COLOUR_IS_SET(v) ((v) & DASH_BG_COLOUR_FLAG)
#define DASH_BG_COLOUR_PACK(r,g,b) (DASH_BG_COLOUR_FLAG | ((r)<<16) | ((g)<<8) | (b))
#define DASH_BG_COLOUR_R(v)  (((v) >> 16) & 0xFF)
#define DASH_BG_COLOUR_G(v)  (((v) >>  8) & 0xFF)
#define DASH_BG_COLOUR_B(v)  ( (v)        & 0xFF)

void dash_settings_open(void);
void dash_settings_apply(bool confirm_box);
void dash_settings_read(void);

#ifdef __cplusplus
}
#endif

#endif
