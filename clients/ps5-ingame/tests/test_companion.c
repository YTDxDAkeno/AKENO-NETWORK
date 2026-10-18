/* The in-game menu model and its OnionHEN page (validated by the SDK's own checks). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "companion.h"
#include "ui_doc.h"

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

/* An encoded page holds every text verbatim; looks for one. */
static int page_has(onion_ui_document *doc, const char *text) {
    size_t size = 0;
    if (onion_ui_document_encoded_size(doc, &size) != ONION_OK) return 0;
    unsigned char *buf = malloc(size);
    size_t used = 0;
    int found = 0;
    if (buf && onion_ui_document_encode(doc, buf, size, &used) == ONION_OK) {
        size_t n = strlen(text);
        for (size_t i = 0; i + n <= used && !found; i++) found = memcmp(buf + i, text, n) == 0;
    }
    free(buf);
    return found;
}

static void fixtures(akn_daemon_info *d, akn_friend_list *f, akn_conv_list *c, akn_party *p, akn_inbox *in) {
    memset(d, 0, sizeof(*d));
    d->ipc = 2;
    d->paired = 1;
    snprintf(d->online_id, sizeof(d->online_id), "akeno_tester");
    d->console_game.present = 1;
    snprintf(d->console_game.title_id, sizeof(d->console_game.title_id), "PPSA01234");
    snprintf(d->console_game.name, sizeof(d->console_game.name), "Nioh 3");
    memset(f, 0, sizeof(*f));
    const char *names[] = { "Bob", "Carol", "Dan", "Eve" };
    akn_presence st[] = { AKN_PRES_ONLINE, AKN_PRES_AWAY, AKN_PRES_OFFLINE, AKN_PRES_OFFLINE };
    for (int i = 0; i < 4; i++) {
        snprintf(f->items[i].display_name, sizeof(f->items[i].display_name), "%s", names[i]);
        f->items[i].presence.status = st[i];
    }
    f->items[0].presence.game.present = 1;
    snprintf(f->items[0].presence.game.name, sizeof(f->items[0].presence.game.name), "Astro Bot");
    f->count = 4;
    f->online = 2;
    f->playing = 1;
    memset(c, 0, sizeof(*c));
    c->count = 2;
    snprintf(c->items[0].title, sizeof(c->items[0].title), "Bob");
    snprintf(c->items[0].preview, sizeof(c->items[0].preview), "ready?");
    c->items[0].unread = 2;
    snprintf(c->items[1].title, sizeof(c->items[1].title), "Friday crew");
    c->unread_total = 2;
    memset(p, 0, sizeof(*p));
    p->present = 1;
    snprintf(p->name, sizeof(p->name), "Squad");
    p->member_count = 3;
    p->max_members = 8;
    memset(in, 0, sizeof(*in));
    in->invitation_count = 2;
    in->invitations[0].kind = AKN_INV_GROUP;
    in->invitations[1].kind = AKN_INV_PARTY;
    snprintf(in->invitations[1].id, sizeof(in->invitations[1].id), "inv-1");
    snprintf(in->invitations[1].inviter_name, sizeof(in->invitations[1].inviter_name), "Dan");
    snprintf(in->invitations[1].target_name, sizeof(in->invitations[1].target_name), "Late night");
    in->incoming_count = 1;
    in->notifications_unread = 1;
}

static void test_model(void) {
    akn_daemon_info d;
    akn_friend_list *f = calloc(1, sizeof(*f));
    akn_conv_list *c = calloc(1, sizeof(*c));
    akn_party p;
    akn_inbox *in = calloc(1, sizeof(*in));
    akn_companion *m = calloc(1, sizeof(*m));
    fixtures(&d, f, c, &p, in);
    akn_companion_build(m, AKN_COMP_READY, &d, f, c, &p, in, "away", 1000);
    CHECK(m->friends_online == 2 && m->online_shown == 2 && m->unread_messages == 2 && m->unread_conversations == 1);
    CHECK(m->in_party && m->party_members == 3 && m->party_invites == 1 && m->other_invites == 1 && m->friend_requests == 1);
    CHECK(strcmp(m->invite_id, "inv-1") == 0 && m->writes);
    char text[4096];
    akn_companion_text(m, text, sizeof(text));
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
    CHECK(!contains(text, "Dan -")); /* offline friends are not listed */
    CHECK(contains(text, "Bob (2): ready?"));
    CHECK(contains(text, "Dan invited you to \"Late night\""));

    /* the page: valid for the SDK, with the summary and the actions */
    onion_ui_document *doc = NULL;
    CHECK(akn_companion_document(m, "note", &doc) == ONION_OK && doc);
    CHECK(page_has(doc, "Friends: 2 online, 1 playing"));
    CHECK(page_has(doc, "Party: Squad, 3 members"));
    CHECK(page_has(doc, AKN_EV_PRESENCE) && page_has(doc, AKN_EV_ACCEPT) && page_has(doc, AKN_EV_REFRESH));
    CHECK(page_has(doc, "Dan invited you"));
    CHECK(onion_ui_document_node_count(doc) <= ONION_UI_MAX_NODES);
    onion_ui_document_destroy(doc);

    /* nothing loaded yet / long texts: still a valid page */
    for (int i = 0; i < 4; i++) memset(f->items[i].display_name, 'x', sizeof(f->items[i].display_name) - 1);
    memset(c->items[0].preview, 'y', sizeof(c->items[0].preview) - 1);
    akn_companion_build(m, AKN_COMP_READY, &d, f, c, &p, in, "", 1000);
    CHECK(akn_companion_document(m, NULL, &doc) == ONION_OK);
    onion_ui_document_destroy(doc);
    akn_companion_build(m, AKN_COMP_READY, &d, NULL, NULL, NULL, NULL, "", 1000);
    akn_companion_text(m, text, sizeof(text));
    CHECK(contains(text, "Friends         0 online") && contains(text, "No unread messages") && contains(text, "Not in a party"));
    CHECK(akn_companion_document(m, NULL, &doc) == ONION_OK);
    onion_ui_document_destroy(doc);

    /* the daemon without writes (IPC 1): the actions are shown disabled, never offered */
    d.ipc = 1;
    akn_companion_build(m, AKN_COMP_READY, &d, f, c, &p, in, "online", 1000);
    CHECK(!m->writes);
    CHECK(akn_companion_document(m, NULL, &doc) == ONION_OK);
    CHECK(page_has(doc, "Quick actions unavailable"));
    CHECK(!page_has(doc, AKN_EV_PRESENCE));
    CHECK(!page_has(doc, AKN_EV_ACCEPT));
    onion_ui_document_destroy(doc);

    /* IPC 2 is writable, but an absent invitation must not create a
     * disabled action (unsupported by the real OnionHEN renderer). */
    d.ipc = 2;
    akn_companion_build(m, AKN_COMP_READY, &d, f, c, &p, NULL, "online", 1000);
    CHECK(akn_companion_document(m, NULL, &doc) == ONION_OK);
    CHECK(page_has(doc, AKN_EV_PRESENCE));
    CHECK(!page_has(doc, AKN_EV_ACCEPT));
    CHECK(page_has(doc, "No invitation waiting"));
    onion_ui_document_destroy(doc);

    /* not ready: one line that says why, and Refresh */
    static const akn_companion_state states[] = { AKN_COMP_NO_DAEMON, AKN_COMP_NO_INTERFACE, AKN_COMP_NO_KEY, AKN_COMP_NOT_PAIRED };
    for (size_t i = 0; i < 4; i++) {
        akn_companion_build(m, states[i], NULL, NULL, NULL, NULL, NULL, "", 0);
        akn_companion_row rows[12];
        CHECK(akn_companion_summary(m, rows, 12) == 1);
        CHECK(akn_companion_document(m, NULL, &doc) == ONION_OK);
        CHECK(page_has(doc, AKN_EV_REFRESH) && !page_has(doc, AKN_EV_ACCEPT));
        onion_ui_document_destroy(doc);
    }
    akn_companion_text(m, text, sizeof(text));
    CHECK(contains(text, "not paired"));
    CHECK(strcmp(akn_companion_presence_label("dnd"), "Busy") == 0 && strcmp(akn_companion_presence_label(""), "Unknown") == 0);
    free(f);
    free(c);
    free(in);
    free(m);
}

int main(void) {
    test_model();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
