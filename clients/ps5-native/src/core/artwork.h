/*
 * Which picture a game tile shows (docs/GAME_ARTWORK.md, "AKENO Native"). One rule for every
 * screen that shows a game: profiles, Recently Played, Favorite Games, Activity, friends playing
 * now, the friend list, party members and history.
 *
 *   1. The verified artwork the AKENO server attached to the game (by its administrator-verified
 *      title-ID mapping). It does not depend on the game being installed: an uninstalled game
 *      keeps its cover as long as the server has one.
 *   2. While that picture loads, the placeholder: the local icon is NOT fetched in parallel, so a
 *      console icon never shows up in place of a verified cover that is about to arrive.
 *   3. The icon this console keeps for an installed title (read-only, through the daemon), only
 *      when the server has no artwork for the game or its picture could not be loaded. The local
 *      icon belongs to the same title ID: never a picture found by name.
 *   4. The placeholder with the game's initials.
 *
 * Pictures are matched by media ID (server) or title ID (console) only; names are never used to
 * find a picture here (the server matches favorite games by their verified name or alias).
 * Portable and unit-tested.
 */
#ifndef AKN_ARTWORK_H
#define AKN_ARTWORK_H

#include "social.h"

typedef enum { AKN_ART_ICON = 0, AKN_ART_COVER, AKN_ART_WIDE } akn_art_shape;

/* State of one picture in the image loader. */
typedef enum {
    AKN_PIC_NONE = 0, /* not asked for (no ID) */
    AKN_PIC_LOADING,  /* queued, fetching or decoding */
    AKN_PIC_READY,    /* a texture is there */
    AKN_PIC_MISSING   /* refused, not found, corrupt or failed for now */
} akn_pic_state;

typedef enum {
    AKN_ART_SHOW_PLACEHOLDER = 0,
    AKN_ART_SHOW_SERVER,
    AKN_ART_SHOW_LOCAL
} akn_art_choice;

/* The server media ID for a frame of this shape, or "" when the game has no verified artwork.
 * A cover frame takes the cover, else the icon; a wide frame the banner, else the cover, else the
 * icon; an icon frame the icon, else the cover (the website's fallbacks). */
const char *akn_art_media_for(const akn_art *art, akn_art_shape shape);

/* 1 when the console's own icon may be asked for: there is a title ID and the server has no
 * picture for this frame, or its picture is missing. */
int akn_art_want_local(const char *media_id, akn_pic_state server, const char *title_id);

/* The verified artwork the server gave this exact title ID in an activity answer (the current game
 * or a recently played one), for a game that is known only by its title ID (the daemon's detection
 * on this console). Matches the title ID exactly, never a name. 1 when found. */
int akn_art_for_title(const akn_activity *activity, const char *title_id, akn_art *out);

/* What to draw now, from the states of both pictures. */
akn_art_choice akn_art_choose(const char *media_id, akn_pic_state server, const char *title_id, akn_pic_state local);

#endif
