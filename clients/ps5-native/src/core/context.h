/*
 * Which kind of process is this? A foreground application (launched by a homebrew launcher, may
 * own the screen, controller and audio) or a background payload (sent to an ELF loader, like the
 * AKENO daemon: no controller, silent audio on 12.20)? Decided from raw system answers so the
 * classification is unit-tested and every input is logged.
 */
#ifndef AKN_CONTEXT_H
#define AKN_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AKN_CTX_UNKNOWN = 0,
    AKN_CTX_FOREGROUND, /* this process is the running foreground application */
    AKN_CTX_BACKGROUND, /* another application (or none) is in the foreground */
    AKN_CTX_DESKTOP     /* PC build */
} akn_context_kind;

typedef struct {
    int have_appinfo_fn;   /* sceKernelGetAppInfo resolved */
    int appinfo_rc;        /* its result for getpid() */
    unsigned char appinfo_raw[64]; /* first bytes of its answer, for the report */
    uint32_t own_app_id;
    char own_title_id[16];
    int have_bigapp_fn;    /* sceSystemServiceGetAppIdOfRunningBigApp resolved */
    int32_t bigapp_app_id; /* its result: > 0 an app ID, otherwise none/error */
    int launcher_arg;      /* started with --launcher by homebrew.js */
} akn_context_raw;

/* Fills own_app_id / own_title_id from appinfo_raw (layout handling of game_detect.c). */
void akn_context_decode(akn_context_raw *raw);
akn_context_kind akn_context_classify(const akn_context_raw *raw);
const char *akn_context_name(akn_context_kind kind);
/* One line for the report and the System screen. */
void akn_context_describe(const akn_context_raw *raw, akn_context_kind kind, char *buf, size_t len);

/*
 * Whether the app may open the screen, controller and audio:
 *   foreground                         -> yes
 *   started by the launcher (--launcher) -> yes, whatever the check says (a disagreement is reported)
 *   anything else                      -> no: background mode (notify, report, exit)
 */
int akn_context_allows_devices(const akn_context_raw *raw, akn_context_kind kind);

#endif
