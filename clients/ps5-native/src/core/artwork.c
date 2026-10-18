#include "artwork.h"

#include <string.h>

#include "image.h"

const char *akn_art_media_for(const akn_art *art, akn_art_shape shape) {
    if (!art) return "";
    const char *m;
    switch (shape) {
    case AKN_ART_COVER: m = art->cover[0] ? art->cover : art->icon; break;
    case AKN_ART_WIDE: m = art->banner[0] ? art->banner : art->cover[0] ? art->cover : art->icon; break;
    default: m = art->icon[0] ? art->icon : art->cover; break;
    }
    return akn_is_media_id(m) ? m : "";
}

int akn_art_want_local(const char *media_id, akn_pic_state server, const char *title_id) {
    if (!title_id || !akn_is_title_id(title_id)) return 0;
    if (!media_id || !media_id[0]) return 1;
    return server == AKN_PIC_MISSING;
}

akn_art_choice akn_art_choose(const char *media_id, akn_pic_state server, const char *title_id, akn_pic_state local) {
    if (media_id && media_id[0] && server == AKN_PIC_READY) return AKN_ART_SHOW_SERVER;
    if (akn_art_want_local(media_id, server, title_id) && local == AKN_PIC_READY) return AKN_ART_SHOW_LOCAL;
    return AKN_ART_SHOW_PLACEHOLDER;
}

static int has_art(const akn_art *a) { return a->icon[0] || a->cover[0] || a->banner[0]; }

int akn_art_for_title(const akn_activity *activity, const char *title_id, akn_art *out) {
    memset(out, 0, sizeof(*out));
    if (!activity || !title_id || !akn_is_title_id(title_id)) return 0;
    if (strcmp(activity->current.title_id, title_id) == 0 && has_art(&activity->current.art)) {
        *out = activity->current.art;
        return 1;
    }
    for (int i = 0; i < activity->recent_count && i < AKN_RECENT_GAMES; i++)
        if (strcmp(activity->recent[i].title_id, title_id) == 0 && has_art(&activity->recent[i].art)) {
            *out = activity->recent[i].art;
            return 1;
        }
    return 0;
}
