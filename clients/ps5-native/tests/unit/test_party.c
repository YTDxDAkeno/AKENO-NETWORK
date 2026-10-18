/* Parties, invitations, friend requests and notifications (core/party.h). */
#include <stdlib.h>
#include <string.h>

#include "akeno/json.h"
#include "party.h"
#include "test.h"

static akeno_json_doc *parse(const char *json, const akeno_json **root) {
    akeno_json_doc *doc = akeno_json_parse(json, strlen(json), NULL, 0);
    *root = doc ? akeno_json_root(doc) : NULL;
    return doc;
}

#define USER(id, oid, dn) "{\"id\":\"" id "\",\"onlineId\":\"" oid "\",\"displayName\":\"" dn "\",\"avatar\":\"a\",\"avatarImage\":null,\"deleted\":false"

static const char *PARTY_JSON =
    "{\"id\":\"p1\",\"name\":\"Squad\",\"privacy\":\"friends\",\"leaderId\":\"u1\",\"maxMembers\":8,\"memberCount\":2,"
    "\"createdAt\":\"2026-10-17T20:00:00Z\",\"isMember\":true,\"conversationId\":\"c1\","
    "\"members\":[{\"user\":" USER("u1", "alice", "Alice") ",\"presence\":{\"status\":\"online\"}},\"joinedAt\":\"2026-10-17T20:00:00Z\","
    "\"isLeader\":true,\"voice\":{\"connected\":true,\"muted\":false}},"
    "{\"user\":" USER("u2", "bob", "") ",\"presence\":{\"status\":\"away\"}},\"joinedAt\":\"2026-10-17T20:01:00Z\",\"isLeader\":false,"
    "\"voice\":{\"connected\":false,\"muted\":null}}],"
    "\"voice\":{\"available\":false,\"reason\":\"Voice chat is not configured on this server.\"}}";

static void test_party(void) {
    char json[4096];
    const akeno_json *root;
    akn_party *p = calloc(1, sizeof(*p));
    snprintf(json, sizeof(json), "{\"party\":%s}", PARTY_JSON);
    akeno_json_doc *doc = parse(json, &root);
    CHECK(akn_fill_current_party(root, p) == 0);
    akeno_json_free(doc);
    CHECK(p->present && p->is_member && p->member_count == 2 && p->members_shown == 2 && p->max_members == 8);
    CHECK_STR(p->name, "Squad");
    CHECK_STR(p->conversation_id, "c1");
    CHECK(p->members[0].leader && p->members[0].voice_connected && p->members[0].voice_muted == 0);
    CHECK(!p->members[1].leader && !p->members[1].voice_connected && p->members[1].voice_muted == -1);
    CHECK_STR(p->members[1].display_name, "bob");
    CHECK(p->members[1].presence.status == AKN_PRES_AWAY);
    CHECK(!p->voice_available);
    CHECK_STR(p->voice_reason, "Voice chat is not configured on this server.");
    CHECK(akn_party_member_find(p, "u2") == 1 && akn_party_member_find(p, "zz") == -1 && akn_party_member_find(NULL, "u1") == -1);

    doc = parse("{\"party\":null}", &root);
    CHECK(akn_fill_current_party(root, p) == 0 && !p->present);
    akeno_json_free(doc);
    doc = parse("{\"nothing\":1}", &root);
    CHECK(akn_fill_current_party(root, p) == -1);
    akeno_json_free(doc);
    CHECK(akn_fill_current_party(NULL, p) == -1);

    /* joinable parties: your own is not offered */
    akn_party_list *l = calloc(1, sizeof(*l));
    snprintf(json, sizeof(json), "{\"parties\":[%s,{\"id\":\"p2\",\"name\":\"Raid\",\"isMember\":false,\"memberCount\":3,\"maxMembers\":4,"
                                 "\"members\":[],\"voice\":{\"available\":true,\"reason\":null}},{\"name\":\"no id\"}]}", PARTY_JSON);
    doc = parse(json, &root);
    CHECK(akn_fill_joinable_parties(root, "p1", l) == 0);
    akeno_json_free(doc);
    CHECK(l->count == 1);
    CHECK_STR(l->items[0].name, "Raid");
    CHECK(l->items[0].member_count == 3 && l->items[0].voice_available);

    /* the Party screen's rows */
    akn_inbox *in = calloc(1, sizeof(*in));
    in->invitation_count = 2;
    in->invitations[0].kind = AKN_INV_GROUP;
    in->invitations[1].kind = AKN_INV_PARTY;
    akn_party_row rows[AKN_PARTY_ROWS_MAX];
    snprintf(json, sizeof(json), "{\"party\":%s}", PARTY_JSON);
    doc = parse(json, &root);
    akn_fill_current_party(root, p);
    akeno_json_free(doc);
    int n = akn_party_rows(p, in, l, rows, AKN_PARTY_ROWS_MAX);
    CHECK(n == 7);
    CHECK(rows[0].kind == AKN_PROW_CHAT && rows[1].kind == AKN_PROW_INVITE && rows[2].kind == AKN_PROW_LEAVE);
    CHECK(rows[3].kind == AKN_PROW_MEMBER && rows[4].kind == AKN_PROW_MEMBER && rows[4].index == 1);
    CHECK(rows[5].kind == AKN_PROW_INVITATION && rows[5].index == 1); /* only party invitations */
    CHECK(rows[6].kind == AKN_PROW_JOINABLE);
    /* party voice: a Join row after Leave when the server offers voice; the controls while in voice */
    p->voice_available = 1;
    n = akn_party_rows_ex(p, in, l, 0, rows, AKN_PARTY_ROWS_MAX);
    CHECK(n == 8 && rows[3].kind == AKN_PROW_VOICE && rows[4].kind == AKN_PROW_MEMBER);
    n = akn_party_rows_ex(p, in, l, 1, rows, AKN_PARTY_ROWS_MAX);
    CHECK(n == 12 && rows[3].kind == AKN_PROW_VOICE && rows[4].kind == AKN_PROW_VOICE_MUTE && rows[5].kind == AKN_PROW_VOICE_DEAFEN &&
          rows[6].kind == AKN_PROW_VOICE_OUTPUT && rows[7].kind == AKN_PROW_VOICE_MIC && rows[8].kind == AKN_PROW_MEMBER);
    CHECK(akn_party_rows(p, in, l, rows, AKN_PARTY_ROWS_MAX) == 8); /* the plain list never shows the controls */
    memset(p, 0, sizeof(*p));
    n = akn_party_rows(p, in, NULL, rows, AKN_PARTY_ROWS_MAX);
    CHECK(n == 2 && rows[0].kind == AKN_PROW_CREATE);
    CHECK(akn_party_rows(NULL, NULL, NULL, rows, 1) == 1);
    CHECK(akn_fill_joinable_parties(NULL, NULL, l) == -1 && l->count == 0);

    char body[200];
    CHECK(akn_party_create_json("", 0, body, sizeof(body)) == 0);
    CHECK_STR(body, "{\"privacy\":\"friends\"}");
    CHECK(akn_party_create_json("Alice's \"party\"", 1, body, sizeof(body)) == 0);
    CHECK_STR(body, "{\"name\":\"Alice's \\\"party\\\"\",\"privacy\":\"invite_only\"}");
    CHECK(akn_party_create_json("x", 0, body, 8) == -1);
    free(p);
    free(l);
    free(in);
}

static void test_inbox(void) {
    akn_inbox *in = calloc(1, sizeof(*in));
    const akeno_json *root;
    akeno_json_doc *doc = parse(
        "{\"invitations\":[{\"id\":\"i1\",\"kind\":\"party\",\"inviter\":" USER("u2", "bob", "Bob") "},\"invitee\":" USER("u1", "alice", "Alice")
        "},\"target\":{\"id\":\"p9\",\"name\":\"Late night\"},\"createdAt\":\"2026-10-17T20:00:00Z\",\"expiresAt\":\"2026-10-18T20:00:00Z\"},"
        "{\"id\":\"i2\",\"kind\":\"game_session\",\"inviter\":" USER("u3", "carol", "Carol") "},\"target\":{\"id\":\"s1\",\"name\":\"Raid\"}},"
        "{\"id\":\"i3\",\"kind\":\"party\",\"inviter\":{}}]}",
        &root);
    CHECK(akn_fill_invitations(root, in) == 0);
    akeno_json_free(doc);
    CHECK(in->invitation_count == 2);
    CHECK(in->invitations[0].kind == AKN_INV_PARTY && in->invitations[1].kind == AKN_INV_SESSION);
    CHECK_STR(in->invitations[0].inviter_name, "Bob");
    CHECK_STR(in->invitations[0].target_name, "Late night");
    CHECK(in->invitations[0].expires_at > in->invitations[0].created_at);
    CHECK_STR(akn_invitation_kind_label(AKN_INV_SESSION), "game session");

    doc = parse("{\"incoming\":[{\"id\":\"r1\",\"direction\":\"incoming\",\"user\":" USER("u4", "dave", "Dave") "},\"createdAt\":\"2026-10-17T20:00:00Z\"}],"
                "\"outgoing\":[{\"id\":\"r2\"},{\"id\":\"r3\"}]}",
                &root);
    CHECK(akn_fill_friend_requests(root, in) == 0);
    akeno_json_free(doc);
    CHECK(in->incoming_count == 1 && in->outgoing_count == 2);
    CHECK_STR(in->incoming[0].display_name, "Dave");
    CHECK_STR(in->incoming[0].id, "r1");
    CHECK(akn_fill_friend_requests(NULL, in) == -1);

    doc = parse("{\"notifications\":[{\"id\":5,\"data\":{\"type\":\"friend_request.accepted\",\"actor\":" USER("u2", "bob", "Bob") "}},"
                "\"createdAt\":\"2026-10-17T20:00:00Z\",\"readAt\":null},"
                "{\"id\":4,\"data\":{\"type\":\"invitation.received\",\"actor\":" USER("u2", "bob", "") "},\"kind\":\"party\",\"targetName\":\"Squad\"},"
                "\"createdAt\":\"2026-10-17T19:00:00Z\",\"readAt\":\"2026-10-17T19:30:00Z\"},"
                "{\"id\":3,\"data\":{\"type\":\"something.new\"},\"createdAt\":\"2026-10-17T18:00:00Z\",\"readAt\":null}],"
                "\"unreadCount\":7,\"hasMore\":true}",
                &root);
    CHECK(akn_fill_notifications(root, in) == 0);
    akeno_json_free(doc);
    CHECK(in->notification_count == 2 && in->notifications_unread == 7);
    CHECK_STR(in->notifications[0].text, "Bob accepted your friend request.");
    CHECK_STR(in->notifications[1].text, "bob invited you to the party \"Squad\".");
    CHECK(in->notifications[0].unread && !in->notifications[1].unread && in->notifications[0].id == 5);
    CHECK(akn_fill_notifications(NULL, in) == -1);
    free(in);
}

void run_party_tests(void) {
    test_party();
    test_inbox();
}
