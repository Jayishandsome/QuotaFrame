#pragma once
/*
 * The flying-orb pet: white sphere, black visor, glowing eyes and floating
 * hands with light trails. Body and hands are pre-rendered images; eyes,
 * mouth, brows and effects are LVGL objects so moods can change cheaply.
 */
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ORB_IDLE,    // cyan, blinking, gentle float
    ORB_HAPPY,   // cyan smile, sparkles, bouncing
    ORB_THINK,   // amber, small eyes and pulsing dots
    ORB_SAD,     // coral, low eyes, drooping hands, no bob
    ORB_SLEEPY,  // grey, closed eyes, slow float
} orb_mood_t;

typedef struct orb orb_t;

/** Creates an orb in a 180 px (`large` false) or 240 px box at (0,0) of `parent`. */
orb_t *orb_create(lv_obj_t *parent, bool large);
lv_obj_t *orb_root(orb_t *orb);
/** Switches mood; restarting the same mood keeps animations running. */
void orb_set_mood(orb_t *orb, orb_mood_t mood);
orb_mood_t orb_mood(const orb_t *orb);
/** Stops (false) or restarts (true) every animation, e.g. while hidden. */
void orb_set_running(orb_t *orb, bool running);

#ifdef __cplusplus
}
#endif
