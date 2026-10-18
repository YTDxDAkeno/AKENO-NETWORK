#include "badges.h"

#include <stdlib.h>
#include <string.h>

#include "vector.h"

/*
 * Line icons of the website's badge medallions: path data of the Lucide icons crown, shield-check,
 * braces, badge-check, sprout, flask-conical, gamepad-2 and ticket (lucide-react 1.56.0),
 * unmodified (the <line> elements of gamepad-2 written as path data). None of them is derived from
 * Feather, so only this licence applies:
 *
 *   ISC License
 *   Copyright (c) 2026 Lucide Icons and Contributors
 *   Permission to use, copy, modify, and/or distribute this software for any purpose with or
 *   without fee is hereby granted, provided that the above copyright notice and this permission
 *   notice appear in all copies.
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 *   SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 *   DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 *   CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
 *   OR PERFORMANCE OF THIS SOFTWARE.
 *
 * See docs/LICENSES.md. The hexagon medallion around each icon is AKENO's own artwork.
 */
static const akn_badge_def BADGES[] = {
    { "founder", "AKENO Founder", 0xE9B949,
      { "M11.562 3.266a.5.5 0 0 1 .876 0L15.39 8.87a1 1 0 0 0 1.516.294L21.183 5.5a.5.5 0 0 1 .798.519l-2.834 10.246a1 1 0 0 1-.956.734H5.81a1 1 0 0 1-.957-.734L2.02 6.02a.5.5 0 0 1 .798-.519l4.276 3.664a1 1 0 0 0 1.516-.294z",
        "M5 21h14", NULL } },
    { "administrator", "Administrator", 0xEF5B5B,
      { "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z",
        "m9 12 2 2 4-4", NULL } },
    { "developer", "Developer", 0x38BDF8,
      { "M8 3H7a2 2 0 0 0-2 2v5a2 2 0 0 1-2 2 2 2 0 0 1 2 2v5c0 1.1.9 2 2 2h1",
        "M16 21h1a2 2 0 0 0 2-2v-5c0-1.1.9-2 2-2a2 2 0 0 1-2-2V5a2 2 0 0 0-2-2h-1", NULL } },
    { "verified_contributor", "Verified Contributor", 0x34D399,
      { "M3.85 8.62a4 4 0 0 1 4.78-4.77 4 4 0 0 1 6.74 0 4 4 0 0 1 4.78 4.78 4 4 0 0 1 0 6.74 4 4 0 0 1-4.77 4.78 4 4 0 0 1-6.75 0 4 4 0 0 1-4.78-4.77 4 4 0 0 1 0-6.76Z",
        "m16 9-5.5 5.5L8 12", NULL } },
    { "early_supporter", "Early Supporter", 0xF472B6,
      { "M14 9.536V7a4 4 0 0 1 4-4h1.5a.5.5 0 0 1 .5.5V5a4 4 0 0 1-4 4 4 4 0 0 0-4 4c0 2 1 3 1 5a5 5 0 0 1-1 3",
        "M4 9a5 5 0 0 1 8 4 5 5 0 0 1-8-4", "M5 21h14", NULL } },
    { "beta_tester", "Beta Tester", 0xA78BFA,
      { "M14 2v6a2 2 0 0 0 .245.96l5.51 10.08A2 2 0 0 1 18 22H6a2 2 0 0 1-1.755-2.96l5.51-10.08A2 2 0 0 0 10 8V2",
        "M6.453 15h11.094", "M8.5 2h7", NULL } },
    { "ps5_alpha_tester", "PS5 Alpha Tester", 0x5B8CFF,
      { "M6 11L10 11", "M8 9L8 13", "M15 12L15.01 12", "M18 10L18.01 10",
        "M17.32 5H6.68a4 4 0 0 0-3.978 3.59c-.006.052-.01.101-.017.152C2.604 9.416 2 14.456 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.414-1.414A2 2 0 0 1 9.828 16h4.344a2 2 0 0 1 1.414.586L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.545-.604-6.584-.685-7.258-.007-.05-.011-.1-.017-.151A4 4 0 0 0 17.32 5z",
        NULL } },
    { "event_participant", "Community Event Participant", 0xFB923C,
      { "M2 9a3 3 0 0 1 0 6v2a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-2a3 3 0 0 1 0-6V7a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2Z", "M13 5v2", "M13 17v2",
        "M13 11v2", NULL } },
};

#define NBADGES ((int)(sizeof(BADGES) / sizeof(BADGES[0])))

const akn_badge_def *akn_badge_find(const char *id) {
    for (int i = 0; id && i < NBADGES; i++)
        if (strcmp(BADGES[i].id, id) == 0) return &BADGES[i];
    return NULL;
}

int akn_badge_count(void) { return NBADGES; }
const akn_badge_def *akn_badge_at(int i) { return i >= 0 && i < NBADGES ? &BADGES[i] : NULL; }

/* The medallion, as clients/ps5/app/web/app.js badgeMedal() draws it in a 32x32 view box. */
int akn_badge_render(const char *id, uint8_t *rgba, int size) {
    if (!rgba || size < 8 || size > 512) return -1;
    memset(rgba, 0, (size_t)size * size * 4);
    const akn_badge_def *def = akn_badge_find(id);
    akn_path *p = malloc(sizeof(*p));
    if (!p) return -1;
    float scale = size / 32.0f;
    static const char HEX[] = "M16 1.8 28.3 8.9v14.2L16 30.2 3.7 23.1V8.9Z";
    memset(p, 0, sizeof(*p));
    akn_path_parse(HEX, p);
    akn_raster_fill(rgba, size, p, scale, 0xFF000000u | (def ? def->color : 0x5B6785u));
    memset(p, 0, sizeof(*p));
    akn_path_parse("M16 1.8 28.3 8.9 16 16Z", p);
    akn_raster_fill(rgba, size, p, scale, 0x24FFFFFFu); /* white at 14% */
    memset(p, 0, sizeof(*p));
    akn_path_parse(HEX, p);
    akn_raster_stroke(rgba, size, p, scale, 1.2f, 0x59FFFFFFu); /* white at 35% */
    if (def) {
        memset(p, 0, sizeof(*p));
        for (int i = 0; i < 6 && def->icon[i]; i++) akn_path_parse(def->icon[i], p);
        akn_path_transform(p, 0.6f, 8.8f, 8.8f); /* translate(8.8 8.8) scale(0.6) */
        akn_raster_stroke(rgba, size, p, scale, 2.6f * 0.6f, 0xFF0B0F1Au);
    }
    free(p);
    return 0;
}
