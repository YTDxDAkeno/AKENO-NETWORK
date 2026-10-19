/*
 * The in-game companion: its model, its onionHEN pages and its actions.
 *
 * The pages are checked on the encoded document, i.e. exactly what onionHEN receives, against the
 * rules of the pinned onionHEN v0.9.0 host (aydencharles/onionHEN@aaa4264,
 * libonion_plugin_ui/source/plugin_ui.cpp validate_document, shellui/src/dynamic_ui_xml.cpp
 * render_node with the "legacy-settings-2.x-13.x" profile that firmware 12.20 gets) and AKENO's own
 * page rules (docs/IN_GAME_ACCESS.md). The actions run against a fake daemon on 127.0.0.1 that
 * records each request. Nothing here is evidence about a PS5.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "actions.h"
#include "companion.h"
#include "fetch.h"
#include "ui_doc.h"
#include "voice_link.h"
#include <onion/ui_protocol.h>

static int g_checks, g_failures;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            g_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static int contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* ---- the encoded document, decoded ------------------------------------------------------ */

typedef struct {
    unsigned kind, flags, value_type, binding, min_length, max_length;
    char id[80], parent[80], title[160], description[300], target[80], key[80], value[300];
} wnode;

typedef struct {
    wnode nodes[300];
    int count;
    size_t size;
    char root[80];
} wdoc;

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned rd32(const unsigned char *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* Copies one text of the wire record; returns -1 when it does not fit what the host accepts. */
static int text(const unsigned char **p, const unsigned char *end, unsigned n, char *out, size_t cap) {
    if (n >= cap || (size_t)(end - *p) < n) return -1;
    memcpy(out, *p, n);
    out[n] = '\0';
    *p += n;
    return 0;
}

/* With AKN_DUMP_DOCS=<dir>, every checked document is also written there as the bytes the plugin
 * sends to onionHEN (doc-NNN.bin), for tools/onionhen/check-direct-entry.sh. */
static void dump_encoded(const unsigned char *buf, size_t size) {
    static int n;
    const char *dir = getenv("AKN_DUMP_DOCS");
    if (!dir || !*dir) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/doc-%03d.bin", dir, n++);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, size, f);
    fclose(f);
}

static int decode(const onion_ui_document *doc, wdoc *w) {
    memset(w, 0, sizeof(*w));
    size_t size = 0, used = 0;
    if (onion_ui_document_encoded_size(doc, &size) != ONION_OK) return -1;
    unsigned char *buf = malloc(size);
    if (!buf || onion_ui_document_encode(doc, buf, size, &used) != ONION_OK || used != size || size < ONION_UI_DOCUMENT_HEADER_SIZE ||
        rd32(buf) != ONION_UI_DOCUMENT_MAGIC) {
        free(buf);
        return -1;
    }
    w->size = size;
    dump_encoded(buf, size);
    const unsigned char *p = buf + ONION_UI_DOCUMENT_HEADER_SIZE, *end = buf + size;
    unsigned count = rd32(buf + 12);
    char skip[300];
    int rc = 0;
    for (int i = 0; i < 4 && rc == 0; i++) rc = text(&p, end, rd16(buf + 24 + i * 2), skip, sizeof(skip));
    if (rc == 0) rc = text(&p, end, rd16(buf + 32), w->root, sizeof(w->root));
    for (unsigned i = 0; i < count && rc == 0; i++) {
        if (i >= sizeof(w->nodes) / sizeof(w->nodes[0]) || (size_t)(end - p) < ONION_UI_NODE_HEADER_SIZE) {
            rc = -1;
            break;
        }
        wnode *n = &w->nodes[w->count++];
        const unsigned char *h = p;
        n->kind = rd16(h + 4);
        n->flags = rd32(h + 8);
        n->value_type = rd32(h + 12);
        n->binding = rd32(h + 16);
        n->min_length = rd32(h + 36);
        n->max_length = rd32(h + 40);
        p += ONION_UI_NODE_HEADER_SIZE;
        char *fields[] = { n->id, n->parent, n->title, n->description, n->target, n->key, n->value };
        size_t caps[] = { sizeof(n->id), sizeof(n->parent), sizeof(n->title), sizeof(n->description), sizeof(n->target), sizeof(n->key),
                          sizeof(n->value) };
        for (int f = 0; f < 7 && rc == 0; f++) rc = text(&p, end, rd16(h + 44 + f * 2), fields[f], caps[f]);
        if (rc == 0 && (size_t)(p - h) != rd32(h)) rc = -1;
    }
    if (p != end) rc = -1;
    free(buf);
    return rc;
}

static const wnode *find(const wdoc *w, const char *id) {
    for (int i = 0; i < w->count; i++)
        if (strcmp(w->nodes[i].id, id) == 0) return &w->nodes[i];
    return NULL;
}

static int title_on(const wdoc *w, const char *page_id, const char *title_part) {
    for (int i = 0; i < w->count; i++)
        if (strcmp(w->nodes[i].parent, page_id) == 0 && contains(w->nodes[i].title, title_part)) return 1;
    return 0;
}

static int valid_id(const char *s, size_t max) {
    size_t n = strlen(s);
    if (n == 0 || n >= max) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static int utf8_ok(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
        if (!n) return 0;
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        p += n;
    }
    return 1;
}

static int interactive(unsigned kind) {
    return kind == ONION_UI_NODE_ACTION || kind == ONION_UI_NODE_TOGGLE || kind == ONION_UI_NODE_LIST || kind == ONION_UI_NODE_INPUT;
}

/* onionHEN v0.9.0's validate_document and render_node (legacy profile), then AKENO's rules.
 * unreachable_ok: page IDs that need no menu (tombstones). Returns NULL when everything holds. */
static const char *host_check(const wdoc *w, char (*unreachable_ok)[AKN_UI_PAGE_ID_MAX], int unreachable_n) {
    static char why[200];
    if (w->count < 1 || w->count > 256) return "node count outside 1-256";
    if (w->size > ONION_UI_DOCUMENT_MAX_ENCODED_SIZE) return "encoded document over 256 KiB";
    const wnode *root = find(w, w->root);
    if (!root || root->kind != ONION_UI_NODE_PAGE) return "root page missing";
    for (int i = 0; i < w->count; i++) {
        const wnode *n = &w->nodes[i];
#define FAIL(msg)                                                         \
    do {                                                                  \
        snprintf(why, sizeof(why), "%s: %.60s (%.60s)", msg, n->id, n->title); \
        return why;                                                       \
    } while (0)
        if (n->kind < ONION_UI_NODE_PAGE || n->kind > ONION_UI_NODE_INPUT) FAIL("unknown kind");
        if (!valid_id(n->id, 64)) FAIL("invalid node ID");
        for (int k = 0; k < i; k++)
            if (strcmp(w->nodes[k].id, n->id) == 0) FAIL("duplicate node ID");
        if (!n->title[0] || strlen(n->title) >= 128) FAIL("title empty or too long");
        if (strlen(n->description) >= 256 || strlen(n->value) >= 256) FAIL("description or value too long");
        if (!utf8_ok(n->title) || !utf8_ok(n->description) || !utf8_ok(n->value)) FAIL("text is not UTF-8");
        if (n->kind == ONION_UI_NODE_PAGE) {
            if (n->parent[0]) FAIL("page with a parent");
        } else {
            const wnode *parent = find(w, n->parent);
            if (!parent) FAIL("parent missing");
            int accepts = n->kind == ONION_UI_NODE_LIST_ITEM ? parent->kind == ONION_UI_NODE_LIST
                                                             : parent->kind == ONION_UI_NODE_PAGE || parent->kind == ONION_UI_NODE_GROUP;
            if (!accepts) FAIL("parent does not accept the node");
        }
        if (n->kind == ONION_UI_NODE_MENU) {
            const wnode *t = find(w, n->target);
            if (!t || t->kind != ONION_UI_NODE_PAGE) FAIL("menu target is not a page");
        } else if (n->target[0]) {
            FAIL("target on a node that is not a menu");
        }
        if (n->kind == ONION_UI_NODE_ACTION && (n->binding != ONION_UI_BINDING_EVENT || !n->key[0] || n->value_type != ONION_UI_VALUE_NONE))
            FAIL("action without an event binding");
        if (n->binding != ONION_UI_BINDING_NONE && !valid_id(n->key, 64)) FAIL("bad binding key");
        if ((n->kind == ONION_UI_NODE_PAGE || n->kind == ONION_UI_NODE_MENU || n->kind == ONION_UI_NODE_GROUP || n->kind == ONION_UI_NODE_LABEL) &&
            (n->value_type != ONION_UI_VALUE_NONE || n->binding != ONION_UI_BINDING_NONE))
            FAIL("value or binding on a passive node");
        if ((n->kind == ONION_UI_NODE_LIST || n->kind == ONION_UI_NODE_INPUT || n->kind == ONION_UI_NODE_LIST_ITEM) &&
            n->value_type != ONION_UI_VALUE_INT && n->value_type != ONION_UI_VALUE_STRING)
            FAIL("list or input without a value type");
        if (n->max_length && n->min_length > n->max_length) FAIL("min_length over max_length");
        if (n->kind == ONION_UI_NODE_INPUT &&
            (strlen(n->value) < n->min_length || (n->max_length && strlen(n->value) > n->max_length)))
            FAIL("input's initial value outside its lengths");
        if (n->kind == ONION_UI_NODE_LIST) {
            int items = 0, selected = 0;
            for (int k = 0; k < w->count; k++)
                if (w->nodes[k].kind == ONION_UI_NODE_LIST_ITEM && strcmp(w->nodes[k].parent, n->id) == 0) {
                    items++;
                    selected |= w->nodes[k].value_type == n->value_type && strcmp(w->nodes[k].value, n->value) == 0;
                }
            if (!items || !selected) FAIL("list without items or without its selected item");
        }
        int depth = 0;
        for (const wnode *c = n; c && c->parent[0]; c = find(w, c->parent))
            if (++depth > 8) FAIL("deeper than 8");
        /* the renderer: DISABLED and SECRET make a page unrenderable */
        if (n->flags & (ONION_UI_NODE_FLAG_DISABLED | ONION_UI_NODE_FLAG_SECRET)) FAIL("disabled or secret node");
        /* AKENO: groups break onionHEN's page stack; every control routes to its verb */
        if (n->kind == ONION_UI_NODE_GROUP) FAIL("group node");
        if (interactive(n->kind)) {
            const char *verb = akn_companion_action_verb(n->id, NULL);
            if (!verb || strcmp(verb, n->key) != 0) FAIL("control does not route to its binding key");
        }
        if (n->kind == ONION_UI_NODE_INPUT && (n->max_length == 0 || n->max_length > 250 || n->value[0])) FAIL("text field limits");
        if (n->kind == ONION_UI_NODE_PAGE && strcmp(n->id, w->root) != 0) {
            int reachable = 0;
            for (int k = 0; k < w->count; k++)
                reachable |= w->nodes[k].kind == ONION_UI_NODE_MENU && strcmp(w->nodes[k].target, n->id) == 0;
            for (int k = 0; k < unreachable_n; k++) reachable |= strcmp(unreachable_ok[k], n->id) == 0;
            if (!reachable) FAIL("page without a menu to it");
        }
#undef FAIL
    }
    return NULL;
}

static const char *check_doc(const akn_companion *m, const akn_doc_options *o, wdoc *w) {
    onion_ui_document *doc = NULL;
    onion_status st = akn_companion_document_ex(m, o, &doc);
    if (st != ONION_OK) return onion_status_string(st);
    int rc = decode(doc, w);
    onion_ui_document_destroy(doc);
    if (rc != 0) return "the encoded document does not decode within the host's limits";
    return host_check(w, o ? o->tombstones : NULL, o ? o->tombstone_count : 0);
}

/* ---- fixtures --------------------------------------------------------------------------- */

static void fixtures(akn_daemon_info *d, akn_friend_list *f, akn_conv_list *c, akn_party *p, akn_inbox *in) {
    memset(d, 0, sizeof(*d));
    d->ipc = 3;
    d->paired = 1;
    snprintf(d->online_id, sizeof(d->online_id), "akeno_tester");
    snprintf(d->realtime, sizeof(d->realtime), "connected");
    d->console_game.present = 1;
    snprintf(d->console_game.title_id, sizeof(d->console_game.title_id), "PPSA01234");
    snprintf(d->console_game.name, sizeof(d->console_game.name), "Nioh 3");
    memset(f, 0, sizeof(*f));
    const char *names[] = { "Bob", "Carol", "Dan", "Eve" };
    const char *ids[] = { "u-bob", "u-carol", "u-dan", "u-eve" };
    akn_presence st[] = { AKN_PRES_ONLINE, AKN_PRES_AWAY, AKN_PRES_OFFLINE, AKN_PRES_OFFLINE };
    for (int i = 0; i < 4; i++) {
        snprintf(f->items[i].display_name, sizeof(f->items[i].display_name), "%s", names[i]);
        snprintf(f->items[i].id, sizeof(f->items[i].id), "%s", ids[i]);
        snprintf(f->items[i].online_id, sizeof(f->items[i].online_id), "it_%s", names[i]);
        f->items[i].presence.status = st[i];
    }
    f->items[0].presence.game.present = 1;
    snprintf(f->items[0].presence.game.name, sizeof(f->items[0].presence.game.name), "Astro Bot");
    snprintf(f->items[1].status_message, sizeof(f->items[1].status_message), "brb");
    f->count = 4;
    f->online = 2;
    f->playing = 1;
    memset(c, 0, sizeof(*c));
    c->count = 2;
    snprintf(c->items[0].id, sizeof(c->items[0].id), "conv-bob");
    snprintf(c->items[0].target_id, sizeof(c->items[0].target_id), "u-bob");
    snprintf(c->items[0].title, sizeof(c->items[0].title), "Bob");
    snprintf(c->items[0].preview, sizeof(c->items[0].preview), "ready?");
    c->items[0].unread = 2;
    c->items[0].can_send = 1;
    c->items[0].last_message_id = 41;
    c->items[1].kind = AKN_CONV_GROUP;
    snprintf(c->items[1].id, sizeof(c->items[1].id), "conv-crew");
    snprintf(c->items[1].title, sizeof(c->items[1].title), "Friday crew");
    c->items[1].can_send = 1;
    c->unread_total = 2;
    memset(p, 0, sizeof(*p));
    p->present = 1;
    snprintf(p->id, sizeof(p->id), "party-1");
    snprintf(p->name, sizeof(p->name), "Squad");
    snprintf(p->leader_id, sizeof(p->leader_id), "self-1");
    p->member_count = 3;
    p->max_members = 8;
    p->members_shown = 2;
    snprintf(p->members[0].id, sizeof(p->members[0].id), "self-1");
    snprintf(p->members[0].display_name, sizeof(p->members[0].display_name), "Tester");
    p->members[0].leader = 1;
    snprintf(p->members[1].id, sizeof(p->members[1].id), "u-carol");
    snprintf(p->members[1].display_name, sizeof(p->members[1].display_name), "Carol");
    p->members[1].voice_connected = 1;
    p->members[1].voice_muted = 1;
    memset(in, 0, sizeof(*in));
    in->invitation_count = 2;
    in->invitations[0].kind = AKN_INV_GROUP;
    snprintf(in->invitations[0].id, sizeof(in->invitations[0].id), "inv-0");
    in->invitations[1].kind = AKN_INV_PARTY;
    snprintf(in->invitations[1].id, sizeof(in->invitations[1].id), "inv-1");
    snprintf(in->invitations[1].inviter_name, sizeof(in->invitations[1].inviter_name), "Dan");
    snprintf(in->invitations[1].target_name, sizeof(in->invitations[1].target_name), "Late night");
    in->incoming_count = 1;
    snprintf(in->incoming[0].id, sizeof(in->incoming[0].id), "req-1");
    snprintf(in->incoming[0].display_name, sizeof(in->incoming[0].display_name), "Frank");
    snprintf(in->incoming[0].online_id, sizeof(in->incoming[0].online_id), "it_frank");
    in->notifications_unread = 1;
    in->notification_count = 1;
    snprintf(in->notifications[0].text, sizeof(in->notifications[0].text), "Frank sent you a friend request.");
}

typedef struct {
    akn_daemon_info d;
    akn_friend_list *f;
    akn_conv_list *c;
    akn_party p;
    akn_inbox *in;
    akn_profile me;
    akn_party_list *joinable;
    akn_thread_cache threads[2];
    akn_ui_voice voice;
    int with_voice;
    akn_companion *m;
} world;

static void world_init(world *w) {
    memset(w, 0, sizeof(*w));
    w->f = calloc(1, sizeof(*w->f));
    w->c = calloc(1, sizeof(*w->c));
    w->in = calloc(1, sizeof(*w->in));
    w->joinable = calloc(1, sizeof(*w->joinable));
    w->m = calloc(1, sizeof(*w->m));
    fixtures(&w->d, w->f, w->c, &w->p, w->in);
    snprintf(w->me.id, sizeof(w->me.id), "self-1");
    snprintf(w->me.display_name, sizeof(w->me.display_name), "Tester");
    snprintf(w->threads[0].conv_id, sizeof(w->threads[0].conv_id), "conv-bob");
    w->threads[0].state = 1;
    w->threads[0].count = 2;
    snprintf(w->threads[0].items[0].sender, sizeof(w->threads[0].items[0].sender), "Bob");
    snprintf(w->threads[0].items[0].body, sizeof(w->threads[0].items[0].body), "ready?");
    snprintf(w->threads[0].items[1].sender, sizeof(w->threads[0].items[1].sender), "You");
    snprintf(w->threads[0].items[1].body, sizeof(w->threads[0].items[1].body), "on my way");
    w->threads[0].items[1].mine = 1;
    snprintf(w->threads[1].conv_id, sizeof(w->threads[1].conv_id), "conv-crew");
    w->threads[1].state = -1;
    snprintf(w->threads[1].error, sizeof(w->threads[1].error), "The AKENO daemon did not answer.");
    /* Eve's party is open to you */
    w->joinable->count = 1;
    akn_party *jp = &w->joinable->items[0];
    jp->present = 1;
    snprintf(jp->id, sizeof(jp->id), "party-eve");
    snprintf(jp->name, sizeof(jp->name), "Eve's lobby");
    snprintf(jp->leader_id, sizeof(jp->leader_id), "u-eve");
    jp->member_count = 1;
    jp->max_members = 4;
    jp->members_shown = 1;
    snprintf(jp->members[0].id, sizeof(jp->members[0].id), "u-eve");
    snprintf(jp->members[0].display_name, sizeof(jp->members[0].display_name), "Eve");
    jp->members[0].leader = 1;
}

static void world_build(world *w, akn_companion_state state, int with_party) {
    akn_companion_extra x;
    memset(&x, 0, sizeof(x));
    x.me = &w->me;
    x.joinable = w->joinable;
    x.threads = w->threads;
    x.thread_count = 2;
    x.voice = w->with_voice ? &w->voice : NULL;
    akn_companion_build_ex(w->m, state, &w->d, w->f, w->c, with_party ? &w->p : NULL, w->in, "away", 1000, &x);
}

static void world_free(world *w) {
    free(w->f);
    free(w->c);
    free(w->in);
    free(w->joinable);
    free(w->m);
}

/* ---- tests ------------------------------------------------------------------------------ */

static void test_summary(void) {
    world w;
    world_init(&w);
    akn_companion *m = w.m;
    akn_companion_build(m, AKN_COMP_READY, &w.d, w.f, w.c, &w.p, w.in, "away", 1000);
    CHECK(m->friends_online == 2 && m->online_shown == 2 && m->unread_messages == 2 && m->unread_conversations == 1);
    CHECK(m->in_party && m->party_members == 3 && m->party_invites == 1 && m->other_invites == 1 && m->friend_requests == 1);
    CHECK(strcmp(m->invite_id, "inv-1") == 0 && m->writes);
    char text[16384];
    akn_companion_text(m, text, sizeof(text));
    /* the lines the end-to-end test and the logs rely on */
    CHECK(contains(text, "AKENO QUICK MENU"));
    CHECK(contains(text, "Friends         2 online, 1 playing"));
    CHECK(contains(text, "Messages        2 unread in 1 conversation"));
    CHECK(contains(text, "Party           Squad, 3 members"));
    CHECK(contains(text, "Invitations     1 party invitation, 1 other, 1 friend request"));
    CHECK(contains(text, "Notifications   1 new"));
    CHECK(contains(text, "Status          Away as @akeno_tester"));
    CHECK(contains(text, "This console    Nioh 3"));
    CHECK(contains(text, "Bob - Playing Astro Bot"));
    CHECK(contains(text, "Carol - Away"));
    CHECK(!contains(text, "Dan -")); /* offline friends are not in the summary */
    CHECK(contains(text, "Bob (2): ready?"));
    CHECK(contains(text, "Dan invited you to \"Late night\""));
    /* nothing loaded: still a valid model */
    akn_companion_build(m, AKN_COMP_READY, &w.d, NULL, NULL, NULL, NULL, "", 1000);
    akn_companion_text(m, text, sizeof(text));
    CHECK(contains(text, "Friends         0 online") && contains(text, "No unread messages") && contains(text, "Not in a party"));
    CHECK(strcmp(akn_companion_presence_label("dnd"), "Busy") == 0 && strcmp(akn_companion_presence_label(""), "Unknown") == 0);
    world_free(&w);
}

static void test_pages(void) {
    world w;
    world_init(&w);
    world_build(&w, AKN_COMP_READY, 1);
    wdoc *d = calloc(1, sizeof(*d));
    const char *why = check_doc(w.m, NULL, d);
    CHECK(why == NULL);
    if (why) fprintf(stderr, "  %s\n", why);
    /* the main page: information in the titles */
    CHECK(title_on(d, "main", "@akeno_tester \xC2\xB7 Away \xC2\xB7 Nioh 3"));
    CHECK(title_on(d, "main", "Friends \xC2\xB7 2 online \xC2\xB7 1 playing"));
    CHECK(title_on(d, "main", "Messages \xC2\xB7 2 unread"));
    CHECK(title_on(d, "main", "Party \xC2\xB7 Squad \xC2\xB7 3/8"));
    CHECK(title_on(d, "main", "Requests & invitations \xC2\xB7 3 waiting"));
    CHECK(find(d, "refresh") && find(d, "diag"));
    /* friends: rows open their pages; Bob's page messages, invites and links his conversation */
    CHECK(title_on(d, "friends", "Bob \xC2\xB7 Playing Astro Bot"));
    CHECK(title_on(d, "friends", "Dan \xC2\xB7 Offline"));
    const wnode *n = find(d, "dm.u-bob");
    CHECK(n && n->kind == ONION_UI_NODE_INPUT && strcmp(n->parent, "f.u-bob") == 0 && n->max_length == 250);
    CHECK(find(d, "fpinv.u-bob"));
    CHECK(!find(d, "fpinv.u-carol") && title_on(d, "f.u-carol", "In your party"));
    CHECK(!find(d, "fpinv.u-dan")); /* offline */
    CHECK(title_on(d, "f.u-bob", "Conversation \xC2\xB7 2 unread"));
    CHECK(title_on(d, "f.u-carol", "\"brb\""));
    CHECK(!find(d, "fpjoin.u-eve")); /* you are in a party */
    /* messages: the latest messages, yours as "You", a reply field and mark read */
    CHECK(title_on(d, "messages", "Bob (2) \xC2\xB7 ready?"));
    CHECK(title_on(d, "c.conv-bob", "Bob: ready?") && title_on(d, "c.conv-bob", "You: on my way"));
    n = find(d, "reply.conv-bob");
    CHECK(n && n->kind == ONION_UI_NODE_INPUT && strcmp(n->parent, "c.conv-bob") == 0 && strcmp(n->key, "reply") == 0);
    CHECK(find(d, "read.conv-bob") && !find(d, "read.conv-crew"));
    CHECK(title_on(d, "c.conv-crew", "could not be loaded"));
    /* party: members with voice state, voice note, invite page, leave with a question */
    CHECK(title_on(d, "party", "Tester (you) \xC2\xB7 leader") && title_on(d, "party", "Carol \xC2\xB7 voice, muted"));
    CHECK(title_on(d, "party", "not available yet") || title_on(d, "party", "not available for this party"));
    CHECK(find(d, "pinv.u-bob") && !find(d, "pinv.u-carol"));
    n = find(d, "pleave.party-1");
    CHECK(n && (n->flags & ONION_UI_NODE_FLAG_CONFIRM) && contains(n->description, "Squad"));
    /* inbox: accept / decline (decline asks first), notifications */
    CHECK(find(d, "racc.req-1") && find(d, "iacc.inv-1") && find(d, "iacc.inv-0"));
    n = find(d, "rdec.req-1");
    CHECK(n && (n->flags & ONION_UI_NODE_FLAG_CONFIRM));
    CHECK(title_on(d, "inbox", "Frank sent you a friend request.") && find(d, "nread"));
    /* profile: the status list */
    n = find(d, "presence");
    CHECK(n && n->kind == ONION_UI_NODE_LIST && strcmp(n->value, "away") == 0 && strcmp(n->parent, "profile") == 0);

    /* not in a party: create, join Eve's party from the list and from her page */
    world_build(&w, AKN_COMP_READY, 0);
    why = check_doc(w.m, NULL, d);
    CHECK(why == NULL);
    if (why) fprintf(stderr, "  %s\n", why);
    CHECK(find(d, "pcreate") && find(d, "pjoin.party-eve") && find(d, "fpjoin.u-eve") && !find(d, "pleave.party-1") && !find(d, "pinvite"));
    CHECK(title_on(d, "party", "Join Eve's party \"Eve's lobby\" \xC2\xB7 1/4"));
    CHECK(title_on(d, "party", "Party invitations \xC2\xB7 1"));

    /* the read-only daemon (native interface 1): no control but Refresh */
    w.d.ipc = 1;
    world_build(&w, AKN_COMP_READY, 1);
    why = check_doc(w.m, NULL, d);
    CHECK(why == NULL);
    int controls = 0;
    for (int i = 0; i < d->count; i++) controls += interactive(d->nodes[i].kind) && strcmp(d->nodes[i].id, "refresh") != 0;
    CHECK(controls == 0);
    CHECK(title_on(d, "profile", "Status changes need the native-IPC daemon"));
    w.d.ipc = 3;

    /* not ready: why, what to do, Refresh and the diagnostics; no other page */
    static const akn_companion_state states[] = { AKN_COMP_NO_DAEMON, AKN_COMP_NO_INTERFACE, AKN_COMP_NO_KEY, AKN_COMP_NOT_PAIRED };
    for (size_t i = 0; i < 4; i++) {
        akn_companion_build(w.m, states[i], NULL, NULL, NULL, NULL, NULL, "", 0);
        akn_companion_row rows[12];
        CHECK(akn_companion_summary(w.m, rows, 12) == 1);
        why = check_doc(w.m, NULL, d);
        CHECK(why == NULL);
        CHECK(find(d, "refresh") && find(d, "diag") && !find(d, "friends") && !find(d, "profile"));
        CHECK(title_on(d, "main", "then choose Refresh"));
    }
    char text[4096];
    akn_companion_text(w.m, text, sizeof(text));
    CHECK(contains(text, "not paired"));
    free(d);
    world_free(&w);
}

/* Every list full, every text as long as its field, multi-byte characters at the cut points,
 * tombstones on top: still within the host's limits. */
static void fill(char *s, size_t len, const char *unit) {
    size_t u = strlen(unit), o = 0;
    while (o + u < len) {
        memcpy(s + o, unit, u);
        o += u;
    }
    s[o] = '\0';
}

static void test_largest_document(void) {
    akn_daemon_info d;
    akn_friend_list *f = calloc(1, sizeof(*f));
    akn_conv_list *c = calloc(1, sizeof(*c));
    akn_party *p = calloc(1, sizeof(*p));
    akn_inbox *in = calloc(1, sizeof(*in));
    akn_party_list *j = calloc(1, sizeof(*j));
    akn_thread_cache *t = calloc(AKN_UI_CONVS, sizeof(*t));
    akn_companion *m = calloc(1, sizeof(*m));
    akn_profile me;
    memset(&d, 0, sizeof(d));
    memset(&me, 0, sizeof(me));
    d.ipc = 3;
    d.paired = 1;
    fill(d.online_id, sizeof(d.online_id), "x");
    snprintf(d.realtime, sizeof(d.realtime), "connecting");
    fill(d.realtime_detail, sizeof(d.realtime_detail), "\xE2\x82\xAC");
    d.console_game.present = 1;
    fill(d.console_game.name, sizeof(d.console_game.name), "\xC3\xBC");
    snprintf(me.id, sizeof(me.id), "self-1");
    fill(me.display_name, sizeof(me.display_name), "\xF0\x9F\x8E\xAE");
    f->count = 40;
    f->online = 40;
    for (int i = 0; i < f->count; i++) {
        akn_friend *x = &f->items[i];
        snprintf(x->id, sizeof(x->id), "u-%032d", i);
        fill(x->display_name, sizeof(x->display_name), "\xC3\xA4");
        fill(x->online_id, sizeof(x->online_id), "o");
        fill(x->status_message, sizeof(x->status_message), "\xE2\x82\xAC");
        x->presence.status = AKN_PRES_ONLINE;
        x->presence.game.present = 1;
        fill(x->presence.game.name, sizeof(x->presence.game.name), "\xF0\x9F\x8E\xAE");
    }
    c->count = 30;
    c->unread_total = 999;
    for (int i = 0; i < c->count; i++) {
        akn_conversation *v = &c->items[i];
        snprintf(v->id, sizeof(v->id), "c-%034d", i);
        snprintf(v->target_id, sizeof(v->target_id), "u-%032d", i);
        fill(v->title, sizeof(v->title), "\xC3\xB6");
        fill(v->preview, sizeof(v->preview), "\xE2\x82\xAC");
        v->unread = 9;
        v->can_send = 1;
        v->last_message_id = 100 + i;
    }
    for (int i = 0; i < AKN_UI_CONVS; i++) {
        snprintf(t[i].conv_id, sizeof(t[i].conv_id), "c-%034d", i);
        t[i].state = 1;
        t[i].count = AKN_UI_THREAD;
        for (int k = 0; k < AKN_UI_THREAD; k++) {
            fill(t[i].items[k].sender, sizeof(t[i].items[k].sender), "\xC3\x9F");
            fill(t[i].items[k].body, sizeof(t[i].items[k].body), "\xF0\x9F\x98\x80");
        }
    }
    p->present = 1;
    snprintf(p->id, sizeof(p->id), "party-1");
    snprintf(p->conversation_id, sizeof(p->conversation_id), "c-%034d", 0);
    fill(p->name, sizeof(p->name), "\xC3\xA9");
    p->member_count = AKN_PARTY_MEMBERS;
    p->max_members = AKN_PARTY_MEMBERS;
    p->members_shown = AKN_PARTY_MEMBERS;
    p->voice_available = 1;
    for (int i = 0; i < AKN_PARTY_MEMBERS; i++) {
        snprintf(p->members[i].id, sizeof(p->members[i].id), "m-%d", i);
        fill(p->members[i].display_name, sizeof(p->members[i].display_name), "\xC3\xA5");
        p->members[i].voice_connected = 1;
    }
    in->invitation_count = AKN_INVITES_MAX;
    for (int i = 0; i < AKN_INVITES_MAX; i++) {
        snprintf(in->invitations[i].id, sizeof(in->invitations[i].id), "i-%d", i);
        in->invitations[i].kind = (akn_invitation_kind)(i % 3);
        fill(in->invitations[i].inviter_name, sizeof(in->invitations[i].inviter_name), "\xC3\xA7");
        fill(in->invitations[i].target_name, sizeof(in->invitations[i].target_name), "\xE2\x82\xAC");
    }
    in->incoming_count = AKN_REQUESTS_MAX;
    for (int i = 0; i < AKN_REQUESTS_MAX; i++) {
        snprintf(in->incoming[i].id, sizeof(in->incoming[i].id), "r-%d", i);
        fill(in->incoming[i].display_name, sizeof(in->incoming[i].display_name), "\xC3\xB1");
        fill(in->incoming[i].online_id, sizeof(in->incoming[i].online_id), "q");
    }
    in->notification_count = AKN_NOTIFICATIONS_MAX;
    in->notifications_unread = 99;
    for (int i = 0; i < AKN_NOTIFICATIONS_MAX; i++) fill(in->notifications[i].text, sizeof(in->notifications[i].text), "\xE2\x82\xAC");
    j->count = AKN_JOINABLE_MAX;
    for (int i = 0; i < AKN_JOINABLE_MAX; i++) {
        snprintf(j->items[i].id, sizeof(j->items[i].id), "jp-%d", i);
        fill(j->items[i].name, sizeof(j->items[i].name), "\xC3\xB8");
        j->items[i].members_shown = 1;
        snprintf(j->items[i].members[0].id, sizeof(j->items[i].members[0].id), "u-%032d", i);
        fill(j->items[i].members[0].display_name, sizeof(j->items[i].members[0].display_name), "\xC3\xBF");
        j->items[i].members[0].leader = 1;
    }
    akn_companion_extra x;
    memset(&x, 0, sizeof(x));
    x.me = &me;
    x.joinable = j;
    x.threads = t;
    x.thread_count = AKN_UI_CONVS;
    /* the voice page as full as it gets: everyone listed with the longest names, a long error */
    akn_ui_voice *voice = calloc(1, sizeof(*voice));
    voice->service = AKN_VOICE_SVC_READY;
    fill(voice->version, sizeof(voice->version), "9");
    fill(voice->summary, sizeof(voice->summary), "\xC2\xB7");
    voice->in_voice = 1;
    snprintf(voice->party_id, sizeof(voice->party_id), "party-1");
    snprintf(voice->net, sizeof(voice->net), "reconnecting");
    fill(voice->net_error, sizeof(voice->net_error), "\xE2\x82\xAC");
    snprintf(voice->mic, sizeof(voice->mic), "failed");
    snprintf(voice->out, sizeof(voice->out), "active");
    snprintf(voice->paced, sizeof(voice->paced), "unknown");
    voice->volume = 150;
    voice->mic_gain = 150;
    voice->route = 4;
    voice->people_count = AKN_UI_VOICE_PEOPLE;
    voice->people_total = 16;
    for (int i = 0; i < AKN_UI_VOICE_PEOPLE; i++) fill(voice->people[i].name, sizeof(voice->people[i].name), "\xF0\x9F\x8E\xA4");
    x.voice = voice;
    char tombs[AKN_UI_TOMBSTONES][AKN_UI_PAGE_ID_MAX];
    for (int i = 0; i < AKN_UI_TOMBSTONES; i++) snprintf(tombs[i], sizeof(tombs[i]), "c.gone-%034d", i);
    akn_doc_options o;
    memset(&o, 0, sizeof(o));
    char note[200];
    fill(note, sizeof(note), "\xE2\x82\xAC");
    o.note = note;
    o.tombstones = tombs;
    o.tombstone_count = AKN_UI_TOMBSTONES;
    wdoc *w = calloc(1, sizeof(*w));
    for (int party = 0; party < 2; party++) {
        if (!party) p->present = 0;
        else p->present = 1;
        akn_companion_build_ex(m, AKN_COMP_READY, &d, f, c, p, in, "online", 1000, &x);
        const char *why = check_doc(m, &o, w);
        CHECK(why == NULL);
        if (why) fprintf(stderr, "  largest document (party %d): %s\n", party, why);
        CHECK(w->count <= 256 && w->size <= ONION_UI_DOCUMENT_MAX_ENCODED_SIZE);
        printf("largest document (%s): %d nodes, %zu bytes\n", party ? "in a party" : "no party", w->count, w->size);
        /* the page list the plugin keeps for tombstones matches the document */
        char ids[AKN_UI_PAGES_MAX][AKN_UI_PAGE_ID_MAX];
        int n = akn_companion_page_ids(m, ids, AKN_UI_PAGES_MAX);
        int pages = 0;
        for (int i = 0; i < w->count; i++) pages += w->nodes[i].kind == ONION_UI_NODE_PAGE;
        CHECK(n == pages - AKN_UI_TOMBSTONES && n < AKN_UI_PAGES_MAX);
        for (int i = 0; i < n; i++) CHECK(find(w, ids[i]) && find(w, ids[i])->kind == ONION_UI_NODE_PAGE);
    }
    /* a name that is nothing but half a character still gives a title */
    snprintf(f->items[0].display_name, sizeof(f->items[0].display_name), "\xC3");
    f->items[0].online_id[0] = '\0';
    akn_companion_build_ex(m, AKN_COMP_READY, &d, f, c, p, in, "online", 1000, &x);
    CHECK(check_doc(m, NULL, w) == NULL);
    free(w);
    free(f);
    free(c);
    free(p);
    free(in);
    free(j);
    free(t);
    free(m);
    free(voice);
}

/* A control names its item: a reordered list cannot send a reply into another conversation. */
static void test_stable_ids(void) {
    world w;
    world_init(&w);
    wdoc *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    world_build(&w, AKN_COMP_READY, 1);
    CHECK(check_doc(w.m, NULL, a) == NULL);
    akn_conversation tmp = w.c->items[0];
    w.c->items[0] = w.c->items[1];
    w.c->items[1] = tmp;
    world_build(&w, AKN_COMP_READY, 1);
    CHECK(check_doc(w.m, NULL, b) == NULL);
    const wnode *ra = find(a, "reply.conv-bob"), *rb = find(b, "reply.conv-bob");
    CHECK(ra && rb && strcmp(ra->parent, "c.conv-bob") == 0 && strcmp(rb->parent, "c.conv-bob") == 0);
    const char *item = NULL;
    CHECK(strcmp(akn_companion_action_verb("reply.conv-bob", &item), "reply") == 0 && strcmp(item, "conv-bob") == 0);
    CHECK(strcmp(akn_companion_action_verb("fpinv.u-bob", &item), "pinv") == 0 && strcmp(item, "u-bob") == 0);
    CHECK(strcmp(akn_companion_action_verb("refresh", &item), "refresh") == 0 && item[0] == '\0');
    /* malformed or foreign IDs never route */
    CHECK(!akn_companion_action_verb("reply.", NULL));
    CHECK(!akn_companion_action_verb("reply.../etc", NULL));
    CHECK(!akn_companion_action_verb("reply.a b", NULL));
    CHECK(!akn_companion_action_verb("reply.0123456789012345678901234567890123456789", NULL));
    CHECK(!akn_companion_action_verb("id_onion_ui_0123", NULL));
    CHECK(!akn_companion_action_verb("refresh.x", NULL));
    CHECK(akn_companion_id_ok("6f1c2a3e-0b9d-4c55-8e21-3f7a9b0c1d2e") && !akn_companion_id_ok("a_b") && !akn_companion_id_ok(""));
    free(a);
    free(b);
    world_free(&w);
}

/* ---- the actions against a fake daemon -------------------------------------------------- */

#define FAKE_MAX 128
typedef struct {
    int fd, port;
    pthread_mutex_t lock;
    int count;
    char method[FAKE_MAX][8], path[FAKE_MAX][200], body[FAKE_MAX][1024];
    int refuse_writes;
} fake_daemon;

static void reply_json(int fd, const char *json) {
    char head[256];
    int n = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     strlen(json));
    if (write(fd, head, (size_t)n) < 0 || write(fd, json, strlen(json)) < 0) return;
}

static void reply_status(int fd, int status, const char *json) {
    char head[256];
    int n = snprintf(head, sizeof(head), "HTTP/1.1 %d Error\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status,
                     strlen(json));
    if (write(fd, head, (size_t)n) < 0 || write(fd, json, strlen(json)) < 0) return;
}

static void *fake_main(void *arg) {
    fake_daemon *f = arg;
    for (;;) {
        int c = accept(f->fd, NULL, NULL);
        if (c < 0) return NULL;
        char buf[8192];
        size_t got = 0;
        char *body = NULL;
        long want = 0;
        while (got < sizeof(buf) - 1) {
            ssize_t n = read(c, buf + got, sizeof(buf) - 1 - got);
            if (n <= 0) break;
            got += (size_t)n;
            buf[got] = '\0';
            if (!body && (body = strstr(buf, "\r\n\r\n"))) {
                body += 4;
                const char *cl = strstr(buf, "Content-Length: ");
                want = cl ? atol(cl + 16) : 0;
            }
            if (body && (long)(got - (size_t)(body - buf)) >= want) break;
        }
        char method[8] = "", path[200] = "";
        sscanf(buf, "%7s %199s", method, path);
        if (strcmp(path, "/api/instance") == 0) {
            reply_json(c, "{\"app\":\"akeno-network-console\",\"version\":\"t\",\"nativeIpc\":3}");
        } else if (strcmp(path, "/api/native/hello") == 0) {
            reply_json(c, "{\"ok\":true,\"ipc\":3,\"app\":\"akeno-network-console\",\"version\":\"t\",\"build\":\"b\",\"paired\":true,"
                          "\"onlineId\":\"akeno_tester\",\"realtime\":\"connected\",\"eventsNext\":5}");
        } else if (strcmp(path, "/api/state") == 0) {
            reply_json(c, "{}");
        } else if (strncmp(path, "/api/voice/", 11) == 0) {
            pthread_mutex_lock(&f->lock);
            if (f->count < FAKE_MAX) {
                snprintf(f->method[f->count], sizeof(f->method[0]), "%s", method);
                snprintf(f->path[f->count], sizeof(f->path[0]), "%s", path);
                snprintf(f->body[f->count], sizeof(f->body[0]), "%s", body ? body : "");
                f->count++;
            }
            int refuse = f->refuse_writes;
            pthread_mutex_unlock(&f->lock);
            if (refuse) reply_status(c, 409, "{\"ok\":false,\"error\":{\"code\":\"MIC_OFF\",\"message\":\"Turn the microphone on first.\"}}");
            else reply_json(c, "{\"ok\":true,\"status\":{\"app\":\"akeno-party-voice\",\"protocol\":1,\"version\":\"t\",\"summary\":\"Connected\"}}");
        } else {
            pthread_mutex_lock(&f->lock);
            if (f->count < FAKE_MAX) {
                snprintf(f->method[f->count], sizeof(f->method[0]), "%s", method);
                snprintf(f->path[f->count], sizeof(f->path[0]), "%s", path);
                snprintf(f->body[f->count], sizeof(f->body[0]), "%s", body ? body : "");
                f->count++;
            }
            int refuse = f->refuse_writes;
            pthread_mutex_unlock(&f->lock);
            if (refuse) reply_json(c, "{\"ok\":false,\"status\":409,\"error\":{\"code\":\"CONFLICT\",\"message\":\"You are already in a party.\"}}");
            else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/conversations") == 0)
                reply_json(c, "{\"ok\":true,\"status\":200,\"data\":{\"id\":\"conv-new\",\"otherUser\":{\"id\":\"u-dan\",\"onlineId\":\"it_dan\"},"
                              "\"canSend\":true}}");
            else reply_json(c, "{\"ok\":true,\"status\":200,\"data\":{}}");
        }
        close(c);
    }
}

static int fake_start(fake_daemon *f, pthread_t *thread) {
    memset(f, 0, sizeof(*f));
    pthread_mutex_init(&f->lock, NULL);
    f->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001u);
    socklen_t len = sizeof(a);
    if (f->fd < 0 || bind(f->fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(f->fd, 16) != 0 ||
        getsockname(f->fd, (struct sockaddr *)&a, &len) != 0)
        return -1;
    f->port = ntohs(a.sin_port);
    return pthread_create(thread, NULL, fake_main, f);
}

/* The request the action made (the last one recorded since `from`). */
static int fake_saw(fake_daemon *f, int from, const char *method, const char *path, const char *body_part) {
    pthread_mutex_lock(&f->lock);
    int found = 0;
    for (int i = from; i < f->count && !found; i++)
        found = strcmp(f->method[i], method) == 0 && strcmp(f->path[i], path) == 0 && (!body_part || contains(f->body[i], body_part));
    pthread_mutex_unlock(&f->lock);
    return found;
}

static int fake_count(fake_daemon *f) {
    pthread_mutex_lock(&f->lock);
    int n = f->count;
    pthread_mutex_unlock(&f->lock);
    return n;
}

static void voice_ready(akn_ui_voice *v, int in_voice);

static void test_actions(void) {
    fake_daemon *fd = calloc(1, sizeof(*fd));
    pthread_t thread;
    CHECK(fake_start(fd, &thread) == 0);
    char dir[] = "/tmp/akeno-ingame-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char keypath[300];
    snprintf(keypath, sizeof(keypath), "%s/native.key", dir);
    FILE *k = fopen(keypath, "w");
    CHECK(k != NULL);
    if (k) {
        fputs("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", k);
        fclose(k);
    }
    chmod(keypath, 0600);
    akn_daemon_link link;
    memset(&link, 0, sizeof(link));
    link.port = fd->port;
    snprintf(link.akeno_dir, sizeof(link.akeno_dir), "%s", dir);
    world w;
    world_init(&w);
    world_build(&w, AKN_COMP_READY, 1);
    akn_action_result r;
    int mark;

#define ACT(node, value) (mark = fake_count(fd), akn_companion_action(&link, w.m, node, value, &r))
    ACT("reply.conv-bob", "  on my way  ");
    CHECK(r.handled && r.ok && r.notify && strcmp(r.message, "Sent to Bob.") == 0 && (r.reload & AKN_LOAD_MESSAGES));
    CHECK(fake_saw(fd, mark, "POST", "/api/v1/conversations/conv-bob/messages", "\"body\":\"on my way\""));
    CHECK(fake_saw(fd, mark, "POST", "/api/v1/conversations/conv-bob/messages", "\"clientMessageId\":\""));
    ACT("reply.conv-bob", "   ");
    CHECK(r.handled && !r.ok && contains(r.message, "empty") && fake_count(fd) == mark);
    ACT("dm.u-dan", "gg");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/conversations", "{\"userId\":\"u-dan\"}"));
    CHECK(fake_saw(fd, mark, "POST", "/api/v1/conversations/conv-new/messages", "\"body\":\"gg\""));
    CHECK(strcmp(r.message, "Sent to Dan.") == 0);
    ACT("read.conv-bob", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/conversations/conv-bob/read", "{\"lastReadMessageId\":41}"));
    ACT("pinv.u-bob", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/parties/party-1/invitations", "{\"userId\":\"u-bob\"}"));
    ACT("fpinv.u-bob", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/parties/party-1/invitations", "{\"userId\":\"u-bob\"}"));
    ACT("pleave.party-1", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/parties/party-1/leave", NULL) && (r.reload & AKN_LOAD_PARTY));
    ACT("iacc.inv-1", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/invitations/inv-1/accept", NULL));
    ACT("idec.inv-0", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/invitations/inv-0/decline", NULL));
    ACT("racc.req-1", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/friends/requests/req-1/accept", NULL) && (r.reload & AKN_LOAD_FRIENDS));
    ACT("rdec.req-1", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/friends/requests/req-1/decline", NULL));
    ACT("nread", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/notifications/read", "{\"all\":true}"));
    ACT("presence", "dnd");
    CHECK(r.ok && fake_saw(fd, mark, "PUT", "/api/v1/presence", "{\"status\":\"dnd\"}") && contains(r.message, "Busy"));
    ACT("presence", "offline");
    CHECK(r.handled && !r.ok && fake_count(fd) == mark);
    ACT("refresh", "");
    CHECK(r.ok && !r.notify && r.reload == AKN_LOAD_ALL && fake_count(fd) == mark);
    ACT("id_onion_ui_123", "");
    CHECK(!r.handled && fake_count(fd) == mark);

    /* not in a party: create one, join a friend's party from the friend's page */
    world_build(&w, AKN_COMP_READY, 0);
    ACT("pcreate", "Late \"night\"");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/parties", "\"name\":\"Late \\\"night\\\"\""));
    ACT("fpjoin.u-eve", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/v1/parties/party-eve/join", NULL));
    ACT("pinv.u-bob", "");
    CHECK(!r.ok && contains(r.message, "not in a party") && fake_count(fd) == mark);

    /* the server refuses: the reason reaches the page */
    fd->refuse_writes = 1;
    ACT("pjoin.party-eve", "");
    CHECK(r.handled && !r.ok && contains(r.message, "Not joined") && contains(r.message, "already in a party"));
    fd->refuse_writes = 0;

    /* a read-only daemon's pages offer nothing, and a stale press changes nothing */
    w.d.ipc = 1;
    world_build(&w, AKN_COMP_READY, 1);
    ACT("reply.conv-bob", "hi");
    CHECK(r.handled && !r.ok && contains(r.message, "read-only") && fake_count(fd) == mark);
#undef ACT

    /* no daemon: a sentence, not a crash */
    w.d.ipc = 3;
    world_build(&w, AKN_COMP_READY, 1);
    akn_daemon_link gone = link;
    gone.port = 1;
    akn_companion_action(&gone, w.m, "reply.conv-bob", "hi", &r);
    CHECK(r.handled && !r.ok && contains(r.message, "not ready"));

    /* voice: every control goes to the voice service with exactly its body */
    link.voice_port = fd->port;
    w.with_voice = 1;
    voice_ready(&w.voice, 1);
    w.voice.volume = 100;
    w.voice.mic_gain = 150;
    world_build(&w, AKN_COMP_READY, 1);
#define ACT(node, value) (mark = fake_count(fd), akn_companion_action(&link, w.m, node, value, &r))
    ACT("vjoin.party-1", "");
    CHECK(r.handled && r.ok && (r.reload & AKN_LOAD_VOICE) && fake_saw(fd, mark, "POST", "/api/voice/join", "{\"partyId\":\"party-1\"}"));
    CHECK(contains(r.message, "microphone is off"));
    ACT("vjoin.party-eve", "");
    CHECK(r.handled && !r.ok && fake_count(fd) == mark); /* not your party */
    ACT("vmicon", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/mic", "{\"enabled\":true}"));
    ACT("vmicoff", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/mic", "{\"enabled\":false}"));
    ACT("vmute", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/mute", "{\"muted\":true}"));
    ACT("vunmute", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/mute", "{\"muted\":false}"));
    ACT("vdeafen", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/deafen", "{\"deafened\":true}"));
    ACT("vundeafen", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/deafen", "{\"deafened\":false}"));
    ACT("vvolup", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/volume", "{\"output\":110}") && contains(r.message, "110%"));
    ACT("vgainup", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/volume", "{\"mic\":150}")); /* the limit */
    ACT("vgaindn", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/volume", "{\"mic\":140}"));
    w.voice.volume = 0;
    world_build(&w, AKN_COMP_READY, 1);
    ACT("vvoldn", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/volume", "{\"output\":0}"));
    ACT("vroute.3", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/route", "{\"route\":3}") && contains(r.message, "3 beeps"));
    ACT("vroute.9", "");
    CHECK(r.handled && !r.ok && fake_count(fd) == mark);
    ACT("vtest", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/test-sound", "{}"));
    ACT("vleave", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/leave", "{}"));
    ACT("vexit", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/exit", "{}"));
    /* the service refuses: its reason reaches the page */
    fd->refuse_writes = 1;
    ACT("vunmute", "");
    CHECK(r.handled && !r.ok && contains(r.message, "Turn the microphone on first"));
    fd->refuse_writes = 0;
    /* the voice service is not running: a sentence, and the other pages are untouched */
    akn_daemon_link novoice = link;
    novoice.voice_port = 1;
    akn_companion_action(&novoice, w.m, "vmicon", "", &r);
    CHECK(r.handled && !r.ok && contains(r.message, "not running"));
    akn_ui_voice absent;
    akn_voice_load(&novoice, &absent);
    CHECK(absent.service == AKN_VOICE_SVC_ABSENT);
    /* voice controls work with a read-only daemon (they are the voice service's) */
    w.d.ipc = 1;
    world_build(&w, AKN_COMP_READY, 1);
    ACT("vmute", "");
    CHECK(r.ok && fake_saw(fd, mark, "POST", "/api/voice/mute", "{\"muted\":true}"));
    w.d.ipc = 3;
    w.with_voice = 0;
#undef ACT

    /* clientMessageIds differ */
    unsigned char a16[16], b16[16];
    akn_companion_random16(a16);
    akn_companion_random16(b16);
    CHECK(memcmp(a16, b16, 16) != 0);

    unlink(keypath);
    rmdir(dir);
    world_free(&w);
    shutdown(fd->fd, SHUT_RDWR);
    close(fd->fd);
    pthread_join(thread, NULL);
    free(fd);
}

/* ---- voice --------------------------------------------------------------------------------- */

static void voice_ready(akn_ui_voice *v, int in_voice) {
    memset(v, 0, sizeof(*v));
    v->service = AKN_VOICE_SVC_READY;
    snprintf(v->version, sizeof(v->version), "0.1.0-alpha");
    snprintf(v->net, sizeof(v->net), "%s", in_voice ? "connected" : "off");
    snprintf(v->summary, sizeof(v->summary), "%s", in_voice ? "Connected \xC2\xB7 Listening \xC2\xB7 Microphone off" : "Not in voice");
    snprintf(v->mic, sizeof(v->mic), "off");
    snprintf(v->out, sizeof(v->out), "%s", in_voice ? "active" : "off");
    snprintf(v->paced, sizeof(v->paced), "yes");
    v->volume = 100;
    v->mic_gain = 100;
    v->route = 1;
    v->in_voice = in_voice;
    if (in_voice) {
        snprintf(v->party_id, sizeof(v->party_id), "party-1");
        v->people_total = 9;
        v->people_count = AKN_UI_VOICE_PEOPLE;
        static const char *const names[] = { "Tester", "Bob", "Carol", "Dan", "Eve", "Frank" };
        for (int i = 0; i < AKN_UI_VOICE_PEOPLE; i++) {
            snprintf(v->people[i].name, sizeof(v->people[i].name), "%s", names[i]);
            v->people[i].self = i == 0;
            v->people[i].speaking = i == 1;
            v->people[i].muted = i == 2;
        }
        v->ev_network = 1;
    }
}

static const char *voice_doc(world *w, int with_party, wdoc *d) {
    w->with_voice = 1;
    world_build(w, AKN_COMP_READY, with_party);
    char ids[AKN_UI_PAGES_MAX][AKN_UI_PAGE_ID_MAX];
    int n = akn_companion_page_ids(w->m, ids, AKN_UI_PAGES_MAX);
    const char *why = check_doc(w->m, NULL, d);
    if (!why) {
        int pages = 0;
        for (int i = 0; i < d->count; i++) pages += d->nodes[i].kind == ONION_UI_NODE_PAGE;
        if (pages != n) why = "the page list does not match the document";
        for (int i = 0; i < n && !why; i++)
            if (!find(d, ids[i])) why = "a listed page is missing";
    }
    if (why) fprintf(stderr, "  voice document: %s\n", why);
    return why;
}

static void test_voice_pages(void) {
    world w;
    world_init(&w);
    wdoc *d = calloc(1, sizeof(*d));
    /* no voice service: the voice page says how to start it; the main page has no entry */
    memset(&w.voice, 0, sizeof(w.voice));
    CHECK(voice_doc(&w, 1, d) == NULL);
    CHECK(find(d, "voice") && title_on(d, "voice", "voice service is not running") && title_on(d, "voice", "port 9021"));
    CHECK(title_on(d, "party", "Voice chat \xC2\xB7 voice service not running") && !title_on(d, "main", "Voice chat"));
    CHECK(!find(d, "vroute") && !find(d, "vjoin.party-1") && !find(d, "vexit"));
    CHECK(title_on(d, "diag", "Voice service: not running"));
    w.voice.service = AKN_VOICE_SVC_REFUSED;
    CHECK(voice_doc(&w, 1, d) == NULL && title_on(d, "voice", "refused the daemon's key"));
    w.voice.service = AKN_VOICE_SVC_INCOMPATIBLE;
    CHECK(voice_doc(&w, 1, d) == NULL && title_on(d, "voice", "does not fit plugin"));

    /* ready, in a party, not in voice: join asks first and says the microphone stays off */
    voice_ready(&w.voice, 0);
    snprintf(w.voice.net_error, sizeof(w.voice.net_error), "The AKENO server has no console voice yet (voice pass: HTTP 404).");
    CHECK(voice_doc(&w, 1, d) == NULL);
    const wnode *n = find(d, "vjoin.party-1");
    CHECK(n && strcmp(n->key, "vjoin") == 0 && (n->flags & ONION_UI_NODE_FLAG_CONFIRM) && contains(n->description, "microphone stays off"));
    CHECK(title_on(d, "voice", "Last: The AKENO server has no console voice yet"));
    CHECK(title_on(d, "main", "Voice chat \xC2\xB7 join") && title_on(d, "party", "Voice chat \xC2\xB7 join"));
    CHECK(!find(d, "vmicon") && !find(d, "vleave") && find(d, "vroute") && find(d, "vroute.4") && find(d, "vtest"));
    CHECK(find(d, "vexit") && (find(d, "vexit")->flags & ONION_UI_NODE_FLAG_CONFIRM));
    /* not in a party: nothing to join, the page is still reachable */
    CHECK(voice_doc(&w, 0, d) == NULL);
    CHECK(!find(d, "vjoin.party-1") && title_on(d, "voice", "Join or create a party first"));

    /* in voice, microphone off: turning it on asks first */
    voice_ready(&w.voice, 1);
    CHECK(voice_doc(&w, 1, d) == NULL);
    n = find(d, "vmicon");
    CHECK(n && (n->flags & ONION_UI_NODE_FLAG_CONFIRM) && contains(n->description, "Nothing is recorded"));
    CHECK(!find(d, "vmute") && !find(d, "vunmute") && !find(d, "vjoin.party-1"));
    CHECK(title_on(d, "voice", "Tester (you)") && title_on(d, "voice", "Bob \xC2\xB7 speaking") && title_on(d, "voice", "Carol \xC2\xB7 muted"));
    CHECK(title_on(d, "voice", "+3 more in voice"));
    CHECK(title_on(d, "main", "Voice chat \xC2\xB7 Connected \xC2\xB7 8 others \xC2\xB7 mic off"));
    CHECK(find(d, "vdeafen") && find(d, "vvolup") && find(d, "vvoldn") && find(d, "vgainup") && find(d, "vgaindn"));
    CHECK(title_on(d, "voice", "Voice volume 100% \xC2\xB7 louder") && title_on(d, "voice", "Microphone gain 100% \xC2\xB7 lower"));
    n = find(d, "vleave");
    CHECK(n && (n->flags & ONION_UI_NODE_FLAG_CONFIRM));
    CHECK(title_on(d, "voice", "Measured: network yes \xC2\xB7 mic sent no \xC2\xB7 speaker output no"));
    CHECK(title_on(d, "vroute", "Route 1: system user, main port (current)") && title_on(d, "vroute", "Play the test sound (1 beep)"));
    /* microphone on: mute and off, no consent question any more */
    w.voice.mic_enabled = 1;
    snprintf(w.voice.mic, sizeof(w.voice.mic), "active");
    CHECK(voice_doc(&w, 1, d) == NULL && find(d, "vmute") && find(d, "vmicoff") && !find(d, "vmicon"));
    w.voice.muted = 1;
    CHECK(voice_doc(&w, 1, d) == NULL && find(d, "vunmute") && !find(d, "vmute"));
    w.voice.deafened = 1;
    CHECK(voice_doc(&w, 1, d) == NULL && find(d, "vundeafen") && !find(d, "vdeafen"));
    /* the microphone failed: the code, and another try */
    w.voice.mic_enabled = 0;
    w.voice.muted = 0;
    snprintf(w.voice.mic, sizeof(w.voice.mic), "failed");
    w.voice.mic_rc = (long long)(int32_t)0x80260105;
    CHECK(voice_doc(&w, 1, d) == NULL && title_on(d, "voice", "Microphone: failed (0x80260105)") && title_on(d, "voice", "Try the microphone again"));
    /* reconnecting; another party's voice */
    snprintf(w.voice.net, sizeof(w.voice.net), "reconnecting");
    snprintf(w.voice.net_error, sizeof(w.voice.net_error), "The voice connection was lost.");
    CHECK(voice_doc(&w, 1, d) == NULL && title_on(d, "main", "Voice chat \xC2\xB7 Reconnecting") && title_on(d, "voice", "Last: The voice connection was lost."));
    snprintf(w.voice.party_id, sizeof(w.voice.party_id), "party-old");
    CHECK(voice_doc(&w, 1, d) == NULL && title_on(d, "voice", "another party") && find(d, "vleave"));
    CHECK(voice_doc(&w, 0, d) == NULL && title_on(d, "voice", "You left the party") && title_on(d, "main", "Voice chat"));
    /* the read-only daemon: no join (the voice pass needs interface 3) */
    voice_ready(&w.voice, 0);
    w.d.ipc = 1;
    CHECK(voice_doc(&w, 1, d) == NULL && !find(d, "vjoin.party-1"));
    w.d.ipc = 3;
    /* the text output the end-to-end test reads */
    voice_ready(&w.voice, 1);
    w.with_voice = 1;
    world_build(&w, AKN_COMP_READY, 1);
    char text[16384];
    akn_companion_text(w.m, text, sizeof(text));
    CHECK(contains(text, "Voice service: ready 0.1.0-alpha") && contains(text, "Voice person: Bob (speaking)") && contains(text, "Voice output: active, hearing, volume 100%, route 1"));
    w.with_voice = 0;
    free(d);
    world_free(&w);
}

static void test_voice_status_parse(void) {
    akn_ui_voice v;
    const char *ok = "{\"ok\":true,\"app\":\"akeno-party-voice\",\"version\":\"0.1.0-alpha\",\"protocol\":1,\"summary\":\"Connected \xC2\xB7 Listening\","
                     "\"inVoice\":true,\"partyId\":\"party-1\",\"network\":{\"state\":\"connected\",\"error\":\"\"},"
                     "\"mic\":{\"enabled\":true,\"state\":\"active\",\"muted\":false,\"rc\":0,\"level\":42,\"gain\":110},"
                     "\"output\":{\"state\":\"active\",\"deafened\":false,\"volume\":90,\"route\":2,\"routeLabel\":\"foreground user, main port\",\"rc\":0,\"paced\":\"yes\"},"
                     "\"evidence\":{\"networkConnected\":true,\"micTransmitting\":true,\"speakerOutput\":false},"
                     "\"participants\":[{\"id\":\"a\",\"name\":\"Bob\",\"muted\":false,\"speaking\":true,\"self\":false},{\"id\":\"b\",\"name\":\"\",\"self\":true},"
                     "{\"id\":\"c\",\"name\":\"C\"},{\"id\":\"d\",\"name\":\"D\"},{\"id\":\"e\",\"name\":\"E\"},{\"id\":\"f\",\"name\":\"F\"},{\"id\":\"g\",\"name\":\"G\"},7],"
                     "\"stats\":{\"sent\":123,\"received\":456}}";
    CHECK(akn_voice_parse_status(ok, strlen(ok), &v) == 0 && v.service == AKN_VOICE_SVC_READY);
    CHECK(v.in_voice && strcmp(v.party_id, "party-1") == 0 && strcmp(v.net, "connected") == 0 && v.mic_enabled && !v.muted && v.mic_gain == 110);
    CHECK(v.volume == 90 && v.route == 2 && strcmp(v.paced, "yes") == 0 && v.ev_network && v.ev_mic && !v.ev_speaker);
    CHECK(v.people_count == AKN_UI_VOICE_PEOPLE && v.people_total == 7 && v.people[0].speaking && strcmp(v.people[1].name, "Someone") == 0 && v.people[1].self);
    CHECK(v.sent == 123 && v.received == 456);
    /* the answer of a command wraps the status */
    char wrapped[4096];
    snprintf(wrapped, sizeof(wrapped), "{\"ok\":true,\"status\":%s}", ok);
    CHECK(akn_voice_parse_status(wrapped, strlen(wrapped), &v) == 0 && v.volume == 90);
    CHECK(akn_voice_parse_status("{\"app\":\"other\",\"protocol\":1}", 30, &v) != 0 && v.service == AKN_VOICE_SVC_INCOMPATIBLE);
    const char *p2 = "{\"app\":\"akeno-party-voice\",\"protocol\":2}";
    CHECK(akn_voice_parse_status(p2, strlen(p2), &v) != 0 && v.service == AKN_VOICE_SVC_INCOMPATIBLE);
    CHECK(akn_voice_parse_status("not json", 8, &v) != 0);
    /* a party ID that is not a server ID is dropped */
    const char *bad = "{\"app\":\"akeno-party-voice\",\"protocol\":1,\"partyId\":\"../x\"}";
    CHECK(akn_voice_parse_status(bad, strlen(bad), &v) == 0 && v.party_id[0] == '\0');
    /* who speaks changes only the speaking fingerprint */
    akn_ui_voice a, b;
    akn_voice_parse_status(ok, strlen(ok), &a);
    b = a;
    b.people[0].speaking = 0;
    CHECK(akn_voice_signature(&a, 0) == akn_voice_signature(&b, 0) && akn_voice_signature(&a, 1) != akn_voice_signature(&b, 1));
    b.muted = 1;
    CHECK(akn_voice_signature(&a, 0) != akn_voice_signature(&b, 0));
}

int main(void) {
    test_summary();
    test_pages();
    test_largest_document();
    test_stable_ids();
    test_actions();
    test_voice_pages();
    test_voice_status_parse();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
