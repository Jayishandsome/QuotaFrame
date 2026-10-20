#include "pet_overview.h"

#include <stdio.h>
#include <string.h>

#include "orb.h"

LV_FONT_DECLARE(pet_ui_16);
LV_FONT_DECLARE(pet_ui_20);
LV_FONT_DECLARE(pet_num_44);

#define C_TEXT 0xF2F4F7
#define C_MUTED 0x8B93A1
#define C_DIM 0x565D68
#define C_CARD 0x0C0F12
#define C_CARD_LINE 0x232830
#define C_TRACK 0x1C2128
#define C_CYAN 0x5CE8F0
#define C_AMBER 0xFFC069
#define C_CORAL 0xFF7A6B
#define C_CODEX 0x7C9CFF
#define C_CLAUDE 0xE8875F

#define PANEL_W 480
#define ORB_SIZE 180
#define ORB_Y 2
#define SAY_Y 186
#define CARD_Y 222
#define CARD_W 210
#define CARD_H 158
#define CARD_GAP 12
#define CARD_PAD 16
#define BAR_W (CARD_W - 2 * CARD_PAD)

#define LOW_LEFT 20     // below this the pet worries
#define GOOD_LEFT 50    // at or above this the quota is "plenty"
#define FULL_LEFT 85    // every quota at or above this makes the pet happy
#define PACE_FAST 10    // points ahead of elapsed time that count as fast
#define REACT_MS 2600   // tap reaction
#define CHEER_MS 5000   // quota came back

static const char *const NAME[2] = {"Codex", "Claude"};
static const uint32_t ACCENT[2] = {C_CODEX, C_CLAUDE};

typedef struct {
    lv_obj_t *card, *dot, *name, *tag, *value, *bar_fill, *week, *reset;
} card_t;

static orb_t *pet;
static lv_obj_t *say;
static card_t cards[2];
static lv_timer_t *react_timer;
static orb_mood_t base_mood = ORB_SLEEPY;
static char base_text[96];
static bool reacting, active = true;
static int16_t last_left[2] = {-1, -1};
static uint8_t tap_count;

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

static uint32_t level_color(int left)
{
    return left < LOW_LEFT ? C_CORAL : left < GOOD_LEFT ? C_AMBER : C_CYAN;
}

static void show_say(const char *s, uint32_t color)
{
    lv_label_set_text(say, s);
    lv_obj_set_style_text_color(say, lv_color_hex(color), 0);
}

static uint32_t mood_color(orb_mood_t mood)
{
    switch (mood) {
    case ORB_THINK: return C_AMBER;
    case ORB_SAD: return C_CORAL;
    case ORB_SLEEPY: return C_MUTED;
    default: return C_TEXT;
    }
}

static void restore(void)
{
    reacting = false;
    orb_set_mood(pet, base_mood);
    show_say(base_text, mood_color(base_mood));
}

static void react_done(lv_timer_t *t)
{
    lv_timer_pause(t);
    restore();
}

static void react(orb_mood_t mood, const char *s, uint32_t ms)
{
    reacting = true;
    orb_set_mood(pet, mood);
    show_say(s, C_CYAN);
    lv_timer_set_period(react_timer, ms);
    lv_timer_reset(react_timer);
    lv_timer_resume(react_timer);
}

static void pet_tapped(lv_event_t *e)
{
    (void)e;
    static const char *const LINES[] = {"嗨！我在幫你看著額度", "今天也一起加油！", "摸摸頭，謝謝你",
                                        "要休息一下嗎？"};
    if (base_mood == ORB_SLEEPY) {
        react(ORB_IDLE, "我醒著，等電腦傳資料過來", REACT_MS);
        return;
    }
    react(ORB_HAPPY, LINES[tap_count++ % 4], REACT_MS);
}

static void build_card(card_t *c, lv_obj_t *parent, int index)
{
    c->card = plain(parent);
    lv_obj_set_size(c->card, CARD_W, CARD_H);
    lv_obj_set_pos(c->card, (PANEL_W - 2 * CARD_W - CARD_GAP) / 2 + index * (CARD_W + CARD_GAP), CARD_Y);
    lv_obj_set_style_bg_color(c->card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c->card, 18, 0);
    lv_obj_set_style_border_color(c->card, lv_color_hex(C_CARD_LINE), 0);
    lv_obj_set_style_border_width(c->card, 1, 0);

    c->dot = plain(c->card);
    lv_obj_set_size(c->dot, 10, 10);
    lv_obj_set_pos(c->dot, CARD_PAD, 19);
    lv_obj_set_style_radius(c->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c->dot, lv_color_hex(ACCENT[index]), 0);

    c->name = text(c->card, &pet_ui_20, C_TEXT, NAME[index]);
    lv_obj_set_pos(c->name, CARD_PAD + 18, 8);
    // Top right: what the big number means, or the link state when not live.
    c->tag = text(c->card, &pet_ui_16, C_MUTED, "5 小時剩餘");
    lv_obj_align(c->tag, LV_ALIGN_TOP_RIGHT, -CARD_PAD, 12);

    c->value = text(c->card, &pet_num_44, C_DIM, "--");
    lv_obj_set_pos(c->value, CARD_PAD - 2, 38);

    lv_obj_t *track = plain(c->card);
    lv_obj_set_size(track, BAR_W, 8);
    lv_obj_set_pos(track, CARD_PAD, 94);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(C_TRACK), 0);
    c->bar_fill = plain(track);
    lv_obj_set_size(c->bar_fill, 0, 8);
    lv_obj_set_style_radius(c->bar_fill, 4, 0);
    lv_obj_set_style_bg_opa(c->bar_fill, LV_OPA_COVER, 0);

    // Two label/value rows: names on the left, values right-aligned.
    lv_obj_set_pos(text(c->card, &pet_ui_16, C_MUTED, "重置"), CARD_PAD, 108);
    c->reset = text(c->card, &pet_ui_16, C_TEXT, "--");
    lv_obj_align(c->reset, LV_ALIGN_TOP_RIGHT, -CARD_PAD, 108);
    lv_obj_set_pos(text(c->card, &pet_ui_16, C_MUTED, "本週剩餘"), CARD_PAD, 130);
    c->week = text(c->card, &pet_ui_16, C_TEXT, "--");
    lv_obj_align(c->week, LV_ALIGN_TOP_RIGHT, -CARD_PAD, 130);
}

void pet_overview_create(lv_obj_t *parent)
{
    pet = orb_create(parent, false);
    lv_obj_t *root = orb_root(pet);
    lv_obj_set_pos(root, (PANEL_W - ORB_SIZE) / 2, ORB_Y);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, pet_tapped, LV_EVENT_SHORT_CLICKED, NULL);

    say = text(parent, &pet_ui_20, C_MUTED, "");
    lv_obj_set_width(say, PANEL_W - 40);
    lv_obj_set_style_text_align(say, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(say, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(say, 20, SAY_Y);

    for (int i = 0; i < 2; ++i) build_card(&cards[i], parent, i);

    react_timer = lv_timer_create(react_done, REACT_MS, NULL);
    lv_timer_pause(react_timer);

    pet_view_t empty = {0};
    pet_overview_update(&empty);
}

static void update_card(card_t *c, const pet_provider_t *p, int index)
{
    bool fresh = p->state == PET_ONLINE || p->state == PET_PARTIAL;
    const char *tag = p->state == PET_OFFLINE ? "離線" : p->state == PET_NO_DATA ? "無資料"
                      : p->state == PET_PARTIAL ? "部分資料" : "5 小時剩餘";
    lv_label_set_text(c->tag, tag);
    lv_obj_set_style_text_color(c->tag, lv_color_hex(p->state == PET_PARTIAL ? C_AMBER : C_MUTED), 0);
    lv_obj_set_style_bg_color(c->dot, lv_color_hex(fresh ? ACCENT[index] : C_DIM), 0);
    lv_obj_set_style_text_color(c->name, lv_color_hex(fresh ? C_TEXT : C_MUTED), 0);

    if (p->short_present) {
        lv_label_set_text_fmt(c->value, "%u%%", p->short_left);
        uint32_t color = fresh ? level_color(p->short_left) : C_DIM;
        lv_obj_set_style_text_color(c->value, lv_color_hex(fresh && p->short_left >= GOOD_LEFT ? C_TEXT : color), 0);
        lv_obj_set_width(c->bar_fill, LV_MAX(p->short_left ? 8 : 0, BAR_W * p->short_left / 100));
        lv_obj_set_style_bg_color(c->bar_fill, lv_color_hex(fresh ? color : C_DIM), 0);
        if (p->countdown[0] && strcmp(p->countdown, "--") != 0) {
            if (strcmp(p->countdown, "WAIT") == 0) lv_label_set_text(c->reset, "即將");
            else lv_label_set_text(c->reset, p->countdown);
        } else {
            lv_label_set_text(c->reset, "--");
        }
    } else {
        lv_label_set_text(c->value, "--");
        lv_obj_set_style_text_color(c->value, lv_color_hex(C_DIM), 0);
        lv_obj_set_width(c->bar_fill, 0);
        lv_label_set_text(c->reset, "--");
    }
    if (p->week_present) lv_label_set_text_fmt(c->week, "%u%%", 100 - LV_MIN(p->week_used, 100));
    else lv_label_set_text(c->week, "--");
    uint32_t week_color = !fresh || !p->week_present ? C_DIM
                          : p->week_used >= 100 - LOW_LEFT ? C_AMBER : C_TEXT;
    lv_obj_set_style_text_color(c->week, lv_color_hex(week_color), 0);
    lv_obj_set_style_text_color(c->reset, lv_color_hex(fresh && p->short_present ? C_TEXT : C_DIM), 0);
}

/* Picks the pet's mood and line from the quotas it can see. */
static void judge(const pet_view_t *v, orb_mood_t *mood, char *line, size_t size)
{
    int known = 0, low = -1, fast = -1, best_pace = PACE_FAST;
    int min_left = 101, min_index = -1, empty_count = 0;
    for (int i = 0; i < 2; ++i) {
        const pet_provider_t *p = &v->provider[i];
        if (!p->short_present) continue;
        ++known;
        if (p->short_left < min_left) { min_left = p->short_left; min_index = i; }
        if (p->short_left == 0) ++empty_count;
        if (p->short_left < LOW_LEFT && (low < 0 || p->short_left < v->provider[low].short_left)) low = i;
        if (p->pace_known && p->pace > best_pace) { best_pace = p->pace; fast = i; }
    }
    if (known == 0) {
        *mood = ORB_SLEEPY;
        snprintf(line, size, v->connected ? "等電腦傳來用量…" : "等待 Bridge 連線…");
        return;
    }
    if (!v->connected) {
        *mood = ORB_SLEEPY;
        snprintf(line, size, "電腦沒連上，先顯示上次的數字");
        return;
    }
    if (empty_count == 2) {
        *mood = ORB_SAD;
        snprintf(line, size, "兩邊額度都用完了，休息一下吧");
    } else if (min_left == 0) {
        *mood = ORB_SAD;
        const pet_provider_t *p = &v->provider[min_index];
        if (p->countdown[0] && strcmp(p->countdown, "--") && strcmp(p->countdown, "WAIT"))
            snprintf(line, size, "%s 用完了，%s 後恢復", NAME[min_index], p->countdown);
        else
            snprintf(line, size, "%s 額度用完了", NAME[min_index]);
    } else if (low >= 0) {
        *mood = ORB_THINK;
        snprintf(line, size, "%s 只剩 %u%%，省著點用", NAME[low], v->provider[low].short_left);
    } else if (fast >= 0) {
        *mood = ORB_THINK;
        snprintf(line, size, "%s 用得有點快喔", NAME[fast]);
    } else if (min_left < GOOD_LEFT) {
        *mood = ORB_IDLE;
        snprintf(line, size, "還夠用，慢慢來");
    } else if (min_left >= FULL_LEFT) {
        *mood = ORB_HAPPY;
        snprintf(line, size, "額度滿滿，放心用！");
    } else {
        *mood = ORB_IDLE;
        snprintf(line, size, "額度充足，放心用");
    }
}

void pet_overview_update(const pet_view_t *v)
{
    for (int i = 0; i < 2; ++i) update_card(&cards[i], &v->provider[i], i);

    orb_mood_t mood;
    char line[sizeof(base_text)];
    judge(v, &mood, line, sizeof(line));
    bool changed = mood != base_mood || strcmp(line, base_text) != 0;
    base_mood = mood;
    snprintf(base_text, sizeof(base_text), "%s", line);

    // A window that was running low and is back near full: celebrate.
    int cheer = -1;
    for (int i = 0; i < 2; ++i) {
        const pet_provider_t *p = &v->provider[i];
        int left = p->short_present && v->connected ? p->short_left : -1;
        if (last_left[i] >= 0 && last_left[i] < GOOD_LEFT && left >= 90) cheer = i;
        last_left[i] = (int16_t)left;
    }
    if (cheer >= 0) {
        char s[64];
        snprintf(s, sizeof(s), "%s 額度恢復了！", NAME[cheer]);
        react(ORB_HAPPY, s, CHEER_MS);
    } else if (!reacting && changed) {
        restore();
    }
}

void pet_overview_set_active(bool value)
{
    if (value == active) return;
    active = value;
    orb_set_running(pet, value);
    if (!value && reacting) {
        lv_timer_pause(react_timer);
        restore();
    }
}
