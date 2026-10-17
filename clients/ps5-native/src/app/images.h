/*
 * Pictures for the native profiles: profile pictures, banners and game artwork from the AKENO
 * server through the native-IPC daemon (GET /api/media, the same media the website shows, JPEG
 * variant), and icons of games installed on this console (GET /api/local-art). Only media IDs that
 * the server itself put into a profile, friend list or activity answer are requested, so the
 * server's privacy rules decide what can be shown.
 *
 * A worker thread fetches and decodes (src/core/image.c: JPEG/PNG only, size-capped) and prepares
 * each picture at the exact size it is drawn; the main thread turns it into a texture. Memory is
 * bounded: at most AKN_IMAGES_MAX entries and AKN_IMAGES_BUDGET bytes of textures, least recently
 * drawn first out; requests that are no longer drawn are dropped before they are fetched. Nothing
 * is written to disk. A missing, refused or corrupt picture stays missing: the screen keeps its
 * initials or placeholder.
 */
#ifndef AKN_IMAGES_H
#define AKN_IMAGES_H

#include <SDL.h>
#include <stddef.h>

#include "image.h"
#include "log.h"

#define AKN_IMAGES_MAX 192
#define AKN_IMAGES_BUDGET ((size_t)48 << 20)

typedef enum { AKN_IMG_MEDIA = 0, AKN_IMG_LOCAL_ART } akn_img_source;

typedef struct akn_images akn_images;

typedef struct {
    unsigned long requested, loaded, failed, evicted, dropped;
    int resident;      /* textures now */
    size_t bytes;      /* texture bytes now */
} akn_images_stats;

/* port: the daemon's; key_path: its native.key; ready(user) says whether the native interface is
 * connected (requests wait otherwise). */
akn_images *akn_images_create(int port, const char *key_path, akn_log *log, int (*ready)(void *user), void *user);
/* Stops the worker (bounded wait); returns 1 when it finished. Frees everything when it did. */
int akn_images_destroy(akn_images *im);

/* The texture of this picture at w x h, or NULL while it loads or when it is missing. Asking is
 * what keeps a picture wanted: call it every frame the picture is on screen. id: a media ID
 * (AKN_IMG_MEDIA) or a title ID (AKN_IMG_LOCAL_ART); anything else is ignored. */
SDL_Texture *akn_images_get(akn_images *im, akn_img_source source, const char *id, int w, int h, akn_fit fit);
/* Once per frame on the main thread: uploads finished pictures, evicts over the budget. */
void akn_images_pump(akn_images *im, SDL_Renderer *renderer);
void akn_images_get_stats(akn_images *im, akn_images_stats *out);
/* Pictures asked for that are still being fetched, decoded or uploaded. */
int akn_images_pending(akn_images *im);

#endif
