/*
 * The AKENO pages of the onionHEN Toolbox (EXPERIMENTAL): the companion model as one onionHEN UI
 * document (onionHEN plugin SDK, onion.ui ABI 1), replaced as a whole when the data changes (same
 * plugin and contribution ID). onionHEN v0.9.0 renders it into PS5 Settings XML on firmware 2.x-13.x
 * (its "legacy-settings" profile), one page each time a page is opened:
 *
 *   main       AKENO NETWORK: your status, Friends, Messages, Party, Requests, Refresh, Diagnostics
 *   profile    your status (list "presence")
 *   friends    one row per friend; f.<user> a friend: message (text field "dm.<user>"),
 *              conversation, invite to / join a party
 *   messages   one row per conversation; c.<conversation> its latest messages, reply (text field
 *              "reply.<conversation>"), mark read
 *   party      members, party chat, invite (page pinvite), leave; or create, join friends' parties
 *   inbox      friend requests, invitations (accept / decline), notifications
 *   voice      party voice through the AKENO voice service (akeno-party-voice): join, microphone,
 *              mute, deafen, volumes, who is in voice, leave (voice_link.h)
 *   vroute     the speaker route (the Voice Lab's four variants) and the test sound
 *   diag       the daemon, the server connection, the voice service, the last problem
 *
 * What onionHEN v0.9.0 shows decides the layout (docs/IN_GAME_ACCESS.md, "ShellUI page rules"):
 * titles carry the information (descriptions and value columns are not reliably visible), only
 * menus link pages (no groups: leaving a nested list pops onionHEN's page stack once too often),
 * no disabled or secret nodes, and every node has a title.
 */
#ifndef AKN_COMPANION_UI_DOC_H
#define AKN_COMPANION_UI_DOC_H

#include <onion/ui.h>

#include "companion.h"

#define AKN_COMPANION_PLUGIN_ID "AKEN00001"
#define AKN_COMPANION_VERSION "1.20"
#define AKN_COMPANION_CONTRIBUTION "akeno"

/* Node IDs the plugin reacts to without an AKENO ID. */
#define AKN_EV_PRESENCE "presence"
#define AKN_EV_REFRESH "refresh"
#define AKN_EV_CREATE_PARTY "pcreate"
#define AKN_EV_NOTIFICATIONS_READ "nread"
/* Voice controls (the voice service's; "vjoin.<party>" and "vroute.<n>" carry an item). */
#define AKN_EV_VOICE_LEAVE "vleave"
#define AKN_EV_VOICE_MIC_ON "vmicon"
#define AKN_EV_VOICE_MIC_OFF "vmicoff"
#define AKN_EV_VOICE_MUTE "vmute"
#define AKN_EV_VOICE_UNMUTE "vunmute"
#define AKN_EV_VOICE_DEAFEN "vdeafen"
#define AKN_EV_VOICE_UNDEAFEN "vundeafen"
#define AKN_EV_VOICE_LOUDER "vvolup"
#define AKN_EV_VOICE_QUIETER "vvoldn"
#define AKN_EV_VOICE_GAIN_UP "vgainup"
#define AKN_EV_VOICE_GAIN_DOWN "vgaindn"
#define AKN_EV_VOICE_TEST "vtest"
#define AKN_EV_VOICE_EXIT "vexit"

/* Pages that a previous document had and this one lost (a friend dropped off the list, a
 * conversation moved down): kept for a while as a short page that says so, so that a person who has
 * one of them open can go back without onionHEN losing track of its page stack. */
#define AKN_UI_TOMBSTONES 6
#define AKN_UI_PAGE_ID_MAX 64
#define AKN_UI_PAGES_MAX (18 + AKN_UI_FRIEND_PAGES + AKN_UI_CONVS)

typedef struct {
    const char *note;                       /* the last action's result for the main page, or NULL */
    char (*tombstones)[AKN_UI_PAGE_ID_MAX]; /* page IDs */
    int tombstone_count;
} akn_doc_options;

/* Builds and validates the document; the caller destroys it. */
onion_status akn_companion_document(const akn_companion *c, const char *note, onion_ui_document **out);
onion_status akn_companion_document_ex(const akn_companion *c, const akn_doc_options *options, onion_ui_document **out);

/* The pages a document for this model has, in document order. Returns the count. */
int akn_companion_page_ids(const akn_companion *c, char ids[][AKN_UI_PAGE_ID_MAX], int max);

#endif
