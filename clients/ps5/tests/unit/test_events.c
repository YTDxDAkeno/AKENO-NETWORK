/* Unit tests for the polling-client helpers: the notification watermark and the formatter. */
#include <stdint.h>
#include <string.h>

#include "akeno/events.h"
#include "akeno/json.h"
#include "test.h"

static akeno_json_doc *parse(const char *text) {
    return akeno_json_parse(text, strlen(text), NULL, 0);
}

static void test_watermark(void) {
    akeno_watermark w;
    akeno_watermark_init(&w);
    /* First poll primes the baseline without surfacing anything. */
    CHECK(akeno_watermark_observe(&w, 3) == 0);
    CHECK(akeno_watermark_observe(&w, 7) == 0);
    CHECK(akeno_watermark_observe(&w, 5) == 0);
    akeno_watermark_prime(&w, w.highest);
    CHECK(w.highest == 7);
    /* Subsequent polls surface strictly newer ids, in ascending order, exactly once. */
    CHECK(akeno_watermark_observe(&w, 7) == 0);
    CHECK(akeno_watermark_observe(&w, 8) == 1);
    CHECK(akeno_watermark_observe(&w, 8) == 0);
    CHECK(akeno_watermark_observe(&w, 9) == 1);
    CHECK(akeno_watermark_observe(&w, 6) == 0);
    CHECK(w.highest == 9);

    /* A client that starts with an empty inbox still primes and then surfaces the first event. */
    akeno_watermark empty;
    akeno_watermark_init(&empty);
    akeno_watermark_prime(&empty, 0);
    CHECK(akeno_watermark_observe(&empty, 1) == 1);
}

static void describe(const char *json, char *out, size_t out_len) {
    akeno_json_doc *doc = parse(json);
    int rc = akeno_describe_notification(akeno_json_root(doc), out, out_len);
    CHECK(rc >= 0);
    akeno_json_free(doc);
}

static void test_describe(void) {
    char out[320];
    describe("{\"type\":\"friend_request.received\",\"actor\":{\"displayName\":\"Neo\"}}", out, sizeof out);
    CHECK_STR(out, "Neo sent you a friend request.");
    describe("{\"type\":\"friend_request.accepted\",\"actor\":{\"displayName\":\"Trinity\"}}", out, sizeof out);
    CHECK_STR(out, "Trinity accepted your friend request.");
    describe("{\"type\":\"invitation.received\",\"actor\":{\"displayName\":\"Mo\"},\"kind\":\"party\",\"targetName\":\"Raid\"}", out, sizeof out);
    CHECK_STR(out, "Mo invited you to the party \"Raid\".");
    describe("{\"type\":\"invitation.received\",\"actor\":{\"displayName\":\"Mo\"},\"kind\":\"game_session\",\"targetName\":\"Race\"}", out, sizeof out);
    CHECK_STR(out, "Mo invited you to the game session \"Race\".");
    describe("{\"type\":\"game_session.join_requested\",\"actor\":{\"displayName\":\"Ann\"},\"gameName\":\"Kart\"}", out, sizeof out);
    CHECK_STR(out, "Ann asked to join your Kart session.");
    describe("{\"type\":\"group.role_changed\",\"actor\":{\"displayName\":\"Sam\"},\"groupName\":\"Crew\",\"role\":\"moderator\"}", out, sizeof out);
    CHECK_STR(out, "Sam made you a moderator of \"Crew\".");
    describe("{\"type\":\"group.removed\",\"actor\":{\"displayName\":\"Sam\"},\"groupName\":\"Crew\"}", out, sizeof out);
    CHECK_STR(out, "Sam removed you from the group \"Crew\".");
    /* Unknown but well-formed type: a safe generic line, never a crash. */
    describe("{\"type\":\"something.new\",\"actor\":{\"displayName\":\"X\"}}", out, sizeof out);
    CHECK_STR(out, "You have a new AKENO NETWORK notification.");
    /* Missing actor falls back; missing type is rejected. */
    describe("{\"type\":\"friend_request.received\"}", out, sizeof out);
    CHECK_STR(out, "Someone sent you a friend request.");

    akeno_json_doc *doc = parse("{\"no\":\"type\"}");
    CHECK(akeno_describe_notification(akeno_json_root(doc), out, sizeof out) == -1);
    akeno_json_free(doc);
}

void run_events_tests(void) {
    test_watermark();
    test_describe();
}
