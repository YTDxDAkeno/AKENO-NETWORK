#include "companion.h"

#include <stdio.h>
#include <string.h>

static void copy(char *dst, size_t len, const char *src) { snprintf(dst, len, "%s", src ? src : ""); }

const char *akn_companion_presence_label(const char *p) {
    if (!p || !p[0]) return "Unknown";
    if (strcmp(p, "away") == 0) return "Away";
    if (strcmp(p, "dnd") == 0) return "Busy";
    if (strcmp(p, "invisible") == 0) return "Invisible";
    return "Online";
}

static const char *state_line(akn_companion_state s) {
    switch (s) {
    case AKN_COMP_NO_DAEMON: return "The AKENO daemon is not running. Start it with your ELF loader.";
    case AKN_COMP_NO_INTERFACE: return "This AKENO daemon has no native interface. Send akeno-network-ps5-native-ipc.elf.";
    case AKN_COMP_NO_KEY: return "The daemon's native key is missing or not private. Restart the daemon.";
    case AKN_COMP_NOT_PAIRED: return "This console is not paired with AKENO. Pair it on the AKENO page.";
    case AKN_COMP_READY: break;
    }
    return "Live from the AKENO daemon";
}

void akn_companion_build(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                         const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now) {
    memset(c, 0, sizeof(*c));
    c->state = state;
    copy(c->status_line, sizeof(c->status_line), state_line(state));
    copy(c->presence, sizeof(c->presence), presence);
    if (daemon) {
        copy(c->online_id, sizeof(c->online_id), daemon->online_id);
        c->writes = daemon->ipc >= 2;
        if (daemon->console_game.present) copy(c->console_game, sizeof(c->console_game), akn_game_label(&daemon->console_game));
    }
    if (friends) {
        c->friends_total = friends->count;
        c->friends_online = friends->online;
        c->friends_playing = friends->playing;
        /* the list is sorted: online and playing first */
        for (int i = 0; i < friends->count && c->online_shown < AKN_COMPANION_FRIENDS; i++) {
            const akn_friend *f = &friends->items[i];
            if (f->presence.status == AKN_PRES_OFFLINE) break;
            akn_companion_row *r = &c->online[c->online_shown++];
            copy(r->label, sizeof(r->label), f->display_name);
            akn_presence_line(&f->presence, now, r->value, sizeof(r->value));
        }
        c->friend_requests = friends->incoming_requests;
    }
    if (convs) {
        c->unread_messages = convs->unread_total;
        for (int i = 0; i < convs->count; i++) {
            const akn_conversation *v = &convs->items[i];
            if (!v->unread) continue;
            c->unread_conversations++;
            if (c->convs_shown >= AKN_COMPANION_CONVS) continue;
            akn_companion_row *r = &c->convs[c->convs_shown++];
            snprintf(r->label, sizeof(r->label), "%.48s (%d)", v->title, v->unread);
            copy(r->value, sizeof(r->value), v->preview);
        }
    }
    if (party && party->present) {
        c->in_party = 1;
        copy(c->party_name, sizeof(c->party_name), party->name[0] ? party->name : "Party");
        c->party_members = party->member_count;
        c->party_max = party->max_members;
        snprintf(c->party_voice, sizeof(c->party_voice), "%s",
                 party->voice_available ? "Voice runs on the website and the apps; not on the PS5 yet" : "Voice not available");
    }
    if (inbox) {
        for (int i = 0; i < inbox->invitation_count; i++) {
            const akn_invitation *v = &inbox->invitations[i];
            if (v->kind != AKN_INV_PARTY) {
                c->other_invites++;
                continue;
            }
            if (!c->party_invites++) {
                copy(c->invite_id, sizeof(c->invite_id), v->id);
                snprintf(c->invite_line, sizeof(c->invite_line), "%s invited you to \"%s\"", v->inviter_name, v->target_name);
            }
        }
        c->friend_requests = inbox->incoming_count;
        c->notifications_unread = inbox->notifications_unread;
    }
}

int akn_companion_summary(const akn_companion *c, akn_companion_row *out, int max) {
    int n = 0;
#define ROW(l, ...)                                                       \
    do {                                                                  \
        if (n < max) {                                                    \
            snprintf(out[n].label, sizeof(out[n].label), "%s", l);        \
            snprintf(out[n].value, sizeof(out[n].value), __VA_ARGS__);    \
        }                                                                 \
        n++;                                                              \
    } while (0)
    if (c->state != AKN_COMP_READY) {
        ROW("AKENO", "%.150s", c->status_line);
        return n < max ? n : max;
    }
    if (c->friends_playing) ROW("Friends", "%d online, %d playing", c->friends_online, c->friends_playing);
    else ROW("Friends", "%d online", c->friends_online);
    if (c->unread_messages)
        ROW("Messages", "%d unread in %d conversation%s", c->unread_messages, c->unread_conversations, c->unread_conversations == 1 ? "" : "s");
    else ROW("Messages", "No unread messages");
    if (c->in_party) ROW("Party", "%s, %d member%s", c->party_name, c->party_members, c->party_members == 1 ? "" : "s");
    else ROW("Party", "Not in a party");
    if (c->party_invites || c->other_invites || c->friend_requests) {
        char t[160] = "";
        size_t o = 0;
        if (c->party_invites)
            o += (size_t)snprintf(t + o, sizeof(t) - o, "%d party invitation%s", c->party_invites, c->party_invites == 1 ? "" : "s");
        if (c->other_invites && o < sizeof(t))
            o += (size_t)snprintf(t + o, sizeof(t) - o, "%s%d other", o ? ", " : "", c->other_invites);
        if (c->friend_requests && o < sizeof(t))
            snprintf(t + o, sizeof(t) - o, "%s%d friend request%s", o ? ", " : "", c->friend_requests, c->friend_requests == 1 ? "" : "s");
        ROW("Invitations", "%s", t);
    } else {
        ROW("Invitations", "None waiting");
    }
    if (c->notifications_unread) ROW("Notifications", "%d new", c->notifications_unread);
    else ROW("Notifications", "Nothing new");
    ROW("Status", "%s%s%s", akn_companion_presence_label(c->presence), c->online_id[0] ? " as @" : "", c->online_id);
    if (c->console_game[0]) ROW("This console", "%s", c->console_game);
#undef ROW
    return n < max ? n : max;
}

void akn_companion_text(const akn_companion *c, char *out, size_t len) {
    size_t o = 0;
    akn_companion_row rows[12];
    int n = akn_companion_summary(c, rows, 12);
#define ADD(...)                                                   \
    do {                                                           \
        if (o < len) o += (size_t)snprintf(out + o, len - o, __VA_ARGS__); \
    } while (0)
    ADD("AKENO QUICK MENU\n");
    for (int i = 0; i < n; i++) ADD("%-16s%s\n", rows[i].label, rows[i].value);
    if (c->online_shown) ADD("Online now:\n");
    for (int i = 0; i < c->online_shown; i++) ADD("  %s - %s\n", c->online[i].label, c->online[i].value);
    if (c->convs_shown) ADD("Unread:\n");
    for (int i = 0; i < c->convs_shown; i++) ADD("  %s: %s\n", c->convs[i].label, c->convs[i].value);
    if (c->invite_line[0]) ADD("Invitation: %s\n", c->invite_line);
#undef ADD
    if (len) out[o < len ? o : len - 1] = '\0';
}
