#include "ui_doc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SEP " \xC2\xB7 " /* " · " */

typedef struct {
    onion_ui_document *doc;
    onion_status status;
    int labels, menus;
} builder;

/* Text for the Toolbox: within its size, cut at a UTF-8 character boundary with "..." when cut,
 * never empty (onionHEN refuses a node without a title). */
static void fit(char *dst, size_t len, const char *src, const char *fallback) {
    if (!src || !src[0]) src = fallback;
    size_t n = strlen(src);
    if (n < len) {
        memcpy(dst, src, n + 1);
        akn_utf8_whole(dst); /* a model text that was cut by bytes before */
        if (!dst[0]) snprintf(dst, len, "%s", fallback);
        return;
    }
    n = len - 4;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    snprintf(dst, len, "%.*s...", (int)n, src);
}

static onion_ui_node_desc_v1 node(uint32_t kind, const char *id, const char *parent, const char *title) {
    onion_ui_node_desc_v1 n;
    memset(&n, 0, sizeof(n));
    n.struct_size = sizeof(n);
    n.abi_version = ONION_UI_ABI_VERSION;
    n.kind = kind;
    snprintf(n.id, sizeof(n.id), "%s", id);
    snprintf(n.parent_id, sizeof(n.parent_id), "%s", parent ? parent : "");
    fit(n.title, sizeof(n.title), title, "-");
    return n;
}

static void add(builder *b, const onion_ui_node_desc_v1 *n) {
    if (b->status == ONION_OK) b->status = onion_ui_document_add_node(b->doc, n);
}

static void describe(onion_ui_node_desc_v1 *n, const char *description) {
    if (description && description[0]) fit(n->description, sizeof(n->description), description, "");
}

static void page(builder *b, const char *id, const char *title) {
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_PAGE, id, NULL, title);
    add(b, &n);
}

static void label(builder *b, const char *parent, const char *title, const char *description) {
    char id[24];
    snprintf(id, sizeof(id), "l%d", b->labels++);
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_LABEL, id, parent, title);
    describe(&n, description);
    add(b, &n);
}

__attribute__((format(printf, 3, 4))) static void labelf(builder *b, const char *parent, const char *fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    label(b, parent, text, NULL);
}

static void menu(builder *b, const char *parent, const char *target, const char *title, const char *description) {
    char id[24];
    snprintf(id, sizeof(id), "m%d", b->menus++);
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_MENU, id, parent, title);
    snprintf(n.target_id, sizeof(n.target_id), "%s", target);
    describe(&n, description);
    add(b, &n);
}

/* key: the binding key (the action's verb); the node ID names the item. confirm: the question
 * the PS5 asks first, or NULL. */
static void action(builder *b, const char *parent, const char *id, const char *key, const char *title, const char *confirm) {
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_ACTION, id, parent, title);
    n.binding = ONION_UI_BINDING_EVENT;
    snprintf(n.binding_key, sizeof(n.binding_key), "%s", key);
    if (confirm) {
        n.flags = ONION_UI_NODE_FLAG_CONFIRM;
        describe(&n, confirm);
    }
    add(b, &n);
}

/* A text field: the PS5 keyboard opens on it; onionHEN delivers the typed text. */
static void input(builder *b, const char *parent, const char *id, const char *key, const char *title, const char *description) {
    onion_ui_node_desc_v1 n = node(ONION_UI_NODE_INPUT, id, parent, title);
    n.value_type = ONION_UI_VALUE_STRING;
    n.binding = ONION_UI_BINDING_EVENT;
    n.max_length = AKN_UI_REPLY_MAX;
    snprintf(n.binding_key, sizeof(n.binding_key), "%s", key);
    describe(&n, description);
    add(b, &n);
}

static void item_id(char *out, size_t len, const char *prefix, const char *id) { snprintf(out, len, "%s%s", prefix, id); }

static void voice_title(const akn_companion *c, char *t, size_t len);

static const char *state_hint(akn_companion_state s) {
    switch (s) {
    case AKN_COMP_NO_DAEMON: return "Load akeno-network-ps5-native-ipc.elf with your ELF loader (port 9021), then choose Refresh.";
    case AKN_COMP_NO_INTERFACE: return "The release daemon is running. Load the native-IPC daemon instead, then choose Refresh.";
    case AKN_COMP_NO_KEY: return "Restart the daemon so that it writes /data/akeno/native.key again, then choose Refresh.";
    case AKN_COMP_NOT_PAIRED: return "Open the AKENO page in the PS5 browser and pair this console, then choose Refresh.";
    case AKN_COMP_READY: break;
    }
    return "";
}

/* ---- pages ------------------------------------------------------------------------------- */

static void main_page(builder *b, const akn_companion *c, const char *note) {
    char t[256];
    page(b, "main", "AKENO NETWORK");
    if (note && note[0]) label(b, "main", note, NULL);
    if (c->state != AKN_COMP_READY) {
        label(b, "main", c->status_line, c->status_line);
        label(b, "main", state_hint(c->state), NULL);
        action(b, "main", AKN_EV_REFRESH, AKN_EV_REFRESH, "Refresh", NULL);
        menu(b, "main", "diag", "Diagnostics & help", "What the companion sees and what to check");
        label(b, "main", "AKENO In-Game Companion " AKN_COMPANION_VERSION " (experimental)", NULL);
        return;
    }
    snprintf(t, sizeof(t), "%s%s" SEP "%s", c->online_id[0] ? "@" : "", c->online_id[0] ? c->online_id : "Your profile",
             akn_companion_presence_label(c->presence));
    if (c->console_game[0]) snprintf(t + strlen(t), sizeof(t) - strlen(t), SEP "%s", c->console_game);
    menu(b, "main", "profile", t, "Your status and profile");
    if (c->friends_total == 0) snprintf(t, sizeof(t), "Friends");
    else if (c->friends_playing) snprintf(t, sizeof(t), "Friends" SEP "%d online" SEP "%d playing", c->friends_online, c->friends_playing);
    else snprintf(t, sizeof(t), "Friends" SEP "%d online", c->friends_online);
    menu(b, "main", "friends", t, "Who is online and what they play; message or invite them");
    if (c->unread_messages) snprintf(t, sizeof(t), "Messages" SEP "%d unread", c->unread_messages);
    else snprintf(t, sizeof(t), "Messages");
    menu(b, "main", "messages", t, "Your conversations: read and reply");
    if (c->in_party) snprintf(t, sizeof(t), "Party" SEP "%s" SEP "%d/%d", c->party_name, c->party_members, c->party_max);
    else if (c->party_invites) snprintf(t, sizeof(t), "Party" SEP "%d invitation%s", c->party_invites, c->party_invites == 1 ? "" : "s");
    else snprintf(t, sizeof(t), "Party" SEP "not in a party");
    menu(b, "main", "party", t, "Members and party chat; create or join a party");
    if (c->voice.service == AKN_VOICE_SVC_READY && (c->voice.in_voice || c->in_party)) {
        voice_title(c, t, sizeof(t));
        menu(b, "main", "voice", t, "Party voice while you play: join, microphone, mute, leave");
    }
    int waiting = c->friend_requests + c->party_invites + c->other_invites;
    if (waiting) snprintf(t, sizeof(t), "Requests & invitations" SEP "%d waiting", waiting);
    else if (c->notifications_unread) snprintf(t, sizeof(t), "Requests & invitations" SEP "%d new", c->notifications_unread);
    else snprintf(t, sizeof(t), "Requests & invitations");
    menu(b, "main", "inbox", t, "Friend requests, invitations and notifications");
    if (c->diag.realtime[0] && strcmp(c->diag.realtime, "connected") != 0) {
        snprintf(t, sizeof(t), "AKENO server: %s (live updates paused)", c->diag.realtime);
        label(b, "main", t, c->diag.realtime_detail);
    }
    action(b, "main", AKN_EV_REFRESH, AKN_EV_REFRESH, "Refresh", NULL);
    menu(b, "main", "diag", "Diagnostics & help", "What the companion sees and what to check");
    label(b, "main", "AKENO In-Game Companion " AKN_COMPANION_VERSION " (experimental)",
          "Reads the AKENO daemon on this console. AKENO NETWORK Native has the full app.");
}

static void profile_page(builder *b, const akn_companion *c) {
    page(b, "profile", "Your AKENO profile");
    if (c->display_name[0]) labelf(b, "profile", "%s (@%s)", c->display_name, c->online_id);
    else labelf(b, "profile", "@%s", c->online_id);
    labelf(b, "profile", "Status: %s", akn_companion_presence_label(c->presence));
    if (c->console_game[0]) labelf(b, "profile", "On this console: %s", c->console_game);
    if (c->writes) {
        onion_ui_node_desc_v1 n = node(ONION_UI_NODE_LIST, AKN_EV_PRESENCE, "profile", "Status");
        n.value_type = ONION_UI_VALUE_STRING;
        n.binding = ONION_UI_BINDING_EVENT;
        snprintf(n.binding_key, sizeof(n.binding_key), AKN_EV_PRESENCE);
        snprintf(n.value, sizeof(n.value), "%s", c->presence[0] ? c->presence : "online");
        describe(&n, "Who sees you online");
        add(b, &n);
        static const char *const values[] = { "online", "away", "dnd", "invisible" };
        static const char *const titles[] = { "Online", "Away", "Busy", "Invisible" };
        for (int i = 0; i < 4; i++) {
            char id[32];
            snprintf(id, sizeof(id), "presence.%s", values[i]);
            n = node(ONION_UI_NODE_LIST_ITEM, id, AKN_EV_PRESENCE, titles[i]);
            n.value_type = ONION_UI_VALUE_STRING;
            snprintf(n.value, sizeof(n.value), "%s", values[i]);
            add(b, &n);
        }
    } else {
        label(b, "profile", "Status changes need the native-IPC daemon", "This daemon's native interface is read-only.");
    }
    label(b, "profile", "Picture, profile and privacy: on the AKENO website",
          "The daemon's native interface does not change profiles or privacy settings.");
}

static void friend_pages(builder *b, const akn_companion *c) {
    char t[256], id[64];
    if (c->friends_total == 0) snprintf(t, sizeof(t), "Friends");
    else snprintf(t, sizeof(t), "Friends" SEP "%d online of %d", c->friends_online, c->friends_total);
    page(b, "friends", t);
    for (int i = 0; i < c->friends_shown; i++) {
        const akn_ui_friend *f = &c->friends[i];
        snprintf(t, sizeof(t), "%s" SEP "%s", f->name, f->line);
        if (i < AKN_UI_FRIEND_PAGES) {
            item_id(id, sizeof(id), "f.", f->id);
            menu(b, "friends", id, t, f->status_message);
        } else {
            label(b, "friends", t, f->status_message);
        }
    }
    if (c->friends_total > c->friends_shown) labelf(b, "friends", "+%d more in AKENO Native", c->friends_total - c->friends_shown);
    if (c->friend_requests) {
        snprintf(t, sizeof(t), "Friend requests" SEP "%d", c->friend_requests);
        menu(b, "friends", "inbox", t, NULL);
    }
    if (c->friends_total == 0) label(b, "friends", "No AKENO friends yet. Add friends on the AKENO website.", NULL);

    for (int i = 0; i < c->friends_shown && i < AKN_UI_FRIEND_PAGES; i++) {
        const akn_ui_friend *f = &c->friends[i];
        char pid[64];
        item_id(pid, sizeof(pid), "f.", f->id);
        page(b, pid, f->name);
        labelf(b, pid, "@%s" SEP "%s", f->online_id, f->line);
        if (f->status_message[0]) labelf(b, pid, "\"%s\"", f->status_message);
        if (c->writes) {
            item_id(id, sizeof(id), "dm.", f->id);
            snprintf(t, sizeof(t), "Send %s a message", f->name);
            input(b, pid, id, "dm", t, "Opens the PS5 keyboard. Up to 250 characters.");
        }
        if (f->conv_id[0]) {
            item_id(id, sizeof(id), "c.", f->conv_id);
            if (f->conv_unread) snprintf(t, sizeof(t), "Conversation" SEP "%d unread", f->conv_unread);
            else snprintf(t, sizeof(t), "Conversation");
            menu(b, pid, id, t, "The latest messages");
        }
        if (f->in_my_party) {
            label(b, pid, "In your party", NULL);
        } else if (c->writes && c->in_party && f->online) {
            item_id(id, sizeof(id), "fpinv.", f->id);
            snprintf(t, sizeof(t), "Invite %s to your party", f->name);
            action(b, pid, id, "pinv", t, NULL);
        }
        if (c->writes && !c->in_party && f->party_id[0]) {
            item_id(id, sizeof(id), "fpjoin.", f->id);
            snprintf(t, sizeof(t), "Join %s's party \"%s\"" SEP "%d/%d", f->name, f->party_name, f->party_members, f->party_max);
            action(b, pid, id, "pjoin", t, NULL);
        }
    }
}

static void message_pages(builder *b, const akn_companion *c) {
    char t[512], id[64];
    if (c->unread_messages) snprintf(t, sizeof(t), "Messages" SEP "%d unread", c->unread_messages);
    else snprintf(t, sizeof(t), "Messages");
    page(b, "messages", t);
    for (int i = 0; i < c->conversations_shown; i++) {
        const akn_ui_conv *v = &c->conversations[i];
        char head[120];
        if (v->unread) snprintf(head, sizeof(head), "%s (%d)", v->title, v->unread);
        else snprintf(head, sizeof(head), "%s", v->title);
        if (v->preview[0]) snprintf(t, sizeof(t), "%s" SEP "%s%s", head, v->preview_mine ? "You: " : "", v->preview);
        else snprintf(t, sizeof(t), "%s", head);
        item_id(id, sizeof(id), "c.", v->id);
        menu(b, "messages", id, t, v->kind == AKN_CONV_PARTY ? "Party chat" : v->kind == AKN_CONV_GROUP ? "Group chat" : NULL);
    }
    if (c->conversations_total > c->conversations_shown)
        labelf(b, "messages", "+%d more in AKENO Native", c->conversations_total - c->conversations_shown);
    if (!c->conversations_total) label(b, "messages", "No conversations yet. Write to a friend from their page.", NULL);

    for (int i = 0; i < c->conversations_shown; i++) {
        const akn_ui_conv *v = &c->conversations[i];
        char pid[64];
        item_id(pid, sizeof(pid), "c.", v->id);
        if (v->unread) snprintf(t, sizeof(t), "%s" SEP "%d unread", v->title, v->unread);
        else snprintf(t, sizeof(t), "%s", v->title);
        page(b, pid, t);
        if (v->thread.state < 0) label(b, pid, "The latest messages could not be loaded", v->thread.error);
        else if (v->thread.state == 0) label(b, pid, "Messages are loading. Choose Refresh on the AKENO page.", NULL);
        else if (v->thread.count == 0) label(b, pid, "No messages yet", NULL);
        for (int m = 0; v->thread.state > 0 && m < v->thread.count; m++) {
            const akn_ui_message *msg = &v->thread.items[m];
            snprintf(t, sizeof(t), "%s: %s", msg->sender, msg->body);
            label(b, pid, t, msg->body);
        }
        if (c->writes && v->can_send) {
            item_id(id, sizeof(id), "reply.", v->id);
            input(b, pid, id, "reply", "Reply...", "Opens the PS5 keyboard. Up to 250 characters.");
        } else if (!v->can_send) {
            label(b, pid, "You cannot write in this conversation", NULL);
        }
        if (c->writes && v->unread) {
            item_id(id, sizeof(id), "read.", v->id);
            action(b, pid, id, "read", "Mark as read", NULL);
        }
    }
}

static void party_pages(builder *b, const akn_companion *c) {
    char t[256], id[64];
    if (!c->in_party) {
        page(b, "party", "Party");
        label(b, "party", "You are not in a party", NULL);
        if (c->writes) input(b, "party", AKN_EV_CREATE_PARTY, "pcreate", "Create a party", "Type a name, or leave it empty. Your friends can join.");
        if (c->party_invites) {
            snprintf(t, sizeof(t), "Party invitations" SEP "%d", c->party_invites);
            menu(b, "party", "inbox", t, "Accept or decline them");
        }
        for (int i = 0; i < c->joinable_shown; i++) {
            if (c->writes) {
                item_id(id, sizeof(id), "pjoin.", c->joinable[i].id);
                snprintf(t, sizeof(t), "Join %s", c->joinable[i].line);
                action(b, "party", id, "pjoin", t, NULL);
            } else {
                label(b, "party", c->joinable[i].line, NULL);
            }
        }
        if (!c->joinable_shown && !c->party_invites) label(b, "party", "None of your friends' parties is open right now", NULL);
        voice_title(c, t, sizeof(t));
        menu(b, "party", "voice", t, "Party voice: join it once you are in a party");
        return;
    }
    snprintf(t, sizeof(t), "Party" SEP "%s" SEP "%d/%d", c->party_name, c->party_members, c->party_max);
    page(b, "party", t);
    for (int i = 0; i < c->members_shown; i++) {
        const akn_ui_member *m = &c->members[i];
        snprintf(t, sizeof(t), "%s%s%s%s", m->name, m->me ? " (you)" : "", m->leader ? SEP "leader" : "",
                 m->voice_connected ? (m->voice_muted == 1 ? SEP "voice, muted" : SEP "in voice") : "");
        label(b, "party", t, NULL);
    }
    if (c->party_members > c->members_shown) labelf(b, "party", "+%d more", c->party_members - c->members_shown);
    label(b, "party", c->party_voice, NULL);
    voice_title(c, t, sizeof(t));
    menu(b, "party", "voice", t, "Join the party's voice chat; microphone, mute, deafen, volumes, leave");
    if (c->party_conv_id[0] && akn_companion_conv(c, c->party_conv_id)) {
        item_id(id, sizeof(id), "c.", c->party_conv_id);
        menu(b, "party", id, "Party chat", "The latest messages; reply");
    }
    if (c->writes && c->invite_targets_shown) {
        snprintf(t, sizeof(t), "Invite friends" SEP "%d online", c->invite_targets_shown);
        menu(b, "party", "pinvite", t, NULL);
    }
    if (c->party_invites) {
        snprintf(t, sizeof(t), "Party invitations" SEP "%d", c->party_invites);
        menu(b, "party", "inbox", t, NULL);
    }
    if (c->writes && c->party_id[0]) {
        item_id(id, sizeof(id), "pleave.", c->party_id);
        snprintf(t, sizeof(t), "Leave the party \"%s\"?", c->party_name);
        action(b, "party", id, "pleave", "Leave the party", t);
    }
    if (c->writes && c->invite_targets_shown) {
        snprintf(t, sizeof(t), "Invite to %s", c->party_name);
        page(b, "pinvite", t);
        for (int i = 0; i < c->invite_targets_shown; i++) {
            item_id(id, sizeof(id), "pinv.", c->invite_targets[i].id);
            snprintf(t, sizeof(t), "Invite %s", c->invite_targets[i].name);
            action(b, "pinvite", id, "pinv", t, NULL);
        }
    }
}

/* ---- voice (the AKENO voice service) ------------------------------------------------------ */

static int voice_is(const akn_companion *c, const char *net) { return strcmp(c->voice.net, net) == 0; }

/* "Voice chat · Connected · 3 in voice" for the menus. */
static void voice_title(const akn_companion *c, char *t, size_t len) {
    const akn_ui_voice *v = &c->voice;
    switch (v->service) {
    case AKN_VOICE_SVC_ABSENT: snprintf(t, len, "Voice chat" SEP "voice service not running"); return;
    case AKN_VOICE_SVC_REFUSED: snprintf(t, len, "Voice chat" SEP "voice service needs a restart"); return;
    case AKN_VOICE_SVC_INCOMPATIBLE: snprintf(t, len, "Voice chat" SEP "voice service version does not fit"); return;
    case AKN_VOICE_SVC_READY: break;
    }
    if (!v->in_voice) {
        snprintf(t, len, "Voice chat" SEP "%s", voice_is(c, "disconnected") ? "disconnected" : "join");
        return;
    }
    int others = 0;
    for (int i = 0; i < v->people_count; i++) others += !v->people[i].self;
    if (v->people_total > v->people_count) others += v->people_total - v->people_count;
    if (voice_is(c, "connected"))
        snprintf(t, len, "Voice chat" SEP "Connected" SEP "%d other%s" SEP "mic %s", others, others == 1 ? "" : "s",
                 !v->mic_enabled ? "off" : v->muted ? "muted" : "on");
    else snprintf(t, len, "Voice chat" SEP "%s", voice_is(c, "reconnecting") ? "Reconnecting" : "Connecting");
}

static void voice_service_help(builder *b, const akn_companion *c) {
    switch (c->voice.service) {
    case AKN_VOICE_SVC_ABSENT:
        label(b, "voice", "The AKENO voice service is not running", NULL);
        label(b, "voice", "Send akeno-party-voice.elf to port 9021 after the daemon, then open this page again", NULL);
        break;
    case AKN_VOICE_SVC_REFUSED:
        label(b, "voice", "The voice service refused the daemon's key", NULL);
        label(b, "voice", "It was started before the daemon restarted: send akeno-party-voice.elf again", NULL);
        break;
    case AKN_VOICE_SVC_INCOMPATIBLE:
        label(b, "voice", "This voice service does not fit plugin " AKN_COMPANION_VERSION, NULL);
        label(b, "voice", "Use the akeno-party-voice.elf from the same package", NULL);
        break;
    case AKN_VOICE_SVC_READY: break;
    }
}

static void voice_page(builder *b, const akn_companion *c) {
    char t[256], q[256], id[64];
    const akn_ui_voice *v = &c->voice;
    if (c->in_party) snprintf(t, sizeof(t), "Voice chat" SEP "%s", c->party_name);
    else snprintf(t, sizeof(t), "Voice chat");
    page(b, "voice", t);
    if (v->service != AKN_VOICE_SVC_READY) {
        voice_service_help(b, c);
        return;
    }
    labelf(b, "voice", "Status: %s", v->summary[0] ? v->summary : "Not in voice");
    int here = v->in_voice && c->in_party && strcmp(v->party_id, c->party_id) == 0;
    if (!v->in_voice) {
        if (v->net_error[0]) labelf(b, "voice", "Last: %s", v->net_error);
        if (!c->in_party) {
            label(b, "voice", "Join or create a party first (Party page)", NULL);
        } else if (c->writes && akn_companion_id_ok(c->party_id)) {
            item_id(id, sizeof(id), "vjoin.", c->party_id);
            snprintf(q, sizeof(q), "Join the voice chat of \"%s\"? Your microphone stays off until you turn it on.", c->party_name);
            action(b, "voice", id, "vjoin", "Join voice chat", q);
        }
    } else {
        if (!here) label(b, "voice", c->in_party ? "This is the voice chat of another party" : "You left the party; leave its voice chat too", NULL);
        if (v->net_error[0] && !voice_is(c, "connected")) labelf(b, "voice", "Last: %s", v->net_error);
        for (int i = 0; i < v->people_count; i++) {
            const akn_ui_voice_person *p = &v->people[i];
            labelf(b, "voice", "%s%s%s", p->name, p->self ? " (you)" : "",
                   p->muted ? SEP "muted" : p->speaking ? SEP "speaking" : "");
        }
        if (v->people_total > v->people_count) labelf(b, "voice", "+%d more in voice", v->people_total - v->people_count);
        /* microphone: off until you turn it on (a confirmation), then mute / unmute */
        if (!v->mic_enabled) {
            if (strcmp(v->mic, "unavailable") == 0 || strcmp(v->mic, "failed") == 0)
                labelf(b, "voice", "Microphone: %s (0x%08llX)", v->mic, (unsigned long long)v->mic_rc & 0xFFFFFFFFull);
            action(b, "voice", AKN_EV_VOICE_MIC_ON, "vmicon", strcmp(v->mic, "off") == 0 ? "Turn on microphone" : "Try the microphone again",
                   "Turn on your microphone? The party hears you while you are not muted. Nothing is recorded.");
        } else {
            if (v->muted) action(b, "voice", AKN_EV_VOICE_UNMUTE, "vunmute", "Unmute microphone", NULL);
            else action(b, "voice", AKN_EV_VOICE_MUTE, "vmute", "Mute microphone", NULL);
            action(b, "voice", AKN_EV_VOICE_MIC_OFF, "vmicoff", "Turn off microphone", NULL);
        }
        if (v->deafened) action(b, "voice", AKN_EV_VOICE_UNDEAFEN, "vundeafen", "Undeafen (hear the party again)", NULL);
        else action(b, "voice", AKN_EV_VOICE_DEAFEN, "vdeafen", "Deafen (hear nobody, also mutes)", NULL);
        snprintf(t, sizeof(t), "Voice volume %d%%" SEP "louder", v->volume);
        action(b, "voice", AKN_EV_VOICE_LOUDER, "vvolup", t, NULL);
        snprintf(t, sizeof(t), "Voice volume %d%%" SEP "quieter", v->volume);
        action(b, "voice", AKN_EV_VOICE_QUIETER, "vvoldn", t, NULL);
        snprintf(t, sizeof(t), "Microphone gain %d%%" SEP "higher", v->mic_gain);
        action(b, "voice", AKN_EV_VOICE_GAIN_UP, "vgainup", t, NULL);
        snprintf(t, sizeof(t), "Microphone gain %d%%" SEP "lower", v->mic_gain);
        action(b, "voice", AKN_EV_VOICE_GAIN_DOWN, "vgaindn", t, NULL);
    }
    snprintf(t, sizeof(t), "Speaker route %d" SEP "test sound", v->route);
    menu(b, "voice", "vroute", t, "Which audio output the voice service uses; try them while a friend talks");
    labelf(b, "voice", "Measured: network %s" SEP "mic sent %s" SEP "speaker output %s", v->ev_network ? "yes" : "no", v->ev_mic ? "yes" : "no",
           v->ev_speaker ? "yes" : "no");
    if (v->in_voice) action(b, "voice", AKN_EV_VOICE_LEAVE, "vleave", "Leave voice chat", "Leave the voice chat?");
    label(b, "voice", "Circle goes back; voice keeps running in the game", NULL);
}

static void route_page(builder *b, const akn_companion *c) {
    char t[256], id[32];
    const akn_ui_voice *v = &c->voice;
    snprintf(t, sizeof(t), "Speaker route" SEP "now %d", v->route);
    page(b, "vroute", t);
    label(b, "vroute", "Route N plays N beeps. Note which routes you hear (TV or headset).", NULL);
    static const char *const names[] = { "system user, main port", "your user, main port", "your user, voice port (unverified)",
                                         "your user, personal port (unverified)" };
    for (int i = 1; i <= 4; i++) {
        snprintf(id, sizeof(id), "vroute.%d", i);
        snprintf(t, sizeof(t), "Route %d: %s%s", i, names[i - 1], v->route == i ? " (current)" : "");
        action(b, "vroute", id, "vroute", t, NULL);
    }
    snprintf(t, sizeof(t), "Play the test sound (%d beep%s)", v->route, v->route == 1 ? "" : "s");
    action(b, "vroute", AKN_EV_VOICE_TEST, "vtest", t, NULL);
    labelf(b, "vroute", "Output: %s" SEP "paced %s", v->out, v->paced);
}

static void inbox_page(builder *b, const akn_companion *c) {
    char t[256], q[256], id[64];
    page(b, "inbox", "Requests & invitations");
    for (int i = 0; i < c->requests_shown; i++) {
        const akn_ui_request *r = &c->requests[i];
        if (!c->writes) {
            labelf(b, "inbox", "Friend request: %s (@%s)", r->name, r->online_id);
            continue;
        }
        item_id(id, sizeof(id), "racc.", r->id);
        snprintf(t, sizeof(t), "Accept friend request: %s (@%s)", r->name, r->online_id);
        action(b, "inbox", id, "racc", t, NULL);
        item_id(id, sizeof(id), "rdec.", r->id);
        snprintf(t, sizeof(t), "Decline: %s", r->name);
        snprintf(q, sizeof(q), "Decline %s's friend request?", r->name);
        action(b, "inbox", id, "rdec", t, q);
    }
    if (c->friend_requests > c->requests_shown) labelf(b, "inbox", "+%d more friend requests on the AKENO website", c->friend_requests - c->requests_shown);
    for (int i = 0; i < c->invites_shown; i++) {
        const akn_ui_invite *v = &c->invites[i];
        if (!c->writes) {
            labelf(b, "inbox", "Invitation: %s", v->line);
            continue;
        }
        item_id(id, sizeof(id), "iacc.", v->id);
        snprintf(t, sizeof(t), "Accept: %s", v->line);
        action(b, "inbox", id, "iacc", t, NULL);
        item_id(id, sizeof(id), "idec.", v->id);
        snprintf(t, sizeof(t), "Decline: %s", v->line);
        snprintf(q, sizeof(q), "Decline this invitation (%s)?", v->line);
        action(b, "inbox", id, "idec", t, q);
    }
    for (int i = 0; i < c->notifications_shown; i++) label(b, "inbox", c->notifications[i], NULL);
    if (c->writes && c->notifications_unread) {
        snprintf(t, sizeof(t), "Mark notifications as read" SEP "%d new", c->notifications_unread);
        action(b, "inbox", AKN_EV_NOTIFICATIONS_READ, "nread", t, NULL);
    }
    if (!c->requests_shown && !c->invites_shown && !c->notifications_shown) label(b, "inbox", "Nothing new", NULL);
}

static void clock_text(long long t, char *out, size_t len) {
    if (t <= 0) {
        snprintf(out, len, "never");
        return;
    }
    long long s = t % 86400;
    snprintf(out, len, "%02lld:%02lld:%02lld UTC", s / 3600, (s / 60) % 60, s % 60);
}

static void diag_page(builder *b, const akn_companion *c) {
    char when[32];
    const akn_ui_diag *d = &c->diag;
    page(b, "diag", "AKENO diagnostics");
    labelf(b, "diag", "Daemon: %s", c->status_line);
    if (d->daemon_ipc) labelf(b, "diag", "Native interface %d" SEP "daemon %s (%s)", d->daemon_ipc, d->daemon_version, d->daemon_build);
    if (c->state != AKN_COMP_READY) label(b, "diag", state_hint(c->state), NULL);
    if (d->realtime[0]) {
        char t[256];
        snprintf(t, sizeof(t), "AKENO server: %s", d->realtime);
        label(b, "diag", t, d->realtime_detail);
    }
    if (c->online_id[0]) labelf(b, "diag", "Account: @%s%s", c->online_id, c->writes ? "" : SEP "read-only");
    clock_text(d->loaded_at, when, sizeof(when));
    labelf(b, "diag", "Last update: %s" SEP "%lu requests, %lu failed", when, d->requests, d->failures);
    if (d->last_error[0]) labelf(b, "diag", "Last problem: %s", d->last_error);
    label(b, "diag", "Pages show new data when you open them again", "onionHEN v0.9.0 draws a page when it is opened.");
    if (c->voice.service == AKN_VOICE_SVC_READY) {
        labelf(b, "diag", "Voice service %s" SEP "%s", c->voice.version, c->voice.summary[0] ? c->voice.summary : "running");
        labelf(b, "diag", "Voice packets: %lu sent, %lu received" SEP "report: /data/akeno-party-voice/report.txt", c->voice.sent, c->voice.received);
        action(b, "diag", AKN_EV_VOICE_EXIT, "vexit", "Stop the voice service", "Stop the AKENO voice service? Voice ends until you send it again.");
    } else {
        label(b, "diag", c->voice.service == AKN_VOICE_SVC_ABSENT ? "Voice service: not running (akeno-party-voice.elf, port 9021)"
                                                                   : "Voice service: running, but it does not answer this plugin",
              NULL);
    }
    label(b, "diag", "Plugin AKEN00001 " AKN_COMPANION_VERSION SEP "log: /data/OnionHEN/OnionHEN.log", NULL);
}

static void tombstone_pages(builder *b, const akn_doc_options *o) {
    for (int i = 0; o && i < o->tombstone_count && i < AKN_UI_TOMBSTONES; i++) {
        page(b, o->tombstones[i], "AKENO NETWORK");
        label(b, o->tombstones[i], "This item changed or is no longer listed. Go back and open it again.", NULL);
    }
}

int akn_companion_page_ids(const akn_companion *c, char ids[][AKN_UI_PAGE_ID_MAX], int max) {
    int n = 0;
#define PAGE_ID(...)                                                   \
    do {                                                               \
        if (n < max) snprintf(ids[n], AKN_UI_PAGE_ID_MAX, __VA_ARGS__); \
        n++;                                                           \
    } while (0)
    PAGE_ID("main");
    PAGE_ID("diag");
    if (c->state == AKN_COMP_READY) {
        PAGE_ID("profile");
        PAGE_ID("friends");
        PAGE_ID("messages");
        PAGE_ID("party");
        PAGE_ID("inbox");
        PAGE_ID("voice");
        if (c->voice.service == AKN_VOICE_SVC_READY) PAGE_ID("vroute");
        for (int i = 0; i < c->friends_shown && i < AKN_UI_FRIEND_PAGES; i++) PAGE_ID("f.%s", c->friends[i].id);
        for (int i = 0; i < c->conversations_shown; i++) PAGE_ID("c.%s", c->conversations[i].id);
        if (c->in_party && c->writes && c->invite_targets_shown) PAGE_ID("pinvite");
    }
#undef PAGE_ID
    return n < max ? n : max;
}

onion_status akn_companion_document_ex(const akn_companion *c, const akn_doc_options *o, onion_ui_document **out) {
    *out = NULL;
    onion_ui_document_desc_v1 desc;
    memset(&desc, 0, sizeof(desc));
    desc.struct_size = sizeof(desc);
    desc.abi_version = ONION_UI_ABI_VERSION;
    desc.priority = 50;
    snprintf(desc.plugin_id, sizeof(desc.plugin_id), AKN_COMPANION_PLUGIN_ID);
    snprintf(desc.contribution_id, sizeof(desc.contribution_id), AKN_COMPANION_CONTRIBUTION);
    snprintf(desc.title, sizeof(desc.title), "AKENO NETWORK");
    snprintf(desc.description, sizeof(desc.description), "Friends, messages, party, voice and invitations while you play");
    snprintf(desc.root_page_id, sizeof(desc.root_page_id), "main");
    builder b;
    memset(&b, 0, sizeof(b));
    b.status = onion_ui_document_create(&desc, &b.doc);
    if (b.status == ONION_OK) {
        main_page(&b, c, o ? o->note : NULL);
        if (c->state == AKN_COMP_READY) {
            profile_page(&b, c);
            friend_pages(&b, c);
            message_pages(&b, c);
            party_pages(&b, c);
            inbox_page(&b, c);
            voice_page(&b, c);
            if (c->voice.service == AKN_VOICE_SVC_READY) route_page(&b, c);
        }
        diag_page(&b, c);
        tombstone_pages(&b, o);
    }
    if (b.status == ONION_OK) b.status = onion_ui_document_validate(b.doc);
    if (b.status != ONION_OK) {
        onion_ui_document_destroy(b.doc);
        return b.status;
    }
    *out = b.doc;
    return ONION_OK;
}

onion_status akn_companion_document(const akn_companion *c, const char *note, onion_ui_document **out) {
    akn_doc_options o;
    memset(&o, 0, sizeof(o));
    o.note = note;
    return akn_companion_document_ex(c, &o, out);
}
