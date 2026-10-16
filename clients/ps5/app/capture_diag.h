/*
 * PS5 capture capability diagnostic (AKENO_CAPTURE_DIAG builds only).
 *
 * A read-only investigation aid for the direct-PS5-capture research (docs/PS5_DIRECT_CAPTURE_
 * RESEARCH.md). It reports, as evidence, which capture/record/broadcast/encoder libraries the
 * console has and which candidate symbols resolve in them. It NEVER calls a capture, encoder or
 * broadcast function, never reads another process's memory, never changes any console setting,
 * and never contacts PSN or Twitch. It is compiled into a separate experimental ELF
 * (akeno-network-ps5-capture-diagnostic.elf); the release AKENO console app contains none of it.
 *
 * Two tiers, so "even loading a library" is never done implicitly (docs §7):
 *   Tier STATIC  — no dlopen: firmware/build info, candidate library file existence (stat),
 *                  and the compiled-in capability catalogue with its sources and classification.
 *   Tier RESOLVE — only after an explicit user action: dlopen each candidate library and record
 *                  whether its candidate symbols resolve (dlsym != NULL). Nothing is called.
 *
 * The module itself is pure/portable (unit-tested on Linux): it builds the JSON and text report
 * from a caller-filled probe result, so tests inject simulated data. The platform layer supplies
 * the real probe results on a console.
 */
#ifndef AKENO_APP_CAPTURE_DIAG_H
#define AKENO_APP_CAPTURE_DIAG_H

#include <stddef.h>

#include "buf.h"

#define AKENO_CAPTURE_MAX_SYMBOLS 6
#define AKENO_CAPTURE_MAX_LIBRARIES 12

/* How strong the evidence for a candidate is. Higher is only reached with real hardware results. */
typedef enum {
    AKENO_CAPLV_UNKNOWN = 0,     /* no evidence either way */
    AKENO_CAPLV_UNSUPPORTED,     /* evidence that it is not usable from our context */
    AKENO_CAPLV_SOURCE_VERIFIED, /* the symbol/library name comes from a verified source (e.g. the pinned SDK stub) */
    AKENO_CAPLV_RUNTIME_DETECTED,/* the library file was found on the console (stat) */
    AKENO_CAPLV_SIGNATURE_VERIFIED, /* a symbol resolved on the console (dlsym), signature still unverified */
    AKENO_CAPLV_HARDWARE_TESTED  /* only ever set from a real, confirmed hardware result (never by a probe) */
} akeno_cap_level;

/* What a candidate, if usable, would give us — so a "symbol exists" is never mistaken for "capture works". */
typedef enum {
    AKENO_CAPROLE_STATUS = 0, /* status / orchestration only (no pixels) */
    AKENO_CAPROLE_OWN_OUTPUT, /* the caller's own display output (not another game's image) */
    AKENO_CAPROLE_CHECKSUM,   /* a checksum/CRC of the frame, not the pixels */
    AKENO_CAPROLE_SCREENSHOT, /* a single still frame on request */
    AKENO_CAPROLE_RECORDING,  /* access to a clip saved to storage (not live) */
    AKENO_CAPROLE_BROADCAST,  /* the console's own encoded broadcast stream */
    AKENO_CAPROLE_LIVE_FRAMES,/* continuous frames of the running game (what SharePlay needs) */
    AKENO_CAPROLE_ENCODER     /* a hardware video encoder */
} akeno_cap_role;

/* A candidate capture-related library and the symbol names to look for in it. */
typedef struct {
    const char *library;      /* e.g. "libSceVideoOut.sprx" */
    const char *symbols[AKENO_CAPTURE_MAX_SYMBOLS];
    size_t symbol_count;
    akeno_cap_role role;
    int in_pinned_sdk;        /* 1: a stub for it exists in the pinned ps5-payload-sdk */
    const char *source;       /* where the names come from */
    const char *note;         /* honest one-line caveat */
} akeno_capture_candidate;

/* Per-library probe result, filled by the platform (or a test). */
typedef struct {
    int file_present;         /* the library file exists in the system library dir (stat) */
    int resolved;             /* RESOLVE tier ran for this library */
    int load_ok;              /* dlopen succeeded (RESOLVE tier) */
    int symbol_found[AKENO_CAPTURE_MAX_SYMBOLS];
    char error[120];          /* dlopen/dlsym error, if any (never a path with secrets) */
} akeno_capture_probe;

/* System/runtime facts for the report (all read-only). */
typedef struct {
    const char *platform;     /* "PlayStation 5" or the host OS */
    const char *firmware;     /* "12.20" or "" */
    unsigned firmware_raw;
    const char *app_version;
    const char *build_id;     /* AKENO_BUILD_ID (commit + build kind) */
    const char *sdk_commit;   /* pinned ps5-payload-sdk commit */
    const char *library_dir;  /* where candidate libraries are looked for */
    int library_dir_readable;
    int process_info_available; /* sceKernelGetAppInfo present (read-only app info) */
    int resolve_ran;          /* whether the RESOLVE tier was run at all */
} akeno_capture_sysinfo;

/* Network facts for the report. No PSN/Twitch contact; destinations are AKENO's own only. */
typedef struct {
    int have;                 /* this section was filled */
    int server_configured;
    int server_reachable;
    char server_detail[160];
    int dns_readable;
    char dns_summary[160];    /* read-only, redacted summary of resolver config (never full tables) */
} akeno_capture_network;

/* The catalogue (compiled in). Returns the entries and their count. */
const akeno_capture_candidate *akeno_capture_catalogue(size_t *count);

/* The evidence-based conclusion string for a candidate given its probe (never claims capture works). */
akeno_cap_level akeno_capture_level(const akeno_capture_candidate *candidate, const akeno_capture_probe *probe);

/* Human-readable names for the JSON. */
const char *akeno_cap_level_name(akeno_cap_level level);
const char *akeno_cap_role_name(akeno_cap_role role);

/*
 * Builds the full diagnostic report as JSON into b. probes[i] corresponds to catalogue entry i
 * (probes may be NULL for a pure static report). now_iso is a timestamp string. Returns 0 on
 * success. Output is bounded; no memory dumps, no account identifiers.
 */
int akeno_capture_report_json(akeno_buf *b, const akeno_capture_sysinfo *sys, const akeno_capture_network *net,
                              const akeno_capture_probe *probes, size_t probe_count, const char *now_iso);

#endif
