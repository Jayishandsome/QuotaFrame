#include "orb.h"

#include <math.h>
#include <string.h>

#include "orb_assets.h"

#define C_CYAN 0x5CE8F0
#define C_AMBER 0xFFC069
#define C_CORAL 0xFF7A6B
#define C_SLEEP 0x8FA3A8

enum { ACC_CYAN, ACC_AMBER, ACC_CORAL, ACC_GREY };

struct orb {
    bool large, running;
    orb_mood_t mood;
    float k;  // px per design unit (the design box is 200 units)
    lv_obj_t *root, *shadow, *floatbox, *hand_l, *hand_r, *body;
    lv_obj_t *eye[2], *arc[2], *brow[2], *dot[3], *sparkles;
    lv_point_precise_t brow_pts[2][2];
    int32_t eye_cy, eye_h;
};

static const orb_part_t *const BODY[2][4] = {
    {&orb_body_cyan_M, &orb_body_amber_M, &orb_body_coral_M, &orb_body_grey_M},
    {&orb_body_cyan_L, &orb_body_amber_L, &orb_body_coral_L, &orb_body_grey_L},
};
static const orb_part_t *const HAND_L[2][4] = {
    {&orb_hand_l_cyan_M, &orb_hand_l_amber_M, &orb_hand_l_coral_M, &orb_hand_l_grey_M},
    {&orb_hand_l_cyan_L, &orb_hand_l_amber_L, &orb_hand_l_coral_L, &orb_hand_l_grey_L},
};
static const orb_part_t *const HAND_R[2][4] = {
    {&orb_hand_r_cyan_M, &orb_hand_r_amber_M, &orb_hand_r_coral_M, &orb_hand_r_grey_M},
    {&orb_hand_r_cyan_L, &orb_hand_r_amber_L, &orb_hand_r_coral_L, &orb_hand_r_grey_L},
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
    lv_obj_set_style_shadow_opa(obj, width > 0 ? LV_OPA_60 : LV_OPA_TRANSP, 0);
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

static void stop_all(orb_t *o)
{
    lv_anim_delete(o, NULL);
    lv_anim_delete(o->floatbox, NULL);
    lv_anim_delete(o->hand_l, NULL);
    lv_anim_delete(o->hand_r, NULL);
    lv_anim_delete(o->sparkles, NULL);
    for (int i = 0; i < 3; ++i) lv_anim_delete(o->dot[i], NULL);
    lv_obj_set_style_translate_y(o->floatbox, 0, 0);
    lv_obj_set_style_translate_x(o->hand_l, 0, 0);
    lv_obj_set_style_translate_y(o->hand_l, 0, 0);
    lv_obj_set_style_translate_x(o->hand_r, 0, 0);
    lv_obj_set_style_translate_y(o->hand_r, 0, 0);
    if (o->eye_h > 0) exec_blink(o, 100);
}

static void eyes(orb_t *o, float x, float y, float w, float h, uint32_t color, bool lit)
{
    o->eye_h = U(o, h);
    o->eye_cy = U(o, y + h / 2);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *e = o->eye[i];
        lv_obj_set_size(e, U(o, w), o->eye_h);
        lv_obj_set_pos(e, U(o, x + 24 * i), U(o, y));
        lv_obj_set_style_bg_color(e, lv_color_hex(color), 0);
        glow(e, color, lit ? U(o, 10) : 0);
        lv_obj_remove_flag(e, LV_OBJ_FLAG_HIDDEN);
    }
}

static void start_anims(orb_t *o)
{
    orb_mood_t m = o->mood;
    if (m == ORB_IDLE || m == ORB_HAPPY || m == ORB_THINK) {
        anim_start(o->hand_l, exec_hand_l, 0, 1000, 1100, 1100, 0, 0, NULL);
        anim_start(o->hand_r, exec_hand_r, 0, 1000, 1300, 1300, 0, 0, NULL);
    }
    if (m == ORB_HAPPY) {
        anim_start(o->floatbox, exec_translate_y, -U(o, 14), 0, 900, 0, 0, 500, lv_anim_path_bounce);
        anim_start(o->sparkles, exec_opa, 80, 255, 600, 600, 0, 0, NULL);
    } else if (m == ORB_SLEEPY) {
        anim_start(o->floatbox, exec_translate_y, 0, -U(o, 5), 2600, 2600, 0, 0, NULL);
    } else if (m != ORB_SAD) {
        anim_start(o->floatbox, exec_translate_y, 0, -U(o, 10), 1600, 1600, 0, 0, NULL);
    }
    if (m == ORB_IDLE) anim_start(o, exec_blink, 100, 10, 110, 110, 1200, 3800, lv_anim_path_linear);
    if (m == ORB_THINK) {
        for (int i = 0; i < 3; ++i) anim_start(o->dot[i], exec_opa, 70, 255, 600, 600, 300 * i, 0, NULL);
    }
}

static void apply_mood(orb_t *o)
{
    stop_all(o);
    orb_mood_t m = o->mood;
    int acc = m == ORB_THINK ? ACC_AMBER : m == ORB_SAD ? ACC_CORAL : m == ORB_SLEEPY ? ACC_GREY : ACC_CYAN;
    int32_t pose_x = 0, pose_y = 0;
    if (m == ORB_HAPPY) { pose_x = -U(o, 6); pose_y = -U(o, 10); }
    if (m == ORB_SAD) { pose_x = U(o, 3); pose_y = U(o, 8); }
    if (m == ORB_SLEEPY) { pose_y = U(o, 5); }
    place_part(o->body, BODY[o->large][acc], 0, 0);
    place_part(o->hand_l, HAND_L[o->large][acc], pose_x, pose_y);
    place_part(o->hand_r, HAND_R[o->large][acc], -pose_x, pose_y);

    lv_obj_t *optional[] = {o->eye[0], o->eye[1], o->arc[0], o->arc[1], o->brow[0], o->brow[1],
                            o->dot[0], o->dot[1], o->dot[2], o->sparkles};
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i) lv_obj_add_flag(optional[i], LV_OBJ_FLAG_HIDDEN);
    o->eye_h = 0;

    switch (m) {
    case ORB_IDLE:
        eyes(o, 83, 93, 10, 28, C_CYAN, true);
        break;
    case ORB_THINK:
        eyes(o, 83.5f, 92, 9, 18, C_AMBER, true);
        for (int i = 0; i < 3; ++i) lv_obj_remove_flag(o->dot[i], LV_OBJ_FLAG_HIDDEN);
        break;
    case ORB_HAPPY:
        lv_obj_remove_flag(o->arc[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->arc[1], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->sparkles, LV_OBJ_FLAG_HIDDEN);
        break;
    case ORB_SAD:
        eyes(o, 83, 108, 10, 14, C_CORAL, true);
        lv_obj_remove_flag(o->brow[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(o->brow[1], LV_OBJ_FLAG_HIDDEN);
        break;
    case ORB_SLEEPY:
        eyes(o, 80, 108, 16, 3.5f, C_SLEEP, false);
        break;
    }
    if (o->running) start_anims(o);
}

static lv_obj_t *smile(orb_t *o, float cx)
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

/*
 * Hands, light trails and the hop reach outside the orb's box. LVGL clips
 * invalidated areas to each parent's box plus its "extra draw size", so the
 * containers get a margin; otherwise the trails are never repainted fully.
 */
static void margin_event(lv_event_t *e) { lv_event_set_ext_draw_size(e, 72); }

static void delete_event(lv_event_t *e)
{
    orb_t *o = lv_event_get_user_data(e);
    lv_anim_delete(o, NULL);
    lv_free(o);
}

orb_t *orb_create(lv_obj_t *parent, bool large)
{
    orb_t *o = lv_malloc(sizeof(*o));
    LV_ASSERT_MALLOC(o);
    memset(o, 0, sizeof(*o));
    o->large = large;
    o->running = true;
    int32_t size = large ? 240 : 180;
    o->k = size / 200.0f;
    o->root = plain(parent);
    lv_obj_set_size(o->root, size, size);
    lv_obj_add_flag(o->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(o->root, delete_event, LV_EVENT_DELETE, o);

    o->shadow = rect(o->root, 0x1C2226, LV_RADIUS_CIRCLE);
    lv_obj_set_size(o->shadow, U(o, 68), U(o, 12));
    lv_obj_set_pos(o->shadow, U(o, 66), U(o, 184));

    o->floatbox = plain(o->root);
    lv_obj_set_size(o->floatbox, size, size);
    lv_obj_add_flag(o->floatbox, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(o->root, margin_event, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
    lv_obj_add_event_cb(o->floatbox, margin_event, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
    lv_obj_refresh_ext_draw_size(o->root);
    lv_obj_refresh_ext_draw_size(o->floatbox);

    o->hand_l = lv_image_create(o->floatbox);
    o->hand_r = lv_image_create(o->floatbox);
    lv_obj_set_user_data(o->hand_l, o);
    lv_obj_set_user_data(o->hand_r, o);
    o->body = lv_image_create(o->floatbox);

    for (int i = 0; i < 2; ++i) o->eye[i] = rect(o->floatbox, C_CYAN, LV_RADIUS_CIRCLE);
    o->arc[0] = smile(o, 88);
    o->arc[1] = smile(o, 112);

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

    o->mood = ORB_IDLE;
    apply_mood(o);
    return o;
}

lv_obj_t *orb_root(orb_t *o) { return o->root; }

orb_mood_t orb_mood(const orb_t *o) { return o->mood; }

void orb_set_mood(orb_t *o, orb_mood_t mood)
{
    if (mood == o->mood) return;
    o->mood = mood;
    apply_mood(o);
}

void orb_set_running(orb_t *o, bool running)
{
    if (running == o->running) return;
    o->running = running;
    stop_all(o);
    if (running) start_anims(o);
}
