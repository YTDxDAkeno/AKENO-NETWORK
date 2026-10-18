/* Which picture a game tile shows (core/artwork.h): verified server artwork, the console's icon,
 * or the placeholder, for installed, uninstalled, unmapped and broken cases. */
#include <string.h>

#include "artwork.h"
#include "test.h"

#define COVER "0123456789abcdef0123456789abcdef"
#define ICON "fedcba9876543210fedcba9876543210"
#define BANNER "00112233445566778899aabbccddeeff"

static akn_art art_of(const char *icon, const char *cover, const char *banner) {
    akn_art a;
    memset(&a, 0, sizeof(a));
    strcpy(a.icon, icon);
    strcpy(a.cover, cover);
    strcpy(a.banner, banner);
    return a;
}

static void test_media_for_shapes(void) {
    akn_art all = art_of(ICON, COVER, BANNER), icon_only = art_of(ICON, "", ""), cover_only = art_of("", COVER, "");
    akn_art none = art_of("", "", "");
    CHECK_STR(akn_art_media_for(&all, AKN_ART_COVER), COVER);
    CHECK_STR(akn_art_media_for(&all, AKN_ART_WIDE), BANNER);
    CHECK_STR(akn_art_media_for(&all, AKN_ART_ICON), ICON);
    /* the website's fallbacks between shapes of the same verified game */
    CHECK_STR(akn_art_media_for(&icon_only, AKN_ART_COVER), ICON);
    CHECK_STR(akn_art_media_for(&icon_only, AKN_ART_WIDE), ICON);
    CHECK_STR(akn_art_media_for(&cover_only, AKN_ART_ICON), COVER);
    CHECK_STR(akn_art_media_for(&cover_only, AKN_ART_WIDE), COVER);
    /* no verified artwork (unmapped title ID) */
    CHECK_STR(akn_art_media_for(&none, AKN_ART_COVER), "");
    CHECK_STR(akn_art_media_for(NULL, AKN_ART_COVER), "");
    /* something that is not a media ID is never requested */
    akn_art bad = art_of("../../etc/passwd", "ABCDEF0123456789ABCDEF0123456789", "");
    CHECK_STR(akn_art_media_for(&bad, AKN_ART_COVER), "");
    CHECK_STR(akn_art_media_for(&bad, AKN_ART_ICON), "");
}

static void test_choose(void) {
    /* Played last week, uninstalled today: the server cover is shown; nothing local is asked for. */
    CHECK(!akn_art_want_local(COVER, AKN_PIC_LOADING, "PPSA01234"));
    CHECK(akn_art_choose(COVER, AKN_PIC_LOADING, "PPSA01234", AKN_PIC_NONE) == AKN_ART_SHOW_PLACEHOLDER);
    CHECK(!akn_art_want_local(COVER, AKN_PIC_READY, "PPSA01234"));
    CHECK(akn_art_choose(COVER, AKN_PIC_READY, "PPSA01234", AKN_PIC_NONE) == AKN_ART_SHOW_SERVER);
    /* Installed and verified: the server cover wins even when the local icon is already there. */
    CHECK(akn_art_choose(COVER, AKN_PIC_READY, "PPSA01234", AKN_PIC_READY) == AKN_ART_SHOW_SERVER);
    /* While the verified cover loads, a local icon that happens to be cached is not shown in its place. */
    CHECK(akn_art_choose(COVER, AKN_PIC_LOADING, "PPSA01234", AKN_PIC_READY) == AKN_ART_SHOW_PLACEHOLDER);
    /* Installed, not verified on the server (unmapped title ID): the console's own icon. */
    CHECK(akn_art_want_local("", AKN_PIC_NONE, "PPSA01234"));
    CHECK(akn_art_choose("", AKN_PIC_NONE, "PPSA01234", AKN_PIC_LOADING) == AKN_ART_SHOW_PLACEHOLDER);
    CHECK(akn_art_choose("", AKN_PIC_NONE, "PPSA01234", AKN_PIC_READY) == AKN_ART_SHOW_LOCAL);
    /* Unmapped and uninstalled (or the icon file is missing or corrupt): the placeholder. */
    CHECK(akn_art_choose("", AKN_PIC_NONE, "PPSA01234", AKN_PIC_MISSING) == AKN_ART_SHOW_PLACEHOLDER);
    /* The server picture failed (deleted, corrupt, refused): the same title's console icon may stand in. */
    CHECK(akn_art_want_local(COVER, AKN_PIC_MISSING, "CUSA01234"));
    CHECK(akn_art_choose(COVER, AKN_PIC_MISSING, "CUSA01234", AKN_PIC_READY) == AKN_ART_SHOW_LOCAL);
    CHECK(akn_art_choose(COVER, AKN_PIC_MISSING, "CUSA01234", AKN_PIC_MISSING) == AKN_ART_SHOW_PLACEHOLDER);
    /* ... and the server picture takes over again once a retry loads it. */
    CHECK(akn_art_choose(COVER, AKN_PIC_READY, "CUSA01234", AKN_PIC_READY) == AKN_ART_SHOW_SERVER);
    /* No title ID (a favorite game typed as text) or a malformed one: never a local lookup. */
    CHECK(!akn_art_want_local("", AKN_PIC_NONE, ""));
    CHECK(!akn_art_want_local("", AKN_PIC_NONE, NULL));
    CHECK(!akn_art_want_local("", AKN_PIC_NONE, "../PPSA0123"));
    CHECK(!akn_art_want_local("", AKN_PIC_NONE, "Elden Ring"));
    CHECK(akn_art_choose("", AKN_PIC_NONE, "Elden Ring", AKN_PIC_READY) == AKN_ART_SHOW_PLACEHOLDER);
    CHECK(akn_art_choose("", AKN_PIC_NONE, "", AKN_PIC_NONE) == AKN_ART_SHOW_PLACEHOLDER);
}

static void test_for_title(void) {
    akn_activity a;
    memset(&a, 0, sizeof(a));
    akn_art out;
    strcpy(a.recent[0].title_id, "PPSA01111");
    strcpy(a.recent[0].name, "Astro Bot");
    a.recent[0].art = art_of(ICON, COVER, "");
    strcpy(a.recent[1].title_id, "PPSA02222"); /* played, never verified: no artwork */
    strcpy(a.recent[1].name, "Astro Bot");     /* same name as a verified game: still no artwork */
    a.recent_count = 2;
    CHECK(akn_art_for_title(&a, "PPSA01111", &out) == 1);
    CHECK_STR(out.cover, COVER);
    CHECK(akn_art_for_title(&a, "PPSA02222", &out) == 0);
    CHECK_STR(out.cover, "");
    CHECK(akn_art_for_title(&a, "PPSA03333", &out) == 0);
    CHECK(akn_art_for_title(&a, "Astro Bot", &out) == 0);
    CHECK(akn_art_for_title(NULL, "PPSA01111", &out) == 0);
    /* the current game is checked first */
    strcpy(a.current.title_id, "PPSA01111");
    a.current.art = art_of(ICON, "", "");
    CHECK(akn_art_for_title(&a, "PPSA01111", &out) == 1);
    CHECK_STR(out.cover, "");
    CHECK_STR(out.icon, ICON);
}

void run_artwork_tests(void) {
    test_for_title();
    test_media_for_shapes();
    test_choose();
}
