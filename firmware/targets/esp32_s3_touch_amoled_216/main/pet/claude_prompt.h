#pragma once
/*
 * Full-screen card shown while the Claude desktop app waits for a permission
 * decision (Developer -> Hardware Buddy). The pet asks; two large buttons
 * answer "allow once" or "deny". Plain C so the simulator can render it.
 */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CLAUDE_ACTION_NONE, CLAUDE_ACTION_ALLOW, CLAUDE_ACTION_DENY } claude_action_t;
typedef enum { CLAUDE_REPLY_NONE, CLAUDE_REPLY_ALLOWED, CLAUDE_REPLY_DENIED, CLAUDE_REPLY_FAILED } claude_reply_t;

typedef struct {
    bool show;
    const char *tool;     // e.g. "Bash"
    const char *hint;     // e.g. the command line
    uint32_t waiting;     // sessions waiting for a decision
    claude_reply_t reply; // what was answered for this prompt, if anything
    uint32_t serial;      // changes with every new prompt
} claude_prompt_view_t;

/** Builds the hidden overlay on `screen`, below the 51 px header. */
void claude_prompt_create(lv_obj_t *screen);
void claude_prompt_update(const claude_prompt_view_t *view);
bool claude_prompt_visible(void);
/** Returns and clears the last button press. */
claude_action_t claude_prompt_take_action(void);

#ifdef __cplusplus
}
#endif
