/*
 * What a press on an AKENO page does. onionHEN delivers the pressed node's ID and, for a list or a
 * text field, its value (the text typed with the PS5 keyboard); the node ID names the action and the
 * AKENO item it is for ("reply.<conversation ID>", "racc.<request ID>"), so a list that changed since
 * the page was drawn can never send a reply to another conversation. Every write goes through the
 * daemon's native write list, which the server checks again. The PC check tool calls the same
 * function, so the end-to-end test drives exactly what a press on the console does.
 */
#ifndef AKN_COMPANION_ACTIONS_H
#define AKN_COMPANION_ACTIONS_H

#include "companion.h"
#include "fetch.h"

typedef struct {
    int handled;          /* the node is one of the companion's controls */
    int ok;               /* the change was made */
    int notify;           /* worth a system notification (writes, not Refresh) */
    unsigned reload;      /* AKN_LOAD_* to read again */
    char message[200];    /* "Sent to Bob." or why it failed, for the notification and the page */
} akn_action_result;

/* The verb of a control's node ID ("reply" for "reply.<conversation ID>", "pinv" for both
 * "pinv.<user>" and "fpinv.<user>") and, through item_id, its AKENO ID ("" when it has none). NULL
 * when the node ID is not one of the companion's controls or its ID is not a valid server ID. The
 * pages bind every control to its verb (the binding key); tests check that every control routes. */
const char *akn_companion_action_verb(const char *node_id, const char **item_id);

/* model: the model the page was built from (party ID, names, the newest message to mark read). */
void akn_companion_action(akn_daemon_link *link, const akn_companion *model, const char *node_id, const char *value, akn_action_result *out);

/* Fills 16 bytes for a clientMessageId: /dev/urandom, else a clock-based fallback (the ID only has
 * to be unique: it makes a retried send idempotent). */
void akn_companion_random16(unsigned char out[16]);

#endif
