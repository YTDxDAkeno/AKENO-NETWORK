/*
 * Capability policy. Detection produces facts; this module turns facts into explicit
 * enabled / disabled / unsupported decisions with an English reason. Nothing is ever enabled
 * because of a firmware version alone, and no firmware-specific offsets or patches exist in
 * this project. Native PS5 system features are reported as unsupported because no documented
 * interface for them has been identified (see docs/PS5_GAMEBASE_RESEARCH.md).
 */
#ifndef AKENO_CAPS_H
#define AKENO_CAPS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AKENO_PLATFORM_HOST = 0, AKENO_PLATFORM_PS5 } akeno_platform;

typedef struct {
    akeno_platform platform;
    const char *platform_name;   /* e.g. "Linux", "PlayStation 5" */
    const char *firmware;        /* As reported by the environment, e.g. "12.20"; NULL if unknown. */
    int has_sockets;             /* BSD sockets usable from this process */
    int has_tls;                 /* A verifying TLS backend is linked */
    int has_storage;             /* The credential directory is writable */
} akeno_platform_facts;

typedef enum {
    AKENO_FEATURE_DEVICE_PAIRING = 0,
    AKENO_FEATURE_REST_API,
    AKENO_FEATURE_REALTIME,
    AKENO_FEATURE_CREDENTIAL_STORAGE,
    AKENO_FEATURE_NATIVE_FRIENDS_LIST,
    AKENO_FEATURE_NATIVE_PRESENCE,
    AKENO_FEATURE_NATIVE_MESSAGES,
    AKENO_FEATURE_NATIVE_PARTY,
    AKENO_FEATURE_NATIVE_PARTY_VOICE,
    AKENO_FEATURE_NATIVE_GAME_INVITES,
    AKENO_FEATURE_SYSTEM_NOTIFICATIONS,
    AKENO_FEATURE_COUNT
} akeno_feature;

typedef enum {
    AKENO_CAP_ENABLED = 0,
    AKENO_CAP_DISABLED,     /* Could work, but a requirement is missing on this system. */
    AKENO_CAP_UNSUPPORTED   /* Not available through any verified interface. */
} akeno_cap_state;

typedef struct {
    akeno_cap_state state;
    const char *reason;
} akeno_cap_decision;

typedef struct {
    int known;                   /* 1 if the version is listed in the compatibility table */
    int major, minor;
    int hardware_tested;         /* 1 only after documented tests on a physical console */
    const char *note;
} akeno_firmware_info;

typedef struct {
    akeno_cap_decision features[AKENO_FEATURE_COUNT];
    akeno_firmware_info firmware;
} akeno_capability_report;

const char *akeno_feature_name(akeno_feature feature);
const char *akeno_cap_state_name(akeno_cap_state state);

/* Strictly parses "MM.mm" (e.g. "12.20"). Returns 0 on success. */
int akeno_firmware_parse(const char *text, int *major, int *minor);
void akeno_firmware_describe(const char *text, akeno_firmware_info *out);
void akeno_caps_evaluate(const akeno_platform_facts *facts, akeno_capability_report *out);

#ifdef __cplusplus
}
#endif

#endif
