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
#define ORB_SIZE 240
#define ORB_Y 8
#define SAY_Y 250
#define COL_W 180
#define COL_CX0 120     // centre of the left half (Codex)
#define COL_CX1 360     // centre of the right half (Claude)
// Even rhythm: about 12 px of air between name, number, bar and reset line.
#define NAME_Y 300
#define VALUE_Y 326
#define BAR_Y 372
#define BAR_W 128
#define RESET_Y 384

#define LOW_LEFT 20     // below this the pet worries
#define GOOD_LEFT 50    // at or above this the quota is "plenty"
#define FULL_LEFT 85    // every quota at or above this makes the pet happy
#define WEEK_LOW 15     // weekly quota left below this is worth a word
#define PACE_FAST 10    // points ahead of elapsed time that count as fast
#define REACT_MS 2600   // tap reaction
#define CHEER_MS 5000   // quota came back

static const char *const NAME[2] = {"Codex", "Claude"};
static const uint32_t ACCENT[2] = {C_CODEX, C_CLAUDE};

typedef struct {
    lv_obj_t *dot, *name, *figure, *value, *pct, *bar_fill, *reset;
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

static lv_obj_t *centered(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *str,
                          int cx, int y, int w)
{
    lv_obj_t *l = text(parent, font, color, str);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, cx - w / 2, y);
    return l;
}

/* One quiet column per provider: name, what is left, a hairline bar, reset. */
static void build_card(card_t *c, lv_obj_t *parent, int index)
{
    const int cx = index == 0 ? COL_CX0 : COL_CX1;

    // The name itself is centred on the column, like the number below it;
    // the dot hangs to its left and does not push the text sideways.
    c->name = centered(parent, &pet_ui_16, C_MUTED, NAME[index], cx, NAME_Y, COL_W);
    lv_obj_set_width(c->name, LV_SIZE_CONTENT);
    lv_obj_align(c->name, LV_ALIGN_TOP_MID, cx - PANEL_W / 2, NAME_Y);
    c->dot = plain(parent);
    lv_obj_set_size(c->dot, 8, 8);
    lv_obj_set_style_radius(c->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c->dot, lv_color_hex(ACCENT[index]), 0);
    lv_obj_align_to(c->dot, c->name, LV_ALIGN_OUT_LEFT_MID, -8, 1);

    // Big number with a small "%" beside it, centred as one group, so the
    // digits themselves sit in the middle of the column.
    c->figure = plain(parent);
    lv_obj_set_size(c->figure, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c->figure, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->figure, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(c->figure, 3, 0);
    c->value = text(c->figure, &pet_num_44, C_DIM, "--");
    c->pct = text(c->figure, &pet_ui_20, C_MUTED, "%");
    lv_obj_set_style_translate_y(c->pct, 3, 0);  // share the digits' baseline
    lv_obj_add_flag(c->pct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(c->figure, LV_ALIGN_TOP_MID, cx - PANEL_W / 2, VALUE_Y);

    lv_obj_t *track = plain(parent);
    lv_obj_set_size(track, BAR_W, 4);
    lv_obj_set_pos(track, cx - BAR_W / 2, BAR_Y);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(C_TRACK), 0);
    c->bar_fill = plain(track);
    lv_obj_set_size(c->bar_fill, 0, 4);
    lv_obj_set_style_radius(c->bar_fill, 2, 0);
    lv_obj_set_style_bg_opa(c->bar_fill, LV_OPA_COVER, 0);

    c->reset = centered(parent, &pet_ui_16, C_DIM, "", cx, RESET_Y, COL_W);
}

void pet_overview_create(lv_obj_t *parent)
{
    pet = orb_create(parent, true);
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
    lv_obj_t *rule = plain(parent);
    lv_obj_set_size(rule, 1, RESET_Y + 18 - NAME_Y);
    lv_obj_set_pos(rule, PANEL_W / 2, NAME_Y + 2);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x15181D), 0);

    react_timer = lv_timer_create(react_done, REACT_MS, NULL);
    lv_timer_pause(react_timer);

    pet_view_t empty = {0};
    pet_overview_update(&empty);
}

static void update_card(card_t *c, const pet_provider_t *p, int index)
{
    bool fresh = p->state == PET_ONLINE || p->state == PET_PARTIAL;
    lv_obj_set_style_bg_color(c->dot, lv_color_hex(fresh ? ACCENT[index] : C_DIM), 0);
    lv_obj_set_style_text_color(c->name, lv_color_hex(fresh ? C_MUTED : C_DIM), 0);

    if (p->short_present) {
        lv_label_set_text_fmt(c->value, "%u", p->short_left);
        lv_obj_remove_flag(c->pct, LV_OBJ_FLAG_HIDDEN);
        uint32_t color = fresh ? level_color(p->short_left) : C_DIM;
        const uint32_t ink = fresh && p->short_left >= GOOD_LEFT ? C_TEXT : color;
        lv_obj_set_style_text_color(c->value, lv_color_hex(ink), 0);
        lv_obj_set_style_text_color(c->pct, lv_color_hex(ink), 0);
        lv_obj_set_style_text_opa(c->pct, LV_OPA_70, 0);
        lv_obj_set_width(c->bar_fill, LV_MAX(p->short_left ? 4 : 0, BAR_W * p->short_left / 100));
        lv_obj_set_style_bg_color(c->bar_fill, lv_color_hex(fresh ? color : C_DIM), 0);
    } else {
        lv_label_set_text(c->value, "--");
        lv_obj_add_flag(c->pct, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(c->value, lv_color_hex(C_DIM), 0);
        lv_obj_set_width(c->bar_fill, 0);
    }

    // One line under the bar: the link state when not live, else the reset.
    if (p->state == PET_OFFLINE) lv_label_set_text(c->reset, "離線");
    else if (p->state == PET_NO_DATA) lv_label_set_text(c->reset, "無資料");
    else if (!p->short_present || !p->countdown[0] || !strcmp(p->countdown, "--")) lv_label_set_text(c->reset, "");
    else if (!strcmp(p->countdown, "WAIT")) lv_label_set_text(c->reset, "即將重置");
    else lv_label_set_text_fmt(c->reset, "%s 後重置", p->countdown);
}

/* Picks the pet's mood and line from the quotas it can see. */
static void judge(const pet_view_t *v, orb_mood_t *mood, char *line, size_t size)
{
    int known = 0, low = -1, fast = -1, best_pace = PACE_FAST;
    int min_left = 101, min_index = -1, empty_count = 0, week_low = -1;
    for (int i = 0; i < 2; ++i) {
        const pet_provider_t *p = &v->provider[i];
        if (!p->short_present) continue;
        ++known;
        if (p->short_left < min_left) { min_left = p->short_left; min_index = i; }
        if (p->short_left == 0) ++empty_count;
        if (p->short_left < LOW_LEFT && (low < 0 || p->short_left < v->provider[low].short_left)) low = i;
        if (p->pace_known && p->pace > best_pace) { best_pace = p->pace; fast = i; }
        if (p->week_present && 100 - p->week_used < WEEK_LOW &&
            (week_low < 0 || p->week_used > v->provider[week_low].week_used)) week_low = i;
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
    } else if (week_low >= 0) {
        *mood = ORB_THINK;
        snprintf(line, size, "%s 本週只剩 %u%%", NAME[week_low], 100 - v->provider[week_low].week_used);
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
