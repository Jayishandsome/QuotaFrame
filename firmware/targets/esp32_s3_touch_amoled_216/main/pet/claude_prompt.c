#include "claude_prompt.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "orb.h"

LV_FONT_DECLARE(pet_ui_16);
LV_FONT_DECLARE(pet_ui_20);

#define C_BG 0x000000
#define C_TEXT 0xF2F4F7
#define C_MUTED 0x8B93A1
#define C_CARD 0x0C0F12
#define C_CARD_LINE 0x232830
#define C_CYAN 0x5CE8F0
#define C_CYAN_INK 0x06272A
#define C_CORAL 0xFF7A6B
#define C_AMBER 0xFFC069

#define TOP 51            // below the header
#define W 480
#define H (480 - TOP)
#define ORB_Y 0
#define TITLE_Y 184
#define CARD_Y 220
#define CARD_H 92
#define NOTE_Y 318
#define BUTTON_Y 346
#define BUTTON_W 204
#define BUTTON_H 68
#define GUARD_MS 700      // ignore taps right after the card appears

static lv_obj_t *panel, *title, *hint, *note, *allow_btn, *deny_btn, *allow_label, *deny_label;
static orb_t *pet;
static atomic_int pending = CLAUDE_ACTION_NONE;
static uint32_t shown_at;
static uint32_t shown_serial;

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

static void pressed(lv_event_t *e)
{
    if (lv_tick_elaps(shown_at) < GUARD_MS) return;
    atomic_store(&pending, (int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *button(lv_obj_t *parent, int x, bool filled, uint32_t color, const char *s, lv_obj_t **label,
                        claude_action_t action)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, BUTTON_W, BUTTON_H);
    lv_obj_set_pos(b, x, BUTTON_Y);
    lv_obj_set_style_radius(b, BUTTON_H / 2, 0);
    lv_obj_set_style_bg_opa(b, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(b, filled ? 0 : 2, 0);
    lv_obj_set_style_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, pressed, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    *label = text(b, &pet_ui_20, filled ? C_CYAN_INK : color, s);
    lv_obj_center(*label);
    return b;
}

void claude_prompt_create(lv_obj_t *screen)
{
    panel = lv_obj_create(screen);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);  // swallow taps meant for the page below
    lv_obj_set_pos(panel, 0, TOP);
    lv_obj_set_size(panel, W, H);
    lv_obj_set_style_bg_color(panel, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    pet = orb_create(panel, false);
    lv_obj_set_pos(orb_root(pet), (W - 180) / 2, ORB_Y);
    orb_set_mood(pet, ORB_THINK);

    title = text(panel, &pet_ui_20, C_TEXT, "");
    lv_obj_set_width(title, W - 48);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 24, TITLE_Y);

    lv_obj_t *card = lv_obj_create(panel);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(card, W - 48, CARD_H);
    lv_obj_set_pos(card, 24, CARD_Y);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(C_CARD_LINE), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    hint = text(card, &pet_ui_16, C_TEXT, "");
    lv_obj_set_size(hint, W - 48 - 32, CARD_H - 24);
    lv_obj_set_pos(hint, 16, 12);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

    note = text(panel, &pet_ui_16, C_MUTED, "");
    lv_obj_set_width(note, W - 48);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 24, NOTE_Y);

    deny_btn = button(panel, (W / 2) - 8 - BUTTON_W, false, C_CORAL, "拒絕", &deny_label, CLAUDE_ACTION_DENY);
    allow_btn = button(panel, (W / 2) + 8, true, C_CYAN, "允許", &allow_label, CLAUDE_ACTION_ALLOW);

    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    orb_set_running(pet, false);
}

bool claude_prompt_visible(void) { return panel && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN); }

claude_action_t claude_prompt_take_action(void)
{
    return (claude_action_t)atomic_exchange(&pending, CLAUDE_ACTION_NONE);
}

void claude_prompt_update(const claude_prompt_view_t *v)
{
    if (!v->show) {
        if (claude_prompt_visible()) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
            orb_set_running(pet, false);
        }
        atomic_store(&pending, CLAUDE_ACTION_NONE);
        return;
    }
    const char *tool = v->tool && v->tool[0] ? v->tool : "工具";
    char line[96];
    snprintf(line, sizeof(line), "Claude 想使用 %s", tool);
    if (!claude_prompt_visible() || v->serial != shown_serial) {
        // A new request: drop stale taps and restart the tap guard.
        shown_serial = v->serial;
        shown_at = lv_tick_get();
        atomic_store(&pending, CLAUDE_ACTION_NONE);
    }
    lv_label_set_text(title, line);
    lv_label_set_text(hint, v->hint && v->hint[0] ? v->hint : "（沒有更多說明）");

    bool answered = v->reply == CLAUDE_REPLY_ALLOWED || v->reply == CLAUDE_REPLY_DENIED;
    if (v->reply == CLAUDE_REPLY_ALLOWED) {
        lv_label_set_text(note, "已允許，等 Claude 繼續…");
        lv_obj_set_style_text_color(note, lv_color_hex(C_CYAN), 0);
        orb_set_mood(pet, ORB_HAPPY);
    } else if (v->reply == CLAUDE_REPLY_DENIED) {
        lv_label_set_text(note, "已拒絕");
        lv_obj_set_style_text_color(note, lv_color_hex(C_CORAL), 0);
        orb_set_mood(pet, ORB_IDLE);
    } else if (v->reply == CLAUDE_REPLY_FAILED) {
        lv_label_set_text(note, "沒送出去，再按一次");
        lv_obj_set_style_text_color(note, lv_color_hex(C_AMBER), 0);
        orb_set_mood(pet, ORB_SAD);
    } else {
        if (v->waiting > 1) lv_label_set_text_fmt(note, "還有 %lu 個請求在等你", (unsigned long)(v->waiting - 1));
        else lv_label_set_text(note, "要讓它繼續嗎？");
        lv_obj_set_style_text_color(note, lv_color_hex(C_MUTED), 0);
        orb_set_mood(pet, ORB_THINK);
    }
    lv_obj_set_style_opa(allow_btn, answered ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(deny_btn, answered ? LV_OPA_30 : LV_OPA_COVER, 0);
    if (answered) {
        lv_obj_remove_flag(allow_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(deny_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_flag(allow_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(deny_btn, LV_OBJ_FLAG_CLICKABLE);
    }

    if (!claude_prompt_visible()) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        orb_set_running(pet, true);
    }
    lv_obj_move_foreground(panel);
}
