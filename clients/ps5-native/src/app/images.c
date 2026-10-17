#include "images.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "akeno/json.h"
#include "ipc.h"

#define REPLY_MAX (1024 * 1024)     /* base64 of the daemon's 512 KiB media limit, plus JSON */
#define FETCH_TIMEOUT_MS 15000
#define STALE_FRAMES 90             /* a queued picture not drawn for this long is dropped */
#define RETRY_MS 15000              /* after a transient failure (daemon busy, new key) */

typedef enum { E_FREE = 0, E_QUEUED, E_LOADING, E_DECODED, E_READY, E_FAILED } entry_state;

typedef struct {
    entry_state state;
    akn_img_source source;
    char id[33];
    int w, h;
    akn_fit fit;
    akn_image pixels;     /* E_DECODED */
    SDL_Texture *tex;     /* E_READY */
    size_t bytes;
    unsigned long last_frame;
    uint64_t retry_at;    /* E_FAILED: 0 = permanent */
} entry;

struct akn_images {
    entry e[AKN_IMAGES_MAX];
    SDL_mutex *lock;
    SDL_cond *cond;
    SDL_Thread *thread;
    int stop, done;
    unsigned long frame;
    akn_images_stats stats;
    int port;
    char key_path[320];
    char key[AKN_NATIVE_KEY_LEN + 1];
    akn_log *log;
    int (*ready)(void *user);
    void *user;
};

static uint64_t now_ms(void) { return SDL_GetTicks64(); }

static int valid_id(akn_img_source source, const char *id) {
    return source == AKN_IMG_MEDIA ? akn_is_media_id(id) : source == AKN_IMG_LOCAL_ART ? akn_is_title_id(id) : 0;
}

/* ---- Worker -------------------------------------------------------------------------------------- */

/* Fetches and decodes one picture. 0 ok; 1 permanent failure; 2 transient (try again later). */
static int fetch_one(akn_images *im, const entry *req, akn_image *out) {
    if (!im->key[0] && akn_read_native_key(im->key_path, im->key, sizeof(im->key)) != 0) return 2;
    char path[96];
    if (req->source == AKN_IMG_MEDIA) snprintf(path, sizeof(path), "/api/media?id=%s&v=fallback", req->id);
    else snprintf(path, sizeof(path), "/api/local-art?title=%s", req->id);
    akn_http_reply reply;
    akn_loopback_get(im->port, path, im->key, FETCH_TIMEOUT_MS, REPLY_MAX, &reply);
    int rc = 1;
    char why[128] = "";
    if (reply.transport_error) {
        rc = 2;
    } else if (reply.status == 403 && reply.body && strstr(reply.body, "NATIVE_KEY_INVALID")) {
        im->key[0] = '\0'; /* the daemon restarted: read its new key next time */
        rc = 2;
    } else if (reply.status == 404 || reply.status == 400) {
        snprintf(why, sizeof(why), "not available (HTTP %d)", reply.status);
    } else if (reply.status != 200 || !reply.body) {
        snprintf(why, sizeof(why), "HTTP %d", reply.status);
        rc = reply.status >= 500 ? 2 : 1;
    } else {
        akeno_json_doc *doc = akeno_json_parse(reply.body, reply.len, NULL, 0);
        const akeno_json *data = akeno_json_get(akeno_json_root(doc), "data");
        uint8_t *raw = malloc(AKN_IMAGE_MAX_BYTES);
        long n = data && data->type == AKENO_JSON_STRING && raw ? akn_base64_decode(data->string, strlen(data->string), raw, AKN_IMAGE_MAX_BYTES) : -1;
        akeno_json_free(doc);
        akn_image decoded;
        char err[96];
        if (n <= 0) snprintf(why, sizeof(why), "unreadable answer");
        else if (akn_image_decode(raw, (size_t)n, &decoded, err, sizeof(err)) != 0) snprintf(why, sizeof(why), "decode: %s", err);
        else {
            if (akn_image_fit(&decoded, req->w, req->h, req->fit, out) == 0) rc = 0;
            else snprintf(why, sizeof(why), "could not be prepared");
            akn_image_free(&decoded);
        }
        free(raw);
    }
    akn_http_reply_free(&reply);
    /* Never the ID: only what kind of picture failed and why. */
    if (rc == 1 && im->log)
        akn_logf(im->log, AKN_LOG_WARN, "images.failed", "%s: %s", req->source == AKN_IMG_MEDIA ? "picture" : "console game icon", why);
    return rc;
}

static int worker(void *arg) {
    akn_images *im = arg;
    SDL_LockMutex(im->lock);
    while (!im->stop) {
        int pick = -1;
        /* ready() takes the app's lock, which the main thread holds while it asks for pictures:
         * never call it with ours held. */
        SDL_UnlockMutex(im->lock);
        int ready = !im->ready || im->ready(im->user);
        SDL_LockMutex(im->lock);
        if (im->stop) break;
        if (ready) {
            /* the most recently drawn queued picture first */
            for (int i = 0; i < AKN_IMAGES_MAX; i++)
                if (im->e[i].state == E_QUEUED && (pick < 0 || im->e[i].last_frame > im->e[pick].last_frame)) pick = i;
        }
        if (pick < 0) {
            SDL_CondWaitTimeout(im->cond, im->lock, 200);
            continue;
        }
        entry *e = &im->e[pick];
        e->state = E_LOADING;
        entry req = *e;
        SDL_UnlockMutex(im->lock);
        akn_image out;
        memset(&out, 0, sizeof(out));
        int rc = fetch_one(im, &req, &out);
        SDL_LockMutex(im->lock);
        if (e->state != E_LOADING) { /* dropped meanwhile */
            akn_image_free(&out);
            continue;
        }
        if (rc == 0) {
            e->pixels = out;
            e->state = E_DECODED;
        } else {
            e->state = E_FAILED;
            e->retry_at = rc == 2 ? now_ms() + RETRY_MS : 0;
            if (rc == 1) im->stats.failed++;
        }
    }
    im->done = 1;
    SDL_UnlockMutex(im->lock);
    return 0;
}

akn_images *akn_images_create(int port, const char *key_path, akn_log *log, int (*ready)(void *user), void *user) {
    akn_images *im = calloc(1, sizeof(*im));
    if (!im) return NULL;
    im->port = port;
    snprintf(im->key_path, sizeof(im->key_path), "%s", key_path);
    im->log = log;
    im->ready = ready;
    im->user = user;
    im->lock = SDL_CreateMutex();
    im->cond = SDL_CreateCond();
    im->thread = im->lock && im->cond ? SDL_CreateThread(worker, "akeno-images", im) : NULL;
    if (!im->thread) {
        if (im->cond) SDL_DestroyCond(im->cond);
        if (im->lock) SDL_DestroyMutex(im->lock);
        free(im);
        return NULL;
    }
    return im;
}

int akn_images_destroy(akn_images *im) {
    if (!im) return 1;
    SDL_LockMutex(im->lock);
    im->stop = 1;
    SDL_CondSignal(im->cond);
    SDL_UnlockMutex(im->lock);
    for (int i = 0; i < 50; i++) {
        SDL_LockMutex(im->lock);
        int done = im->done;
        SDL_UnlockMutex(im->lock);
        if (done) {
            SDL_WaitThread(im->thread, NULL);
            for (int k = 0; k < AKN_IMAGES_MAX; k++) {
                akn_image_free(&im->e[k].pixels);
                if (im->e[k].tex) SDL_DestroyTexture(im->e[k].tex);
            }
            memset(im->key, 0, sizeof(im->key));
            SDL_DestroyCond(im->cond);
            SDL_DestroyMutex(im->lock);
            free(im);
            return 1;
        }
        SDL_Delay(10);
    }
    SDL_DetachThread(im->thread); /* a fetch in flight ends within its timeout; the process ends first */
    return 0;
}

/* ---- Main thread --------------------------------------------------------------------------------- */

static void release(akn_images *im, entry *e) {
    akn_image_free(&e->pixels);
    if (e->tex) SDL_DestroyTexture(e->tex);
    if (e->state == E_READY) {
        im->stats.resident--;
        im->stats.bytes -= e->bytes;
    }
    memset(e, 0, sizeof(*e));
}

SDL_Texture *akn_images_get(akn_images *im, akn_img_source source, const char *id, int w, int h, akn_fit fit) {
    if (!im || !valid_id(source, id) || w <= 0 || h <= 0 || w > 1920 || h > 1080) return NULL;
    SDL_LockMutex(im->lock);
    int free_slot = -1, oldest = -1;
    SDL_Texture *tex = NULL;
    for (int i = 0; i < AKN_IMAGES_MAX; i++) {
        entry *e = &im->e[i];
        if (e->state == E_FREE) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (e->source == source && e->w == w && e->h == h && e->fit == fit && strcmp(e->id, id) == 0) {
            e->last_frame = im->frame;
            if (e->state == E_READY) tex = e->tex;
            else if (e->state == E_FAILED && e->retry_at && now_ms() >= e->retry_at) {
                e->state = E_QUEUED;
                SDL_CondSignal(im->cond);
            }
            SDL_UnlockMutex(im->lock);
            return tex;
        }
        /* a slot to reuse: something not drawn this frame and not being fetched */
        if (e->state != E_LOADING && e->last_frame != im->frame && (oldest < 0 || e->last_frame < im->e[oldest].last_frame)) oldest = i;
    }
    int slot = free_slot >= 0 ? free_slot : oldest;
    if (slot >= 0) {
        entry *e = &im->e[slot];
        if (e->state != E_FREE) {
            im->stats.evicted++;
            release(im, e);
        }
        e->state = E_QUEUED;
        e->source = source;
        snprintf(e->id, sizeof(e->id), "%s", id);
        e->w = w;
        e->h = h;
        e->fit = fit;
        e->last_frame = im->frame;
        im->stats.requested++;
        SDL_CondSignal(im->cond);
    }
    SDL_UnlockMutex(im->lock);
    return NULL;
}

void akn_images_pump(akn_images *im, SDL_Renderer *renderer) {
    if (!im) return;
    SDL_LockMutex(im->lock);
    im->frame++;
    for (int i = 0; i < AKN_IMAGES_MAX; i++) {
        entry *e = &im->e[i];
        if (e->state == E_QUEUED && im->frame - e->last_frame > STALE_FRAMES) { /* no longer on screen */
            memset(e, 0, sizeof(*e));
            im->stats.dropped++;
            continue;
        }
        if (e->state != E_DECODED) continue;
        SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, e->pixels.w, e->pixels.h);
        if (t && SDL_UpdateTexture(t, NULL, e->pixels.rgba, e->pixels.w * 4) == 0) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            e->tex = t;
            e->bytes = (size_t)e->pixels.w * e->pixels.h * 4;
            e->state = E_READY;
            im->stats.loaded++;
            im->stats.resident++;
            im->stats.bytes += e->bytes;
        } else {
            if (t) SDL_DestroyTexture(t);
            e->state = E_FAILED;
            e->retry_at = 0;
            im->stats.failed++;
        }
        akn_image_free(&e->pixels);
    }
    /* Over the budget: least recently drawn textures first, never one drawn this frame. */
    while (im->stats.bytes > AKN_IMAGES_BUDGET) {
        int victim = -1;
        for (int i = 0; i < AKN_IMAGES_MAX; i++)
            if (im->e[i].state == E_READY && im->e[i].last_frame + 1 < im->frame &&
                (victim < 0 || im->e[i].last_frame < im->e[victim].last_frame))
                victim = i;
        if (victim < 0) break;
        im->stats.evicted++;
        release(im, &im->e[victim]);
    }
    SDL_UnlockMutex(im->lock);
}

void akn_images_get_stats(akn_images *im, akn_images_stats *out) {
    memset(out, 0, sizeof(*out));
    if (!im) return;
    SDL_LockMutex(im->lock);
    *out = im->stats;
    SDL_UnlockMutex(im->lock);
}

int akn_images_pending(akn_images *im) {
    if (!im) return 0;
    int n = 0;
    SDL_LockMutex(im->lock);
    for (int i = 0; i < AKN_IMAGES_MAX; i++)
        if (im->e[i].state == E_QUEUED || im->e[i].state == E_LOADING || im->e[i].state == E_DECODED) n++;
    SDL_UnlockMutex(im->lock);
    return n;
}
