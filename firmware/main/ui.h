#pragma once
#include <stdbool.h>

/*
 * Voice-remote UI: flying-orb character on a 480x480 AMOLED.
 *
 * Every ui_* function except ui_init may be called from any task. Updates are
 * queued under port->lock and applied by an LVGL timer on the LVGL task.
 * User input comes back through port->action, called on the LVGL task.
 */

#define UI_TEXT_MAX 512
#define UI_HISTORY_SHOW 4

typedef enum {
    UI_SCREEN_SETUP,     // No Wi-Fi/backend settings yet.
    UI_SCREEN_IDLE,      // Cover: floating orb, tap to talk.
    UI_SCREEN_LISTEN,    // Recording.
    UI_SCREEN_THINK,     // Mac is turning speech into text.
    UI_SCREEN_SENDING,   // Handing the command to Grok Bot.
    UI_SCREEN_CONFIRM,   // Show recognised text: send or say again.
    UI_SCREEN_DONE,      // Grok Bot accepted the command.
    UI_SCREEN_ERROR,
    UI_SCREEN_HISTORY,
    UI_SCREEN_SLEEP,
} ui_screen_t;

typedef enum {
    UI_ERR_OFFLINE,  // No connection to the Mac.
    UI_ERR_UNCLEAR,  // Speech recognition returned nothing.
    UI_ERR_SEND,     // Grok Bot webhook failed.
    UI_ERR_SETUP,    // Webhook not configured on the Mac.
    UI_ERR_STT,      // Mac speech recognition unavailable (helper missing, permission).
} ui_error_t;

typedef enum {
    UI_ACT_TAP,      // Start recording (idle) or stop and send audio (listen).
    UI_ACT_CANCEL,   // Long press while recording.
    UI_ACT_SEND,     // Confirmed the recognised text.
    UI_ACT_REDO,     // Wants to say it again.
    UI_ACT_RETRY,    // Retry sending after a webhook failure.
    UI_ACT_TIMEOUT,  // Confirmation not answered in time.
} ui_action_t;

typedef struct {
    char time[6];
    char text[160];
    bool ok;
} ui_history_item_t;

typedef struct {
    void (*action)(ui_action_t action);
    void (*brightness)(int percent);  // 0 turns the panel off.
    void (*lock)(void);
    void (*unlock)(void);
} ui_port_t;

/* Builds all screens. Call once on the LVGL task (or holding the display lock). */
void ui_init(const ui_port_t *port);

void ui_show(ui_screen_t screen);
void ui_show_confirm(const char *text);
void ui_show_done(const char *text);
void ui_show_error(ui_error_t error);
void ui_set_link(bool online);
void ui_set_battery(int percent, bool charging, bool usb_power);  // percent < 0: no battery reading
void ui_set_level(int level_0_to_1000);                          // live microphone level
void ui_set_history(const ui_history_item_t *items, int count, int total);
void ui_key(void);  // Physical key press: meaning depends on the screen.
