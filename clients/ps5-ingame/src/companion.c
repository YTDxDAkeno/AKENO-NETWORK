#include "companion.h"

#include <stdio.h>
#include <string.h>

void akn_utf8_whole(char *s) {
    size_t n = strlen(s), i = n;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--; /* back to the last lead byte */
    if (i == 0) return;
    unsigned char lead = (unsigned char)s[i - 1];
    size_t need = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
    if (n - (i - 1) < need) s[i - 1] = '\0';
}

const char *akn_utf8_cut(char *buf, size_t cap, const char *src, size_t max) {
    if (!cap) return buf;
    if (max >= cap) max = cap - 1;
    snprintf(buf, max + 1, "%s", src ? src : "");
    akn_utf8_whole(buf);
    return buf;
}

static void copy(char *dst, size_t len, const char *src) {
    snprintf(dst, len, "%s", src ? src : "");
    akn_utf8_whole(dst);
}

const char *akn_companion_presence_label(const char *p) {
    if (!p || !p[0]) return "Unknown";
    if (strcmp(p, "away") == 0) return "Away";
    if (strcmp(p, "dnd") == 0) return "Busy";
    if (strcmp(p, "invisible") == 0) return "Invisible";
    return "Online";
}

int akn_companion_id_ok(const char *id) {
    size_t n = id ? strlen(id) : 0;
    if (n == 0 || n > 39) return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-')) return 0;
    }
    return 1;
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

static int party_has(const akn_party *p, const char *user_id) {
    if (!p || !p->present || !user_id[0]) return 0;
    if (strcmp(p->leader_id, user_id) == 0) return 1;
    for (int i = 0; i < p->members_shown && i < AKN_PARTY_MEMBERS; i++)
        if (strcmp(p->members[i].id, user_id) == 0) return 1;
    return 0;
}

static const char *party_leader_name(const akn_party *p) {
    for (int i = 0; i < p->members_shown && i < AKN_PARTY_MEMBERS; i++)
        if (p->members[i].leader || strcmp(p->members[i].id, p->leader_id) == 0) return p->members[i].display_name;
    return "";
}

static void build_conversations(akn_companion *c, const akn_conv_list *convs, const akn_companion_extra *x) {
    if (!convs) return;
    c->conversations_total = convs->count;
    for (int i = 0; i < convs->count && c->conversations_shown < AKN_UI_CONVS; i++) {
        const akn_conversation *v = &convs->items[i];
        if (!akn_companion_id_ok(v->id)) continue;
        akn_ui_conv *u = &c->conversations[c->conversations_shown++];
        copy(u->id, sizeof(u->id), v->id);
        u->kind = v->kind;
        copy(u->title, sizeof(u->title), v->title[0] ? v->title : v->kind == AKN_CONV_PARTY ? "Party chat" : "Conversation");
        akn_chat_preview(v->preview, u->preview, sizeof(u->preview));
        u->preview_mine = v->preview_mine;
        u->unread = v->unread;
        u->can_send = v->can_send;
        u->last_message_id = v->last_message_id;
        for (int t = 0; x && x->threads && t < x->thread_count; t++)
            if (strcmp(x->threads[t].conv_id, v->id) == 0) u->thread = x->threads[t];
    }
}

static void build_friends(akn_companion *c, const akn_friend_list *friends, const akn_party *party, const akn_companion_extra *x,
                          long long now) {
    if (!friends) return;
    const akn_party_list *joinable = x ? x->joinable : NULL;
    for (int i = 0; i < friends->count && c->friends_shown < AKN_UI_FRIENDS; i++) {
        const akn_friend *f = &friends->items[i];
        if (!akn_companion_id_ok(f->id) || f->deleted) continue;
        akn_ui_friend *u = &c->friends[c->friends_shown++];
        copy(u->id, sizeof(u->id), f->id);
        copy(u->online_id, sizeof(u->online_id), f->online_id);
        copy(u->name, sizeof(u->name), f->display_name[0] ? f->display_name : f->online_id);
        akn_presence_line(&f->presence, now, u->line, sizeof(u->line));
        copy(u->status_message, sizeof(u->status_message), f->status_message);
        u->online = f->presence.status != AKN_PRES_OFFLINE;
        u->in_my_party = party_has(party, f->id);
        for (int j = 0; joinable && j < joinable->count; j++) {
            const akn_party *p = &joinable->items[j];
            if (!akn_companion_id_ok(p->id) || !party_has(p, f->id)) continue;
            copy(u->party_id, sizeof(u->party_id), p->id);
            copy(u->party_name, sizeof(u->party_name), p->name[0] ? p->name : "Party");
            u->party_members = p->member_count;
            u->party_max = p->max_members;
            break;
        }
    }
}

/* Direct conversations name the other user in target_id; the friend rows link to them. */
static void link_friend_conversations(akn_companion *c, const akn_conv_list *convs) {
    if (!convs) return;
    for (int i = 0; i < c->friends_shown; i++) {
        akn_ui_friend *u = &c->friends[i];
        for (int k = 0; k < convs->count; k++) {
            const akn_conversation *v = &convs->items[k];
            if (v->kind != AKN_CONV_DIRECT || strcmp(v->target_id, u->id) != 0) continue;
            if (!akn_companion_conv(c, v->id)) break; /* not on the Messages page */
            copy(u->conv_id, sizeof(u->conv_id), v->id);
            u->conv_unread = v->unread;
            break;
        }
    }
}

static void build_party(akn_companion *c, const akn_party *party) {
    if (!party || !party->present) return;
    c->in_party = 1;
    copy(c->party_name, sizeof(c->party_name), party->name[0] ? party->name : "Party");
    if (akn_companion_id_ok(party->id)) copy(c->party_id, sizeof(c->party_id), party->id);
    if (akn_companion_id_ok(party->conversation_id)) copy(c->party_conv_id, sizeof(c->party_conv_id), party->conversation_id);
    c->party_members = party->member_count;
    c->party_max = party->max_members;
    snprintf(c->party_voice, sizeof(c->party_voice), "%s",
             party->voice_available ? "Party voice: join it in AKENO Native or on the website; voice during games is not available yet"
                                    : "Party voice is not available for this party");
    for (int i = 0; i < party->members_shown && i < AKN_PARTY_MEMBERS && c->members_shown < AKN_UI_MEMBERS; i++) {
        const akn_party_member *m = &party->members[i];
        akn_ui_member *u = &c->members[c->members_shown++];
        copy(u->id, sizeof(u->id), m->id);
        copy(u->name, sizeof(u->name), m->display_name[0] ? m->display_name : m->online_id);
        u->leader = m->leader || strcmp(m->id, party->leader_id) == 0;
        u->me = c->self_id[0] && strcmp(m->id, c->self_id) == 0;
        u->voice_connected = m->voice_connected;
        u->voice_muted = m->voice_muted;
    }
}

static void build_inbox(akn_companion *c, const akn_inbox *inbox) {
    if (!inbox) return;
    for (int i = 0; i < inbox->invitation_count; i++) {
        const akn_invitation *v = &inbox->invitations[i];
        if (v->kind != AKN_INV_PARTY) c->other_invites++;
        else if (!c->party_invites++) {
            copy(c->invite_id, sizeof(c->invite_id), v->id);
            snprintf(c->invite_line, sizeof(c->invite_line), "%s invited you to \"%s\"", v->inviter_name, v->target_name);
            akn_utf8_whole(c->invite_line);
        }
        if (c->invites_shown >= AKN_UI_INVITES || !akn_companion_id_ok(v->id)) continue;
        akn_ui_invite *u = &c->invites[c->invites_shown++];
        copy(u->id, sizeof(u->id), v->id);
        u->kind = v->kind;
        char who[64], what[72];
        snprintf(u->line, sizeof(u->line), "%s \xC2\xB7 %s \"%s\"", akn_utf8_cut(who, sizeof(who), v->inviter_name[0] ? v->inviter_name : "Someone", 60),
                 akn_invitation_kind_label(v->kind), akn_utf8_cut(what, sizeof(what), v->target_name, 70));
    }
    c->friend_requests = inbox->incoming_count;
    for (int i = 0; i < inbox->incoming_count && i < AKN_REQUESTS_MAX && c->requests_shown < AKN_UI_REQUESTS; i++) {
        const akn_friend_request *r = &inbox->incoming[i];
        if (!akn_companion_id_ok(r->id)) continue;
        akn_ui_request *u = &c->requests[c->requests_shown++];
        copy(u->id, sizeof(u->id), r->id);
        copy(u->name, sizeof(u->name), r->display_name[0] ? r->display_name : r->online_id);
        copy(u->online_id, sizeof(u->online_id), r->online_id);
    }
    c->notifications_unread = inbox->notifications_unread;
    for (int i = 0; i < inbox->notification_count && i < AKN_NOTIFICATIONS_MAX && c->notifications_shown < AKN_UI_NOTIFICATIONS; i++)
        copy(c->notifications[c->notifications_shown++], sizeof(c->notifications[0]), inbox->notifications[i].text);
}

static void build_joinable(akn_companion *c, const akn_party_list *joinable) {
    for (int i = 0; joinable && i < joinable->count && c->joinable_shown < AKN_UI_JOINABLE; i++) {
        const akn_party *p = &joinable->items[i];
        if (!akn_companion_id_ok(p->id) || (c->party_id[0] && strcmp(p->id, c->party_id) == 0)) continue;
        akn_ui_joinable *u = &c->joinable[c->joinable_shown++];
        copy(u->id, sizeof(u->id), p->id);
        char leader[64], name[64];
        akn_utf8_cut(leader, sizeof(leader), party_leader_name(p), 60);
        akn_utf8_cut(name, sizeof(name), p->name[0] ? p->name : "Party", 60);
        if (leader[0]) snprintf(u->line, sizeof(u->line), "%s's party \"%s\" \xC2\xB7 %d/%d", leader, name, p->member_count, p->max_members);
        else snprintf(u->line, sizeof(u->line), "Party \"%s\" \xC2\xB7 %d/%d", name, p->member_count, p->max_members);
    }
}

void akn_companion_build_ex(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                            const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now,
                            const akn_companion_extra *x) {
    memset(c, 0, sizeof(*c));
    c->state = state;
    copy(c->status_line, sizeof(c->status_line), state_line(state));
    copy(c->presence, sizeof(c->presence), presence);
    if (x && x->diag) c->diag = *x->diag;
    if (daemon) {
        copy(c->online_id, sizeof(c->online_id), daemon->online_id);
        c->writes = daemon->ipc >= 2;
        if (daemon->console_game.present) copy(c->console_game, sizeof(c->console_game), akn_game_label(&daemon->console_game));
        c->diag.daemon_ipc = daemon->ipc;
        copy(c->diag.daemon_version, sizeof(c->diag.daemon_version), daemon->version);
        copy(c->diag.daemon_build, sizeof(c->diag.daemon_build), daemon->build);
        copy(c->diag.realtime, sizeof(c->diag.realtime), daemon->realtime);
        copy(c->diag.realtime_detail, sizeof(c->diag.realtime_detail), daemon->realtime_detail);
    }
    if (x && x->me && akn_companion_id_ok(x->me->id)) {
        copy(c->self_id, sizeof(c->self_id), x->me->id);
        copy(c->display_name, sizeof(c->display_name), x->me->display_name);
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
            char title[52];
            snprintf(r->label, sizeof(r->label), "%s (%d)", akn_utf8_cut(title, sizeof(title), v->title, 48), v->unread);
            copy(r->value, sizeof(r->value), v->preview);
        }
    }
    build_party(c, party);
    build_inbox(c, inbox);
    if (state != AKN_COMP_READY) return;
    build_conversations(c, convs, x);
    build_friends(c, friends, party, x, now);
    link_friend_conversations(c, convs);
    build_joinable(c, x ? x->joinable : NULL);
    if (c->in_party)
        for (int i = 0; i < c->friends_shown && c->invite_targets_shown < AKN_UI_INVITE_TARGETS; i++) {
            const akn_ui_friend *f = &c->friends[i];
            if (!f->online || f->in_my_party) continue;
            akn_ui_person *t = &c->invite_targets[c->invite_targets_shown++];
            copy(t->id, sizeof(t->id), f->id);
            copy(t->name, sizeof(t->name), f->name);
        }
}

void akn_companion_build(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                         const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now) {
    akn_companion_build_ex(c, state, daemon, friends, convs, party, inbox, presence, now, NULL);
}

const akn_ui_friend *akn_companion_friend(const akn_companion *c, const char *user_id) {
    for (int i = 0; user_id && i < c->friends_shown; i++)
        if (strcmp(c->friends[i].id, user_id) == 0) return &c->friends[i];
    return NULL;
}

const akn_ui_conv *akn_companion_conv(const akn_companion *c, const char *conv_id) {
    for (int i = 0; conv_id && i < c->conversations_shown; i++)
        if (strcmp(c->conversations[i].id, conv_id) == 0) return &c->conversations[i];
    return NULL;
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
    /* the pages' details */
    for (int i = 0; i < c->conversations_shown; i++) {
        const akn_ui_conv *v = &c->conversations[i];
        ADD("Conversation %s [%s]%s:\n", v->title, v->id, v->can_send ? "" : " (read only)");
        if (v->thread.state < 0) ADD("  (not loaded: %s)\n", v->thread.error);
        for (int m = 0; m < v->thread.count; m++) ADD("  %s: %s\n", v->thread.items[m].sender, v->thread.items[m].body);
    }
    for (int i = 0; i < c->friends_shown; i++) {
        const akn_ui_friend *f = &c->friends[i];
        ADD("Friend %s [%s] - %s%s%s%s\n", f->name, f->id, f->line, f->in_my_party ? " (in your party)" : "", f->party_id[0] ? " (party to join)" : "",
            f->conv_id[0] ? " (conversation)" : "");
    }
    for (int i = 0; i < c->members_shown; i++)
        ADD("Member %s%s%s%s\n", c->members[i].name, c->members[i].leader ? " (leader)" : "", c->members[i].me ? " (you)" : "",
            c->members[i].voice_connected ? (c->members[i].voice_muted == 1 ? " (voice, muted)" : " (voice)") : "");
    for (int i = 0; i < c->joinable_shown; i++) ADD("Joinable: %s [%s]\n", c->joinable[i].line, c->joinable[i].id);
    for (int i = 0; i < c->requests_shown; i++) ADD("Friend request: %s [%s]\n", c->requests[i].name, c->requests[i].id);
    for (int i = 0; i < c->invites_shown; i++) ADD("Invited: %s [%s]\n", c->invites[i].line, c->invites[i].id);
#undef ADD
    if (len) out[o < len ? o : len - 1] = '\0';
}
