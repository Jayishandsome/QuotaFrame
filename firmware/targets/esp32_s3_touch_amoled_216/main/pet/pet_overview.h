#pragma once
/*
 * Overview page: the flying-orb pet reacts to the Codex and Claude quotas,
 * with one card per provider underneath. Plain C so the layout can also be
 * rendered by the desktop LVGL simulator.
 */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PET_NO_DATA, PET_OFFLINE, PET_PARTIAL, PET_ONLINE } pet_state_t;

typedef struct {
    pet_state_t state;
    bool short_present;   // 5-hour window known
    uint8_t short_left;   // percent of the 5-hour window left
    bool week_present;
    uint8_t week_used;    // percent of the weekly window used
    char countdown[16];   // time until the 5-hour reset, e.g. "2h 14m"
    bool pace_known;
    int16_t pace;         // 5-hour usage minus elapsed share, in points
} pet_provider_t;

typedef struct {
    bool connected;
    pet_provider_t provider[2];  // 0 = Codex, 1 = Claude
} pet_view_t;

/** Builds the page inside `parent` (480 x 429 area below the header). */
void pet_overview_create(lv_obj_t *parent);
void pet_overview_update(const pet_view_t *view);
/** Pauses the pet's animations while the page is hidden. */
void pet_overview_set_active(bool active);

#ifdef __cplusplus
}
#endif
