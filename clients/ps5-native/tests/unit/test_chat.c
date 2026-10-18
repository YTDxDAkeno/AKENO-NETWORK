/* Messages (core/chat.h) and the on-screen keyboard (core/osk.h). */
#include <stdlib.h>
#include <string.h>

#include "akeno/json.h"
#include "chat.h"
#include "osk.h"
#include "test.h"

#define ME "11111111-1111-4111-8111-111111111111"
#define BOB "22222222-2222-4222-8222-222222222222"
#define CONV "33333333-3333-4333-8333-333333333333"
#define GROUPCONV "44444444-4444-4444-8444-444444444444"
#define PARTYCONV "55555555-5555-4555-8555-555555555555"

static akeno_json_doc *parse(const char *json, const akeno_json **root) {
    akeno_json_doc *doc = akeno_json_parse(json, strlen(json), NULL, 0);
    *root = doc ? akeno_json_root(doc) : NULL;
    return doc;
}

static const char *msg_json(char *buf, size_t len, long long id, const char *sender, const char *body, const char *client) {
    snprintf(buf, len,
             "{\"id\":%lld,\"conversationId\":\"" CONV "\",\"sender\":{\"id\":\"%s\",\"onlineId\":\"%s\",\"displayName\":\"%s\","
             "\"avatar\":\"a1\",\"avatarImage\":null,\"deleted\":false},\"body\":\"%s\",\"clientMessageId\":%s%s%s,"
             "\"createdAt\":\"2026-10-17T20:00:%02lldZ\"}",
             id, sender, strcmp(sender, ME) == 0 ? "me" : "bob", strcmp(sender, ME) == 0 ? "Me" : "Bob", body, client ? "\"" : "",
             client ? client : "null", client ? "\"" : "", id % 60);
    return buf;
}

static void test_conversations(void) {
    const char *json =
        "{\"conversations\":["
        "{\"id\":\"" CONV "\",\"kind\":\"direct\",\"otherUser\":{\"id\":\"" BOB "\",\"onlineId\":\"bob\",\"displayName\":\"Bob\","
        "\"avatar\":\"a\",\"avatarImage\":\"0123456789abcdef0123456789abcdef\",\"deleted\":false,"
        "\"presence\":{\"status\":\"online\",\"lastSeenAt\":null,\"activity\":null,\"game\":null}},"
        "\"lastMessage\":{\"id\":7,\"conversationId\":\"" CONV "\",\"sender\":{\"id\":\"" BOB "\",\"onlineId\":\"bob\","
        "\"displayName\":\"Bob\"},\"body\":\"line one\\nline   two\",\"clientMessageId\":null,\"createdAt\":\"2026-10-17T20:00:00Z\"},"
        "\"lastMessageAt\":\"2026-10-17T20:00:00Z\",\"unreadCount\":2,\"lastReadMessageId\":5,\"canSend\":true},"
        "{\"id\":\"66666666-6666-4666-8666-666666666666\",\"kind\":\"direct\",\"otherUser\":{\"id\":\"x\",\"onlineId\":\"carol\","
        "\"displayName\":\"\"},\"lastMessage\":null,\"lastMessageAt\":null,\"unreadCount\":0,\"lastReadMessageId\":0,\"canSend\":false},"
        "{\"id\":\"broken\"}]}";
    const akeno_json *root;
    akeno_json_doc *doc = parse(json, &root);
    akn_conv_list *l = calloc(1, sizeof(*l));
    CHECK(akn_fill_direct_conversations(root, l) == 0);
    CHECK(l->count == 2);
    CHECK_STR(l->items[0].title, "Bob");
    CHECK_STR(l->items[0].handle, "bob");
    CHECK_STR(l->items[0].preview, "line one line two");
    CHECK(l->items[0].unread == 2 && l->items[0].can_send && l->items[0].last_message_id == 7 && !l->items[0].preview_mine);
    CHECK(l->items[0].presence.status == AKN_PRES_ONLINE);
    CHECK_STR(l->items[0].avatar, "0123456789abcdef0123456789abcdef");
    CHECK_STR(l->items[1].title, "carol"); /* no display name: the Online ID */
    CHECK(!l->items[1].can_send);
    CHECK(l->unread_total == 2);
    akeno_json_free(doc);

    doc = parse("{\"groups\":[{\"id\":\"g1\",\"name\":\"Friday crew\",\"conversationId\":\"" GROUPCONV "\",\"myRole\":\"member\","
                "\"memberCount\":5,\"unreadCount\":3,\"updatedAt\":\"2026-10-17T21:00:00Z\"},"
                "{\"id\":\"g2\",\"name\":\"Not mine\",\"conversationId\":null,\"myRole\":null,\"memberCount\":9,\"unreadCount\":0}]}",
                &root);
    CHECK(akn_add_group_conversations(root, l) == 0);
    CHECK(l->count == 3);
    CHECK_STR(l->items[0].title, "Friday crew"); /* the most recent first */
    CHECK(l->items[0].kind == AKN_CONV_GROUP && l->items[0].members == 5 && l->unread_total == 5);
    akeno_json_free(doc);
    CHECK(akn_add_group_conversations(NULL, l) == -1);

    akn_add_party_conversation(l, PARTYCONV, "p1", "Squad", 3, 0);
    CHECK(l->count == 4 && l->items[0].kind == AKN_CONV_PARTY);
    CHECK_STR(l->items[0].title, "Squad");
    akn_add_party_conversation(l, PARTYCONV, "p1", "Squad renamed", 4, 0); /* updated, not added twice */
    CHECK(l->count == 4 && l->items[0].members == 4);
    akn_add_party_conversation(l, "77777777-7777-4777-8777-777777777777", "p2", "", 2, 0); /* a new party replaces the old one */
    CHECK(l->count == 4 && akn_conversations_find(l, PARTYCONV) < 0);
    CHECK_STR(l->items[0].title, "Party");
    CHECK(akn_conversations_find(l, CONV) >= 0 && akn_conversations_find(l, "") == -1 && akn_conversations_find(NULL, CONV) == -1);
    CHECK(akn_fill_direct_conversations(NULL, l) == -1 && l->count == 0);
    free(l);
}

static void test_threads(void) {
    akn_thread *t = calloc(1, sizeof(*t));
    snprintf(t->conversation_id, sizeof(t->conversation_id), "%s", CONV);
    char a[600], b[600], c[600], page[2400];
    snprintf(page, sizeof(page), "{\"messages\":[%s,%s],\"hasMore\":true}", msg_json(a, sizeof(a), 5, BOB, "hi", NULL),
             msg_json(b, sizeof(b), 6, ME, "hello \\\"you\\\"", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    const akeno_json *root;
    akeno_json_doc *doc = parse(page, &root);
    CHECK(akn_fill_thread(root, ME, t) == 0);
    akeno_json_free(doc);
    CHECK(t->count == 2 && t->has_more && t->newest_id == 6);
    CHECK(!t->items[0].mine && t->items[1].mine);
    CHECK_STR(t->items[1].body, "hello \"you\"");
    CHECK_STR(t->items[0].sender_name, "Bob");
    CHECK(t->items[0].created_at > 0);

    /* sending: pending at once, confirmed once (the send answer and the event both arrive) */
    const char *cid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    CHECK(akn_thread_add_pending(t, cid, ME, "Me", "on my way", 1000) == 0);
    CHECK(akn_thread_add_pending(t, cid, ME, "Me", "on my way", 1000) == 0 && t->count == 3); /* a retry: still one */
    CHECK(t->items[2].state == AKN_MSG_SENDING && t->items[2].id == 0);
    akn_thread_set_state(t, cid, AKN_MSG_FAILED);
    CHECK(t->items[2].state == AKN_MSG_FAILED);
    akn_message m;
    doc = parse(msg_json(c, sizeof(c), 9, ME, "on my way", cid), &root);
    CHECK(akn_parse_message(root, ME, &m) == 0);
    akeno_json_free(doc);
    CHECK(akn_thread_add(t, &m) == 1);
    CHECK(t->count == 3 && t->items[2].id == 9 && t->items[2].state == AKN_MSG_SENT && t->newest_id == 9);
    CHECK(akn_thread_add(t, &m) == 0 && t->count == 3); /* the same message from the event ring */
    /* a message from bob that arrived between: sorted by ID */
    doc = parse(msg_json(c, sizeof(c), 8, BOB, "where are you", NULL), &root);
    CHECK(akn_parse_message(root, ME, &m) == 0);
    akeno_json_free(doc);
    CHECK(akn_thread_add(t, &m) == 1);
    CHECK(t->items[2].id == 8 && t->items[3].id == 9);
    /* a pending message stays at the end when a page is loaded again */
    CHECK(akn_thread_add_pending(t, "cccccccc-cccc-4ccc-8ccc-cccccccccccc", ME, "Me", "later", 1001) == 0);
    doc = parse(page, &root);
    CHECK(akn_fill_thread(root, ME, t) == 0);
    akeno_json_free(doc);
    CHECK(t->count == 3 && t->items[2].id == 0);
    CHECK_STR(t->items[2].body, "later");
    CHECK(akn_thread_oldest_id(t) == 5);
    /* an older page goes in front */
    snprintf(page, sizeof(page), "{\"messages\":[%s],\"hasMore\":false}", msg_json(a, sizeof(a), 2, BOB, "earlier", NULL));
    doc = parse(page, &root);
    CHECK(akn_thread_prepend(root, ME, t) == 0);
    akeno_json_free(doc);
    CHECK(t->items[0].id == 2 && !t->has_more && t->count == 4);
    /* not a message */
    doc = parse("{\"id\":0,\"body\":\"x\"}", &root);
    CHECK(akn_parse_message(root, ME, &m) == -1);
    akeno_json_free(doc);
    CHECK(akn_parse_message(NULL, ME, &m) == -1);
    CHECK(akn_fill_thread(NULL, ME, t) == -1);

    /* a full thread drops its oldest message */
    memset(t, 0, sizeof(*t));
    for (int i = 1; i <= AKN_CHAT_MSGS + 5; i++) {
        memset(&m, 0, sizeof(m));
        m.id = i;
        snprintf(m.body, sizeof(m.body), "m%d", i);
        akn_thread_add(t, &m);
    }
    CHECK(t->count == AKN_CHAT_MSGS && t->items[0].id == 6 && t->has_more && t->newest_id == AKN_CHAT_MSGS + 5);
    m.id = 1; /* older than everything kept: ignored */
    CHECK(akn_thread_add(t, &m) == 0);
    /* long bodies are cut at a character boundary */
    char *longbody = malloc(AKN_MSG_BODY + 64);
    memset(longbody, 0, AKN_MSG_BODY + 64);
    for (int i = 0; i + 2 < AKN_MSG_BODY + 60; i += 2) memcpy(longbody + i, "\xC3\xA9", 2);
    memset(t, 0, sizeof(*t));
    CHECK(akn_thread_add_pending(t, cid, ME, "Me", longbody, 1) == 0);
    CHECK(t->items[0].truncated && strlen(t->items[0].body) % 2 == 0);
    free(longbody);
    free(t);
}

static void test_events(void) {
    akn_conv_list *l = calloc(1, sizeof(*l));
    akn_thread *t = calloc(1, sizeof(*t));
    l->count = 2;
    snprintf(l->items[0].id, 40, "%s", CONV);
    l->items[0].last_message_id = 7;
    snprintf(l->items[1].id, 40, "%s", GROUPCONV);
    l->items[1].unread = 4;
    l->items[1].last_message_id = 20;
    snprintf(t->conversation_id, sizeof(t->conversation_id), "%s", GROUPCONV);
    char m1[600], m2[600], ev[3000];
    msg_json(m1, sizeof(m1), 8, BOB, "ping", NULL);
    msg_json(m2, sizeof(m2), 21, BOB, "group news", NULL);
    snprintf(ev, sizeof(ev),
             "{\"ok\":true,\"next\":10,\"events\":["
             "{\"seq\":1,\"type\":\"message.created\",\"data\":{\"conversationId\":\"" CONV "\",\"conversationKind\":\"direct\",\"message\":%s}},"
             "{\"seq\":2,\"type\":\"message.created\",\"data\":{\"conversationId\":\"" GROUPCONV "\",\"conversationKind\":\"group\",\"message\":%s}},"
             "{\"seq\":3,\"type\":\"message.created\",\"data\":{\"conversationId\":\"" CONV "\",\"conversationKind\":\"direct\",\"message\":%s}},"
             "{\"seq\":4,\"type\":\"presence.updated\",\"data\":{}}]}",
             m1, m2, m1);
    akn_chat_events r;
    CHECK(akn_chat_apply_events(ev, strlen(ev), ME, l, t, &r) == 0);
    CHECK(r.messages == 3 && r.in_open == 1 && r.from_others == 3 && !r.reload_list && !r.reload_thread);
    int direct = akn_conversations_find(l, CONV), group = akn_conversations_find(l, GROUPCONV);
    CHECK(l->items[direct].unread == 1);  /* the repeated event counts once */
    CHECK_STR(l->items[direct].preview, "ping");
    CHECK(l->items[group].unread == 4);   /* the open conversation does not count as unread */
    CHECK(t->count == 1 && t->items[0].id == 21);
    CHECK(l->unread_total == 5);
    /* read on another device */
    snprintf(ev, sizeof(ev), "{\"ok\":true,\"next\":11,\"events\":[{\"seq\":5,\"type\":\"conversation.read\",\"data\":{\"conversationId\":\"" CONV
                             "\",\"lastReadMessageId\":8}},{\"seq\":6,\"type\":\"conversation.read\",\"data\":{\"conversationId\":\"" GROUPCONV
                             "\",\"lastReadMessageId\":3}}]}");
    CHECK(akn_chat_apply_events(ev, strlen(ev), ME, l, t, &r) == 0);
    CHECK(l->items[akn_conversations_find(l, CONV)].unread == 0);
    CHECK(r.reload_list); /* the group was read only up to an earlier message */
    /* too large for the ring, or a new conversation: reload */
    snprintf(ev, sizeof(ev), "{\"ok\":true,\"next\":12,\"events\":[{\"seq\":7,\"type\":\"message.created\",\"data\":null}]}");
    CHECK(akn_chat_apply_events(ev, strlen(ev), ME, l, t, &r) == 0 && r.reload_list && r.reload_thread);
    msg_json(m1, sizeof(m1), 30, BOB, "new", NULL);
    snprintf(ev, sizeof(ev), "{\"ok\":true,\"next\":13,\"events\":[{\"seq\":8,\"type\":\"message.created\",\"data\":{\"conversationId\":"
                             "\"88888888-8888-4888-8888-888888888888\",\"message\":%s}}]}", m1);
    CHECK(akn_chat_apply_events(ev, strlen(ev), ME, l, t, &r) == 0 && r.reload_list && !r.reload_thread);
    CHECK(akn_chat_apply_events("nope", 4, ME, l, t, &r) == -1);
    CHECK(akn_chat_apply_events("{\"ok\":false}", 12, ME, l, t, &r) == -1);
    CHECK(akn_chat_apply_events("{\"ok\":true,\"events\":[]}", 23, ME, NULL, NULL, &r) == 0);
    free(l);
    free(t);
}

static void test_sending_text(void) {
    uint8_t rnd[16];
    for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)(0xF0 + i);
    char uuid[37];
    akn_uuid_v4(rnd, uuid);
    CHECK_STR(uuid, "f0f1f2f3-f4f5-46f7-b8f9-fafbfcfdfeff");
    char out[4096];
    CHECK(akn_message_body_json("Hi \"Bob\"\nline\\2 \xC3\xA9", uuid, out, sizeof(out)) == 0);
    CHECK_STR(out, "{\"body\":\"Hi \\\"Bob\\\"\\nline\\\\2 \xC3\xA9\",\"clientMessageId\":\"f0f1f2f3-f4f5-46f7-b8f9-fafbfcfdfeff\"}");
    const akeno_json *root;
    akeno_json_doc *doc = parse(out, &root); /* it is valid JSON and round-trips */
    CHECK(doc && strcmp(akeno_json_get_string(root, "body", ""), "Hi \"Bob\"\nline\\2 \xC3\xA9") == 0);
    akeno_json_free(doc);
    CHECK(akn_message_body_json("   ", uuid, out, sizeof(out)) == -1);
    CHECK(akn_message_body_json("\n\n", uuid, out, sizeof(out)) == -1);
    CHECK(akn_message_body_json("bad \x01", uuid, out, sizeof(out)) == -1);
    CHECK(akn_message_body_json("bad \xC3", uuid, out, sizeof(out)) == -1);
    CHECK(akn_message_body_json("ok", "short", out, sizeof(out)) == -1);
    CHECK(akn_message_body_json("ok", uuid, out, 20) == -1);
    char *big = malloc(AKN_MSG_SEND_MAX + 2);
    memset(big, 'x', AKN_MSG_SEND_MAX + 1);
    big[AKN_MSG_SEND_MAX + 1] = '\0';
    CHECK(!akn_message_valid(big));
    big[AKN_MSG_SEND_MAX] = '\0';
    CHECK(akn_message_valid(big));
    free(big);
    CHECK(akn_json_quote("a\tb\x1f", out, sizeof(out)) == 0);
    CHECK_STR(out, "\"a\\tb\\u001f\"");
    CHECK(akn_utf8_count("\xC3\xA9t\xC3\xA9") == 3);
    char p[12];
    akn_chat_preview("  hello\n\nworld  ", p, sizeof(p));
    CHECK_STR(p, "hello world");
    akn_chat_preview("ab\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", p, 6); /* never half a character */
    CHECK_STR(p, "ab\xC3\xA9");
}

static akn_osk_result press(akn_osk *k, akn_button b) {
    akn_input_event ev = { b, AKN_EV_PRESS, AKN_SRC_PAD };
    return akn_osk_handle(k, &ev);
}

static void test_keyboard(void) {
    akn_osk k;
    akn_osk_init(&k, NULL, 10, 0);
    akn_osk_key key;
    /* the first letter is upper case */
    CHECK(k.row == 1 && k.col == 0 && k.shift);
    CHECK(akn_osk_key_at(&k, 1, 0, &key) && key.kind == AKN_KEY_CHAR);
    CHECK_STR(key.text, "Q");
    CHECK(press(&k, AKN_BTN_CROSS) == AKN_OSK_CHANGED);
    CHECK_STR(k.text, "Q");
    CHECK(!k.shift);
    press(&k, AKN_BTN_RIGHT);
    press(&k, AKN_BTN_CROSS);
    CHECK_STR(k.text, "Qw");
    press(&k, AKN_BTN_TRIANGLE);
    CHECK_STR(k.text, "Qw ");
    press(&k, AKN_BTN_SQUARE);
    CHECK_STR(k.text, "Qw");
    /* wrap-around and the wide keys of the action row */
    press(&k, AKN_BTN_UP);
    press(&k, AKN_BTN_UP);
    CHECK(k.row == 4);
    CHECK(akn_osk_key_at(&k, 4, k.col, &key) && key.kind == AKN_KEY_LAYER);
    press(&k, AKN_BTN_RIGHT);
    CHECK(akn_osk_key_at(&k, 4, k.col, &key) && key.kind == AKN_KEY_SPACE && k.col == 2);
    press(&k, AKN_BTN_RIGHT);
    CHECK(akn_osk_key_at(&k, 4, k.col, &key) && key.kind == AKN_KEY_BACKSPACE && k.col == 6);
    press(&k, AKN_BTN_RIGHT);
    CHECK(akn_osk_key_at(&k, 4, k.col, &key) && key.kind == AKN_KEY_DONE);
    press(&k, AKN_BTN_RIGHT);
    CHECK(k.col == 0);
    press(&k, AKN_BTN_LEFT);
    CHECK(k.col == 8);
    akn_osk_key row[AKN_OSK_COLS];
    CHECK(akn_osk_row_keys(&k, 4, row, AKN_OSK_COLS) == 5);
    CHECK(akn_osk_row_keys(&k, 0, row, AKN_OSK_COLS) == 10);
    /* symbols, including multi-byte characters */
    press(&k, AKN_BTN_R1);
    CHECK(akn_osk_key_at(&k, 2, 8, &key));
    CHECK_STR(key.text, "\xE2\x82\xAC");
    k.row = 2, k.col = 8;
    press(&k, AKN_BTN_CROSS);
    CHECK_STR(k.text, "Qw\xE2\x82\xAC");
    CHECK(akn_osk_chars(&k) == 3);
    press(&k, AKN_BTN_SQUARE); /* deletes the whole character */
    CHECK_STR(k.text, "Qw");
    /* the cursor */
    press(&k, AKN_BTN_L2);
    CHECK(k.cursor == 1);
    CHECK(akn_osk_insert(&k, "x") == AKN_OSK_CHANGED);
    CHECK_STR(k.text, "Qxw");
    press(&k, AKN_BTN_R2);
    press(&k, AKN_BTN_R2);
    CHECK(k.cursor == 3);
    /* the limit is in characters */
    CHECK(akn_osk_insert(&k, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") == AKN_OSK_CHANGED);
    CHECK(akn_osk_chars(&k) == 10);
    CHECK(akn_osk_insert(&k, "z") == AKN_OSK_NONE);
    /* control characters and invalid UTF-8 from a USB keyboard are ignored; Enter only when multi-line */
    akn_osk_init(&k, "ab", 40, 1);
    CHECK(k.len == 2 && !k.shift);
    akn_osk_insert(&k, "\x01\n\xC3");
    CHECK_STR(k.text, "ab");
    akn_osk_init(&k, "ab", 40, 0);
    akn_osk_insert(&k, "\n");
    CHECK_STR(k.text, "ab\n");
    /* Shift twice is Caps Lock */
    akn_osk_init(&k, "x", 40, 0);
    k.row = 4, k.col = 0;
    press(&k, AKN_BTN_CROSS);
    press(&k, AKN_BTN_CROSS);
    CHECK(k.caps);
    k.row = 1, k.col = 0;
    press(&k, AKN_BTN_CROSS);
    press(&k, AKN_BTN_CROSS);
    CHECK_STR(k.text, "xQQ");
    /* finishing */
    CHECK(press(&k, AKN_BTN_OPTIONS) == AKN_OSK_SUBMIT);
    CHECK(press(&k, AKN_BTN_CIRCLE) == AKN_OSK_CANCEL);
    k.row = 4, k.col = 9;
    CHECK(press(&k, AKN_BTN_CROSS) == AKN_OSK_SUBMIT);
    akn_input_event rel = { AKN_BTN_CROSS, AKN_EV_RELEASE, AKN_SRC_PAD };
    CHECK(akn_osk_handle(&k, &rel) == AKN_OSK_NONE);
    akn_input_event rep = { AKN_BTN_CROSS, AKN_EV_REPEAT, AKN_SRC_PAD };
    CHECK(akn_osk_handle(&k, &rep) == AKN_OSK_NONE); /* holding Cross does not type again */
    CHECK(akn_osk_backspace(&k) == AKN_OSK_CHANGED);
    akn_osk_init(&k, "", 5, 0);
    CHECK(akn_osk_backspace(&k) == AKN_OSK_NONE);
}

void run_chat_tests(void) {
    test_conversations();
    test_threads();
    test_events();
    test_sending_text();
    test_keyboard();
}
