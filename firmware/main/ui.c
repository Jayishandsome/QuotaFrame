#include "ui.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "assets/orb_assets.h"

LV_FONT_DECLARE(font_text_22);
LV_FONT_DECLARE(font_ui_16);
LV_FONT_DECLARE(font_ui_20);
LV_FONT_DECLARE(font_ui_28);
LV_FONT_DECLARE(font_num_16);
LV_FONT_DECLARE(font_num_64);

#define C_TEXT 0xF2F2F2
#define C_GREY 0x8C8C8C
#define C_GREY2 0xA3A3A3
#define C_DIM 0x7A7A7A
#define C_CYAN 0x5CE8F0
#define C_AMBER 0xFFC069
#define C_CORAL 0xFF7A6B
#define C_CORAL_TEXT 0xFF9A8E
#define C_BOX 0x0C0F11
#define C_BOX_LINE 0x2E3438

#define BRIGHT_ON 80
#define BRIGHT_SLEEP 25
#define SLEEP_AFTER_MS 60000
#define PANEL_OFF_AFTER_MS 300000
#define CONFIRM_MS 10000
#define DONE_MS 3500
#define HISTORY_IDLE_MS 20000

static const ui_port_t *port;

/* ------------------------------------------------------------------ orb -- */

typedef enum { MOOD_IDLE, MOOD_LISTEN, MOOD_THINK, MOOD_HAPPY, MOOD_SAD } mood_t;
enum { ACC_CYAN, ACC_AMBER, ACC_CORAL };

typedef struct {
    bool large;
    float k;  // px per design unit (design box is 200 units)
    lv_obj_t *root, *shadow, *floatbox, *hand_l, *hand_r, *body;
    lv_obj_t *eye[2], *bar[3], *arc[2], *brow[2], *dot[3], *sparkles, *ripple[2];
    lv_point_precise_t brow_pts[2][2];
    int32_t eye_cy, eye_h;
} orb_t;

static const orb_part_t *const BODY[2][3] = {
    {&orb_body_cyan_M, &orb_body_amber_M, &orb_body_coral_M},
    {&orb_body_cyan_L, &orb_body_amber_L, &orb_body_coral_L},
};
static const orb_part_t *const HAND_L[2][3] = {
    {&orb_hand_l_cyan_M, &orb_hand_l_amber_M, &orb_hand_l_coral_M},
    {&orb_hand_l_cyan_L, &orb_hand_l_amber_L, &orb_hand_l_coral_L},
};
static const orb_part_t *const HAND_R[2][3] = {
    {&orb_hand_r_cyan_M, &orb_hand_r_amber_M, &orb_hand_r_coral_M},
    {&orb_hand_r_cyan_L, &orb_hand_r_amber_L, &orb_hand_r_coral_L},
};

static int32_t U(const orb_t *o, float v) { return (int32_t)lroundf(v * o->k); }

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *rect(lv_obj_t *parent, uint32_t color, int32_t radius)
{
    lv_obj_t *obj = plain(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

static void glow(lv_obj_t *obj, uint32_t color, int32_t width)
{
    lv_obj_set_style_shadow_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(obj, width, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_60, 0);
}

static void place_part(lv_obj_t *img, const orb_part_t *part, int32_t dx, int32_t dy)
{
    lv_image_set_src(img, part->img);
    lv_obj_set_pos(img, part->x + dx, part->y + dy);
}

static void anim_start(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                       uint32_t reverse_ms, uint32_t delay, uint32_t repeat_delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_reverse_duration(&a, reverse_ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, repeat_delay);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void exec_translate_y(void *obj, int32_t v) { lv_obj_set_style_translate_y(obj, v, 0); }
static void exec_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }

static void exec_hand_l(void *obj, int32_t v)
{
    orb_t *o = lv_obj_get_user_data(obj);
    lv_obj_set_style_translate_x(obj, -U(o, 3) * v / 1000, 0);
    lv_obj_set_style_translate_y(obj, -U(o, 7) * v / 1000, 0);
}

static void exec_hand_r(void *obj, int32_t v)
{
    orb_t *o = lv_obj_get_user_data(obj);
    lv_obj_set_style_translate_x(obj, U(o, 3) * v / 1000, 0);
    lv_obj_set_style_translate_y(obj, -U(o, 5) * v / 1000, 0);
}

static void exec_blink(void *var, int32_t pct)
{
    orb_t *o = var;
    int32_t h = LV_MAX(2, o->eye_h * pct / 100);
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_height(o->eye[i], h);
        lv_obj_set_y(o->eye[i], o->eye_cy - h / 2);
    }
}

static void exec_bar(void *obj, int32_t pct)
{
    orb_t *o = lv_obj_get_user_data(obj);
    int32_t h = U(o, 30) * pct / 100;
    lv_obj_set_height(obj, h);
    lv_obj_set_y(obj, U(o, 107) - h / 2);
}

static void exec_ripple(void *obj, int32_t v)
{
    orb_t *o = lv_obj_get_user_data(obj);
    int32_t d = U(o, 120) * (850 + 400 * v / 1000) / 1000;
    lv_obj_set_size(obj, d, d);
    lv_obj_set_pos(obj, U(o, 100) - d / 2, U(o, 116) - d / 2);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)(230 * (1000 - v) / 1000), 0);
}

static void orb_stop(orb_t *o)
{
    lv_anim_delete(o, NULL);
    lv_anim_delete(o->floatbox, NULL);
    lv_anim_delete(o->hand_l, NULL);
    lv_anim_delete(o->hand_r, NULL);
    lv_anim_delete(o->sparkles, NULL);
    for (int i = 0; i < 3; ++i) {
        lv_anim_delete(o->bar[i], NULL);
        lv_anim_delete(o->dot[i], NULL);
    }
    for (int i = 0; i < 2; ++i) lv_anim_delete(o->ripple[i], NULL);
    lv_obj_set_style_translate_y(o->floatbox, 0, 0);
    lv_obj_set_style_translate_x(o->hand_l, 0, 0);
    lv_obj_set_style_translate_y(o->hand_l, 0, 0);
    lv_obj_set_style_translate_x(o->hand_r, 0, 0);
    lv_obj_set_style_translate_y(o->hand_r, 0, 0);
}

static void orb_eyes(orb_t *o, float x, float y, float w, float h, uint32_t color)
{
    o->eye_h = U(o, h);
    o->eye_cy = U(o, y + h / 2);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *e = o->eye[i];
        lv_obj_set_size(e, U(o, w), o->eye_h);
        lv_obj_set_pos(e, U(o, x + 24 * i), U(o, y));
        lv_obj_set_style_bg_color(e, lv_color_hex(color), 0);
        glow(e, color, U(o, 10));
        lv_obj_remove_flag(e, LV_OBJ_FLAG_HIDDEN);
    }
}

static void orb_set_mood(orb_t *o, mood_t mood)
{
    orb_stop(o);
    int acc = mood == MOOD_THINK ? ACC_AMBER : mood == MOOD_SAD ? ACC_CORAL : ACC_CYAN;
    int32_t pose_x = 0, pose_y = 0;
    if (mood == MOOD_HAPPY) { pose_x = -U(o, 6); pose_y = -U(o, 10); }
    if (mood == MOOD_SAD) { pose_x = U(o, 3); pose_y = U(o, 8); }
    place_part(o->body, BODY[o->large][acc], 0, 0);
    place_part(o->hand_l, HAND_L[o->large][acc], pose_x, pose_y);
    place_part(o->hand_r, HAND_R[o->large][acc], -pose_x, pose_y);

    lv_obj_t *optional[] = {o->eye[0], o->eye[1], o->bar[0], o->bar[1], o->bar[2], o->arc[0], o->arc[1],
                            o->brow[0], o->brow[1], o->dot[0], o->dot[1], o->dot[2], o->sparkles,
                            o->ripple[0], o->ripple[1]};
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i) lv_obj_add_flag(optional[i], LV_OBJ_FLAG_HIDDEN);

    if (mood != MOOD_SAD) {
        anim_start(o->hand_l, exec_hand_l, 0, 1000, 1100, 1100, 0, 0, NULL);
        anim_start(o->hand_r, exec_hand_r, 0, 1000, 1300, 1300, 0, 0, NULL);
    }
    if (mood == MOOD_HAPPY) {
        anim_start(o->floatbox, exec_translate_y, -U(o, 14), 0, 900, 0, 0, 500, lv_anim_path_bounce);
    } else {
        anim_start(o->floatbox, exec_translate_y, 0, -U(o, 10), 1600, 1600, 0, 0, NULL);
    }

    switch (mood) {
    case MOOD_IDLE:
        orb_eyes(o, 83, 93, 10, 28, C_CYAN);
        anim_start(o, exec_blink, 100, 10, 110, 110, 1200, 3800, lv_anim_path_linear);
        break;
    case MOOD_LISTEN:
        for (int i = 0; i < 3; ++i) {
            lv_obj_remove_flag(o->bar[i], LV_OBJ_FLAG_HIDDEN);
            anim_start(o->bar[i], exec_bar, 35, 100, 450, 450, 250 * i, 0, NULL);
        }
        for (int i = 0; i < 2; ++i) {
            lv_obj_remove_flag(o->ripple[i], LV_OBJ_FLAG_HIDDEN);
            anim_start(o->ripple[i], exec_ripple, 0, 1000, 1600, 0, 800 * i, 0, lv_anim_path_ease_out);
        }
        break;
    case MOOD_THINK:
        orb_eyes(o, 83.5f, 92, 9, 18, C_AMBER);
        for (int i = 0; i < 3; ++i) {
            lv_obj_remove_flag(o->dot[i], LV_OBJ_FLAG_HIDDEN);
            anim_start(o->dot[i], exec_opa, 70, 255, 600, 600, 300 * i, 0, NULL);
        }
        break;
    case MOOD_HAPPY:
        lv_obj_remove_flag(o->arc[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->arc[1], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->sparkles, LV_OBJ_FLAG_HIDDEN);
        anim_start(o->sparkles, exec_opa, 80, 255, 600, 600, 0, 0, NULL);
        break;
    case MOOD_SAD:
        orb_eyes(o, 83, 108, 10, 14, C_CORAL);
        lv_obj_remove_flag(o->brow[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->brow[1], LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static lv_obj_t *orb_arc(orb_t *o, float cx)
{
    const float r = 9, w = 7;
    int32_t size = U(o, 2 * (r + w / 2));
    lv_obj_t *arc = lv_arc_create(o->floatbox);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, U(o, cx) - size / 2, U(o, 114.5f) - size / 2);
    lv_arc_set_bg_angles(arc, 180, 360);
    lv_arc_set_angles(arc, 180, 360);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(C_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, U(o, w), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

static void orb_create(orb_t *o, lv_obj_t *parent, bool large)
{
    memset(o, 0, sizeof(*o));
    o->large = large;
    int32_t size = large ? 240 : 180;
    o->k = size / 200.0f;
    o->root = plain(parent);
    lv_obj_set_size(o->root, size, size);
    lv_obj_add_flag(o->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    o->shadow = rect(o->root, 0x1C2226, LV_RADIUS_CIRCLE);
    lv_obj_set_size(o->shadow, U(o, 68), U(o, 12));
    lv_obj_set_pos(o->shadow, U(o, 66), U(o, 184));

    o->floatbox = plain(o->root);
    lv_obj_set_size(o->floatbox, size, size);
    lv_obj_add_flag(o->floatbox, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    o->hand_l = lv_image_create(o->floatbox);
    o->hand_r = lv_image_create(o->floatbox);
    lv_obj_set_user_data(o->hand_l, o);
    lv_obj_set_user_data(o->hand_r, o);
    o->body = lv_image_create(o->floatbox);

    for (int i = 0; i < 2; ++i) o->eye[i] = rect(o->floatbox, C_CYAN, LV_RADIUS_CIRCLE);
    const float bar_x[3] = {78, 95.5f, 113};
    for (int i = 0; i < 3; ++i) {
        o->bar[i] = rect(o->floatbox, C_CYAN, LV_RADIUS_CIRCLE);
        lv_obj_set_user_data(o->bar[i], o);
        lv_obj_set_size(o->bar[i], U(o, 9), U(o, 30));
        lv_obj_set_pos(o->bar[i], U(o, bar_x[i]), U(o, 92));
        glow(o->bar[i], C_CYAN, U(o, 10));
    }
    o->arc[0] = orb_arc(o, 88);
    o->arc[1] = orb_arc(o, 112);

    const float brow[2][4] = {{78, 105, 95, 100}, {122, 105, 105, 100}};
    for (int i = 0; i < 2; ++i) {
        o->brow_pts[i][0] = (lv_point_precise_t){U(o, brow[i][0]), U(o, brow[i][1])};
        o->brow_pts[i][1] = (lv_point_precise_t){U(o, brow[i][2]), U(o, brow[i][3])};
        o->brow[i] = lv_line_create(o->floatbox);
        lv_line_set_points(o->brow[i], o->brow_pts[i], 2);
        lv_obj_set_style_line_color(o->brow[i], lv_color_hex(C_CORAL), 0);
        lv_obj_set_style_line_width(o->brow[i], U(o, 4), 0);
        lv_obj_set_style_line_rounded(o->brow[i], true, 0);
    }
    const float dot_x[3] = {86, 100, 114};
    for (int i = 0; i < 3; ++i) {
        o->dot[i] = rect(o->floatbox, C_AMBER, LV_RADIUS_CIRCLE);
        lv_obj_set_size(o->dot[i], U(o, 6), U(o, 6));
        lv_obj_set_pos(o->dot[i], U(o, dot_x[i] - 3), U(o, 121));
    }
    o->sparkles = lv_image_create(o->floatbox);
    place_part(o->sparkles, large ? &orb_sparkles_L : &orb_sparkles_M, 0, 0);
    for (int i = 0; i < 2; ++i) {
        o->ripple[i] = plain(o->floatbox);
        lv_obj_set_user_data(o->ripple[i], o);
        lv_obj_set_style_radius(o->ripple[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(o->ripple[i], lv_color_hex(C_CYAN), 0);
        lv_obj_set_style_border_width(o->ripple[i], LV_MAX(2, U(o, 2.5f)), 0);
    }
    orb_set_mood(o, MOOD_IDLE);
    orb_stop(o);
}

/* -------------------------------------------------------------- helpers -- */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *centered_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text,
                                int32_t y, int32_t width)
{
    lv_obj_t *l = label(parent, font, color, text);
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static lv_obj_t *pill_button(lv_obj_t *parent, const char *text, bool filled, uint32_t color,
                             lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    if (filled) {
        lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(b, 2, 0);
    }
    lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
    lv_obj_t *l = label(b, &font_ui_20, filled ? 0x021316 : (color == C_CORAL ? C_CORAL_TEXT : C_TEXT), text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

/* -------------------------------------------------------------- screens -- */

static ui_screen_t current = UI_SCREEN_SETUP;
static ui_error_t current_error;
static uint32_t shown_at;
static bool panel_off;
static bool online;
static bool on_usb = true;

static lv_obj_t *screens[UI_SCREEN_SLEEP + 1];
static orb_t orb_idle, orb_think, orb_listen, orb_done, orb_error;
static lv_obj_t *think_label, *listen_bars[16], *confirm_text, *confirm_count, *done_text, *done_check;
static lv_obj_t *error_title, *error_detail, *error_button, *error_button_label;
static lv_obj_t *history_rows[UI_HISTORY_SHOW], *history_time[UI_HISTORY_SHOW], *history_text[UI_HISTORY_SHOW];
static lv_obj_t *history_ok[UI_HISTORY_SHOW], *history_fail[UI_HISTORY_SHOW], *history_count, *history_empty;
static lv_obj_t *sleep_clock, *sleep_orb;
static lv_obj_t *status_dot, *status_text, *rec_group, *rec_time, *batt_group, *batt_label, *batt_fill;
static const int32_t BAR_BASE[16] = {10, 18, 30, 42, 26, 16, 36, 48, 28, 14, 34, 44, 22, 12, 20, 8};
static atomic_int mic_level;

static void act(ui_action_t a)
{
    if (port && port->action) port->action(a);
}

static void brightness(int pct)
{
    if (port && port->brightness) port->brightness(pct);
}

static void show(ui_screen_t s);

static void key_or_tap(void)
{
    switch (current) {
    case UI_SCREEN_IDLE:
    case UI_SCREEN_LISTEN: act(UI_ACT_TAP); break;
    case UI_SCREEN_CONFIRM: act(UI_ACT_SEND); break;
    case UI_SCREEN_DONE:
    case UI_SCREEN_HISTORY:
    case UI_SCREEN_SLEEP: show(UI_SCREEN_IDLE); break;
    case UI_SCREEN_ERROR:
        if (current_error == UI_ERR_UNCLEAR) act(UI_ACT_TAP);
        else if (current_error == UI_ERR_SEND) act(UI_ACT_RETRY);
        else show(UI_SCREEN_IDLE);
        break;
    default: break;
    }
}

static uint32_t gesture_at;

static void screen_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        gesture_at = lv_tick_get();
        if (dir == LV_DIR_TOP && current == UI_SCREEN_IDLE) show(UI_SCREEN_HISTORY);
        else if (dir == LV_DIR_BOTTOM && current == UI_SCREEN_HISTORY) show(UI_SCREEN_IDLE);
        return;
    }
    if (code == LV_EVENT_LONG_PRESSED) {
        if (current == UI_SCREEN_LISTEN) act(UI_ACT_CANCEL);
        return;
    }
    if (code == LV_EVENT_SHORT_CLICKED && lv_tick_elaps(gesture_at) > 300) {
        if (current == UI_SCREEN_ERROR) return;  // only its button acts
        key_or_tap();
    }
}

static void button_event(lv_event_t *e)
{
    ui_action_t a = (ui_action_t)(intptr_t)lv_event_get_user_data(e);
    if (a == UI_ACT_SEND || a == UI_ACT_REDO) {
        act(a);
    } else {
        key_or_tap();  // error button
    }
}

static lv_obj_t *screen_base(lv_obj_t *parent, ui_screen_t s)
{
    lv_obj_t *scr = plain(parent);
    lv_obj_set_size(scr, 480, 480);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(scr, screen_event, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, screen_event, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_event, LV_EVENT_GESTURE, NULL);
    screens[s] = scr;
    return scr;
}

static lv_obj_t *icon(lv_obj_t *parent, const orb_part_t *part, int32_t box_x, int32_t box_y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, part->img);
    lv_obj_set_pos(img, box_x + part->x, box_y + part->y);
    return img;
}

static void build_setup(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_SETUP);
    icon(s, &orb_icon_idle_64, 208, 110);
    centered_label(s, &font_ui_28, C_TEXT, "需要用 USB 設定", 210, 420);
    centered_label(s, &font_ui_20, C_GREY2, "請在 Mac 執行 provision 指令，\n把 Wi-Fi 和連線憑證寫進板子。", 262, 420);
}

static void build_idle(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_IDLE);
    orb_create(&orb_idle, s, true);
    lv_obj_set_pos(orb_idle.root, 120, 112);
    lv_obj_t *hint = centered_label(s, &font_ui_20, C_GREY, "輕觸螢幕 開始說話", 380, 440);
    lv_obj_set_style_text_letter_space(hint, 3, 0);
}

static void build_listen(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_LISTEN);
    orb_create(&orb_listen, s, false);
    lv_obj_set_pos(orb_listen.root, 150, 72);
    for (int i = 0; i < 16; ++i) {
        listen_bars[i] = rect(s, C_CYAN, 4);
        lv_obj_set_size(listen_bars[i], 8, BAR_BASE[i]);
        lv_obj_set_pos(listen_bars[i], 240 - (16 * 14 - 6) / 2 + i * 14, 290 - BAR_BASE[i] / 2);
    }
    centered_label(s, &font_ui_20, C_TEXT, "再按一下 送出", 334, 440);
    centered_label(s, &font_ui_16, C_GREY, "長按取消 · 最長 30 秒", 368, 440);
}

static void build_think(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_THINK);
    screens[UI_SCREEN_SENDING] = s;
    orb_create(&orb_think, s, true);
    lv_obj_set_pos(orb_think.root, 120, 106);
    think_label = centered_label(s, &font_ui_20, C_TEXT, "正在把聲音轉成文字", 374, 440);
}

static void build_confirm(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_CONFIRM);
    icon(s, &orb_icon_idle_64, 30, 62);
    lv_obj_t *head = label(s, &font_ui_20, C_GREY2, "要交給 Grok Bot 的指令");
    lv_obj_set_pos(head, 108, 80);

    lv_obj_t *box = plain(s);
    lv_obj_set_pos(box, 30, 140);
    lv_obj_set_size(box, 420, 172);
    lv_obj_set_style_radius(box, 20, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(C_BOX), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(C_BOX_LINE), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    confirm_text = label(box, &font_text_22, C_TEXT, "");
    lv_obj_set_pos(confirm_text, 22, 18);
    lv_obj_set_size(confirm_text, 376, 136);
    lv_obj_set_style_text_line_space(confirm_text, 6, 0);
    lv_label_set_long_mode(confirm_text, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *redo = pill_button(s, "重講", false, 0x4A5256, button_event, (void *)(intptr_t)UI_ACT_REDO);
    lv_obj_set_pos(redo, 30, 328);
    lv_obj_set_size(redo, 168, 64);
    lv_obj_t *send = pill_button(s, "送出", true, C_CYAN, button_event, (void *)(intptr_t)UI_ACT_SEND);
    lv_obj_set_pos(send, 210, 328);
    lv_obj_set_size(send, 240, 64);
    confirm_count = centered_label(s, &font_ui_16, C_GREY, "", 412, 440);
}

static void build_done(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_DONE);
    orb_create(&orb_done, s, false);
    lv_obj_set_pos(orb_done.root, 150, 56);

    lv_obj_t *row = plain(s);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 254);
    lv_obj_t *ring = plain(row);
    lv_obj_set_size(ring, 36, 36);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(C_CYAN), 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    static const lv_point_precise_t tick[] = {{10, 19}, {16, 25}, {27, 12}};
    done_check = lv_line_create(ring);
    lv_line_set_points(done_check, tick, 3);
    lv_obj_set_style_line_color(done_check, lv_color_hex(C_CYAN), 0);
    lv_obj_set_style_line_width(done_check, 4, 0);
    lv_obj_set_style_line_rounded(done_check, true, 0);
    label(row, &font_ui_28, C_TEXT, "已交給 Grok Bot");

    done_text = centered_label(s, &font_text_22, C_GREY2, "", 304, 400);
    lv_obj_set_height(done_text, 64);
    lv_label_set_long_mode(done_text, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *pill = plain(s);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_pad_ver(pill, 8, 0);
    lv_obj_set_style_radius(pill, 22, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x0F1A1C), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    label(pill, &font_ui_16, 0x9FDDE1, "結果請到電腦上的 Grok Bot 查看");
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 386);
}

static void build_error(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_ERROR);
    orb_create(&orb_error, s, false);
    lv_obj_set_pos(orb_error.root, 150, 50);

    lv_obj_t *row = plain(s);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_t *badge = rect(row, C_CORAL, LV_RADIUS_CIRCLE);
    lv_obj_set_size(badge, 28, 28);
    lv_obj_t *mark = label(badge, &font_num_16, 0x1A0503, "!");
    lv_obj_center(mark);
    error_title = label(row, &font_ui_28, C_TEXT, "");

    error_detail = centered_label(s, &font_ui_20, C_GREY2, "", 294, 400);
    lv_obj_set_style_text_line_space(error_detail, 4, 0);
    error_button = pill_button(s, "", false, C_CORAL, button_event, (void *)(intptr_t)UI_ACT_RETRY);
    lv_obj_set_size(error_button, 220, 58);
    lv_obj_align(error_button, LV_ALIGN_TOP_MID, 0, 398);
    error_button_label = lv_obj_get_child(error_button, 0);
}

static void build_history(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_HISTORY);
    icon(s, &orb_icon_idle_40, 26, 62);
    lv_obj_t *title = label(s, &font_ui_28, C_TEXT, "最近的指令");
    lv_obj_set_pos(title, 78, 60);
    history_count = label(s, &font_num_16, C_GREY, "");
    lv_obj_align(history_count, LV_ALIGN_TOP_RIGHT, -26, 72);
    for (int i = 0; i < UI_HISTORY_SHOW; ++i) {
        lv_obj_t *row = rect(s, C_BOX, 16);
        lv_obj_set_pos(row, 26, 116 + i * 74);
        lv_obj_set_size(row, 428, 64);
        history_rows[i] = row;
        history_time[i] = label(row, &font_num_16, C_GREY, "");
        lv_obj_align(history_time[i], LV_ALIGN_LEFT_MID, 14, 0);
        history_text[i] = label(row, &font_text_22, C_TEXT, "");
        lv_obj_set_size(history_text[i], 236, 32);
        lv_label_set_long_mode(history_text[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(history_text[i], LV_ALIGN_LEFT_MID, 74, 0);
        history_ok[i] = rect(row, 0x0F2629, 12);
        lv_obj_set_size(history_ok[i], 72, 28);
        lv_obj_align(history_ok[i], LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_center(label(history_ok[i], &font_ui_16, 0x8FEFF4, "已送出"));
        history_fail[i] = rect(row, 0x2A1210, 12);
        lv_obj_set_size(history_fail[i], 56, 28);
        lv_obj_align(history_fail[i], LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_center(label(history_fail[i], &font_ui_16, C_CORAL_TEXT, "失敗"));
    }
    history_empty = centered_label(s, &font_ui_20, C_GREY, "還沒有指令。\n回到封面，輕觸螢幕開始說話。", 200, 420);
    centered_label(s, &font_ui_16, C_GREY, "向下滑 返回", 436, 440);
}

static void build_sleep(lv_obj_t *parent)
{
    lv_obj_t *s = screen_base(parent, UI_SCREEN_SLEEP);
    sleep_clock = centered_label(s, &font_num_64, C_DIM, "--:--", 104, 440);
    sleep_orb = icon(s, &orb_icon_sleep_120, 180, 226);
    centered_label(s, &font_ui_16, C_DIM, "輕觸喚醒", 404, 440);
}

static void build_status(lv_obj_t *parent)
{
    lv_obj_t *bar = plain(parent);
    lv_obj_set_size(bar, 480, 56);
    status_dot = rect(bar, C_CORAL, LV_RADIUS_CIRCLE);
    lv_obj_set_size(status_dot, 9, 9);
    lv_obj_set_pos(status_dot, 30, 24);
    status_text = label(bar, &font_ui_16, C_GREY2, "未連線");
    lv_obj_set_pos(status_text, 46, 17);

    rec_group = plain(bar);
    lv_obj_set_size(rec_group, 90, 56);
    lv_obj_align(rec_group, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *rec_dot = rect(rec_group, C_CORAL, LV_RADIUS_CIRCLE);
    lv_obj_set_size(rec_dot, 10, 10);
    lv_obj_set_pos(rec_dot, 18, 23);
    anim_start(rec_dot, exec_opa, 70, 255, 600, 600, 0, 0, NULL);
    rec_time = label(rec_group, &font_num_16, C_TEXT, "0:00");
    lv_obj_set_pos(rec_time, 36, 17);
    lv_obj_add_flag(rec_group, LV_OBJ_FLAG_HIDDEN);

    batt_group = plain(bar);
    lv_obj_set_size(batt_group, 110, 56);
    lv_obj_align(batt_group, LV_ALIGN_TOP_RIGHT, -26, 0);
    batt_label = label(batt_group, &font_num_16, C_GREY2, "");
    lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_t *shell = plain(batt_group);
    lv_obj_set_size(shell, 30, 16);
    lv_obj_align(shell, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_radius(shell, 4, 0);
    lv_obj_set_style_border_color(shell, lv_color_hex(C_GREY2), 0);
    lv_obj_set_style_border_width(shell, 2, 0);
    batt_fill = rect(shell, C_TEXT, 2);
    lv_obj_set_pos(batt_fill, 3, 3);
    lv_obj_set_size(batt_fill, 20, 6);
    lv_obj_t *nub = rect(batt_group, C_GREY2, 1);
    lv_obj_set_size(nub, 3, 6);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(batt_group, LV_OBJ_FLAG_HIDDEN);
}

static orb_t *orb_for(ui_screen_t s)
{
    switch (s) {
    case UI_SCREEN_IDLE: return &orb_idle;
    case UI_SCREEN_THINK:
    case UI_SCREEN_SENDING: return &orb_think;
    case UI_SCREEN_LISTEN: return &orb_listen;
    case UI_SCREEN_DONE: return &orb_done;
    case UI_SCREEN_ERROR: return &orb_error;
    default: return NULL;
    }
}

static mood_t mood_for(ui_screen_t s)
{
    switch (s) {
    case UI_SCREEN_THINK:
    case UI_SCREEN_SENDING: return MOOD_THINK;
    case UI_SCREEN_LISTEN: return MOOD_LISTEN;
    case UI_SCREEN_DONE: return MOOD_HAPPY;
    case UI_SCREEN_ERROR: return MOOD_SAD;
    default: return MOOD_IDLE;
    }
}

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2024) lv_label_set_text(sleep_clock, "--:--");
    else lv_label_set_text_fmt(sleep_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void show(ui_screen_t s)
{
    orb_t *old = orb_for(current);
    if (old && screens[current] != screens[s]) orb_stop(old);
    for (int i = 0; i <= UI_SCREEN_SLEEP; ++i) {
        if (screens[i] && screens[i] != screens[s]) lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(screens[s], LV_OBJ_FLAG_HIDDEN);
    if (s == UI_SCREEN_THINK) lv_label_set_text(think_label, "正在把聲音轉成文字");
    if (s == UI_SCREEN_SENDING) lv_label_set_text(think_label, "正在交給 Grok Bot");
    orb_t *orb = orb_for(s);
    if (orb) orb_set_mood(orb, mood_for(s));
    lv_anim_delete(sleep_orb, NULL);
    if (s == UI_SCREEN_SLEEP) {
        update_clock();
        anim_start(sleep_orb, exec_translate_y, 0, -6, 3000, 3000, 0, 0, NULL);
        brightness(BRIGHT_SLEEP);
    } else if (current == UI_SCREEN_SLEEP || panel_off) {
        brightness(BRIGHT_ON);
    }
    panel_off = false;
    lv_obj_set_flag(rec_group, LV_OBJ_FLAG_HIDDEN, s != UI_SCREEN_LISTEN);
    lv_obj_set_flag(lv_obj_get_parent(status_dot), LV_OBJ_FLAG_HIDDEN, s == UI_SCREEN_SLEEP);
    current = s;
    shown_at = lv_tick_get();
}

static const char *const ERROR_TEXT[][3] = {
    [UI_ERR_OFFLINE] = {"送不出去", "找不到你的 Mac。請確認 Mac 已開機、沒有睡眠，並和板子連同一個 Wi-Fi。", "知道了"},
    [UI_ERR_UNCLEAR] = {"聽不清楚", "請靠近一點，再講一次。", "再講一次"},
    [UI_ERR_SEND] = {"送不出去", "Grok Bot 沒有回應。請確認 Mac 上的 webhook 設定，或稍後再試。", "再試一次"},
    [UI_ERR_SETUP] = {"還未設定 Grok Bot", "請在 Mac 的 config/backend.env 填入 webhook 網址和金鑰。", "知道了"},
    [UI_ERR_STT] = {"未能辨識語音", "Mac 的語音辨識未能使用。請查看 Mac 上後端視窗的提示。", "知道了"},
};

static void show_error(ui_error_t e)
{
    current_error = e;
    lv_label_set_text(error_title, ERROR_TEXT[e][0]);
    lv_label_set_text(error_detail, ERROR_TEXT[e][1]);
    lv_label_set_text(error_button_label, ERROR_TEXT[e][2]);
    show(UI_SCREEN_ERROR);
}

static void set_link(bool value)
{
    online = value;
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(value ? C_CYAN : C_CORAL), 0);
    lv_label_set_text(status_text, value ? "Mac" : "未連線");
}

static void set_battery(int pct, bool charging, bool usb)
{
    on_usb = usb;
    if (pct < 0) {
        lv_obj_add_flag(batt_group, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(batt_group, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(batt_label, "%d%%", pct);
    lv_obj_set_width(batt_fill, LV_MAX(2, 20 * pct / 100));
    uint32_t color = charging ? C_CYAN : pct <= 15 ? C_CORAL : C_TEXT;
    lv_obj_set_style_bg_color(batt_fill, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(batt_label, lv_color_hex(charging ? C_CYAN : C_GREY2), 0);
}

static void set_history(const ui_history_item_t *items, int count, int total)
{
    lv_label_set_text_fmt(history_count, "%d / %d", count, total);
    lv_obj_set_flag(history_empty, LV_OBJ_FLAG_HIDDEN, count > 0);
    for (int i = 0; i < UI_HISTORY_SHOW; ++i) {
        bool used = i < count;
        lv_obj_set_flag(history_rows[i], LV_OBJ_FLAG_HIDDEN, !used);
        if (!used) continue;
        lv_label_set_text(history_time[i], items[i].time);
        lv_label_set_text(history_text[i], items[i].text);
        lv_obj_set_flag(history_ok[i], LV_OBJ_FLAG_HIDDEN, !items[i].ok);
        lv_obj_set_flag(history_fail[i], LV_OBJ_FLAG_HIDDEN, items[i].ok);
    }
}

/* ------------------------------------------------------- queued updates -- */

static struct {
    bool screen_dirty;
    ui_screen_t screen;
    ui_error_t error;
    char text[UI_TEXT_MAX];
    bool link_dirty, online;
    bool batt_dirty, charging, usb;
    int percent;
    bool history_dirty;
    ui_history_item_t items[UI_HISTORY_SHOW];
    int count, total;
    bool key;
} pending;

static void lock(void) { if (port && port->lock) port->lock(); }
static void unlock(void) { if (port && port->unlock) port->unlock(); }

static void post_screen(ui_screen_t s, const char *text, ui_error_t error)
{
    lock();
    pending.screen_dirty = true;
    pending.screen = s;
    pending.error = error;
    snprintf(pending.text, sizeof(pending.text), "%s", text ? text : "");
    unlock();
}

void ui_show(ui_screen_t s) { post_screen(s, NULL, UI_ERR_OFFLINE); }
void ui_show_confirm(const char *text) { post_screen(UI_SCREEN_CONFIRM, text, UI_ERR_OFFLINE); }
void ui_show_done(const char *text) { post_screen(UI_SCREEN_DONE, text, UI_ERR_OFFLINE); }
void ui_show_error(ui_error_t error) { post_screen(UI_SCREEN_ERROR, NULL, error); }
void ui_set_level(int level) { atomic_store(&mic_level, LV_CLAMP(0, level, 1000)); }

void ui_set_link(bool value)
{
    lock();
    pending.link_dirty = true;
    pending.online = value;
    unlock();
}

void ui_set_battery(int percent, bool charging, bool usb_power)
{
    lock();
    pending.batt_dirty = true;
    pending.percent = percent;
    pending.charging = charging;
    pending.usb = usb_power;
    unlock();
}

void ui_set_history(const ui_history_item_t *items, int count, int total)
{
    lock();
    pending.history_dirty = true;
    pending.count = LV_MIN(count, UI_HISTORY_SHOW);
    pending.total = total;
    memcpy(pending.items, items, sizeof(ui_history_item_t) * pending.count);
    unlock();
}

void ui_key(void)
{
    lock();
    pending.key = true;
    unlock();
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    static char text[UI_TEXT_MAX];
    static ui_history_item_t items[UI_HISTORY_SHOW];
    lock();
    bool screen_dirty = pending.screen_dirty, link_dirty = pending.link_dirty;
    bool batt_dirty = pending.batt_dirty, history_dirty = pending.history_dirty, key = pending.key;
    ui_screen_t screen = pending.screen;
    ui_error_t error = pending.error;
    bool link = pending.online, charging = pending.charging, usb = pending.usb;
    int percent = pending.percent, count = pending.count, total = pending.total;
    if (screen_dirty) memcpy(text, pending.text, sizeof(text));
    if (history_dirty) memcpy(items, pending.items, sizeof(items));
    pending.screen_dirty = pending.link_dirty = pending.batt_dirty = pending.history_dirty = pending.key = false;
    unlock();

    if (link_dirty) set_link(link);
    if (batt_dirty) set_battery(percent, charging, usb);
    if (history_dirty) set_history(items, count, total);
    if (screen_dirty) {
        if (screen == UI_SCREEN_ERROR) show_error(error);
        else {
            if (screen == UI_SCREEN_CONFIRM) lv_label_set_text(confirm_text, text);
            if (screen == UI_SCREEN_DONE) {
                char quoted[UI_TEXT_MAX + 8];
                snprintf(quoted, sizeof(quoted), "「%s」", text);
                lv_label_set_text(done_text, quoted);
            }
            show(screen);
        }
    }
    if (key) {
        if (panel_off || current == UI_SCREEN_SLEEP) show(UI_SCREEN_IDLE);
        else key_or_tap();
    }

    uint32_t since = lv_tick_elaps(shown_at);
    uint32_t inactive = lv_display_get_inactive_time(NULL);
    if (current == UI_SCREEN_LISTEN) {
        lv_label_set_text_fmt(rec_time, "%" LV_PRIu32 ":%02" LV_PRIu32, since / 60000, (since / 1000) % 60);
        int level = atomic_load(&mic_level);
        static uint32_t jitter = 12345;
        for (int i = 0; i < 16; ++i) {
            jitter = jitter * 1103515245u + 12345u;
            int32_t scale = 250 + 750 * level / 1000;
            int32_t h = LV_CLAMP(6, BAR_BASE[i] * scale / 1000 + (int32_t)((jitter >> 16) % 7) - 3, 48);
            lv_obj_set_height(listen_bars[i], h);
            lv_obj_set_y(listen_bars[i], 290 - h / 2);
        }
    } else if (current == UI_SCREEN_CONFIRM) {
        int left = (int)((CONFIRM_MS - LV_MIN(since, CONFIRM_MS) + 999) / 1000);
        lv_label_set_text_fmt(confirm_count, "%d 秒內沒有確認，就自動取消", left);
        if (since >= CONFIRM_MS) {
            act(UI_ACT_TIMEOUT);
            show(UI_SCREEN_IDLE);
        }
    } else if (current == UI_SCREEN_DONE && since >= DONE_MS) {
        show(UI_SCREEN_IDLE);
    } else if (current == UI_SCREEN_HISTORY && inactive >= HISTORY_IDLE_MS) {
        show(UI_SCREEN_IDLE);
    } else if (current == UI_SCREEN_IDLE && inactive >= SLEEP_AFTER_MS && since >= SLEEP_AFTER_MS) {
        show(UI_SCREEN_SLEEP);
    } else if (current == UI_SCREEN_SLEEP) {
        if (since % 1000 < 40) update_clock();
        if (!panel_off && !on_usb && inactive >= PANEL_OFF_AFTER_MS) {
            panel_off = true;
            lv_anim_delete(sleep_orb, NULL);  // nothing to animate on a dark panel
            brightness(0);
        }
    }
    if (current == UI_SCREEN_DONE) {
        lv_obj_set_style_line_opa(done_check, (lv_opa_t)LV_MIN(255, since * 255 / 400), 0);
    }
}

void ui_init(const ui_port_t *p)
{
    port = p;
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    build_setup(root);
    build_idle(root);
    build_listen(root);
    build_think(root);
    build_confirm(root);
    build_done(root);
    build_error(root);
    build_history(root);
    build_sleep(root);
    build_status(root);
    set_link(false);
    set_history(NULL, 0, 0);
    show(UI_SCREEN_SETUP);
    lv_timer_create(tick, 33, NULL);
}
