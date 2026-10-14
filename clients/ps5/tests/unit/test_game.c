/* Unit tests for game detection: classification, debouncing and title names from metadata. */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game_detect.h"
#include "test.h"

static akeno_proc_info proc(int pid, const char *title, akeno_proc_state state) {
    akeno_proc_info p;
    memset(&p, 0, sizeof(p));
    p.pid = pid;
    p.state = state;
    p.has_app_info = title != NULL;
    snprintf(p.title_id, sizeof(p.title_id), "%s", title ? title : "");
    return p;
}

/* A process whose title ID sits at the offset of samples/test_privileges (16). */
static akeno_proc_info proc_alt(int pid, const char *title, akeno_proc_state state) {
    akeno_proc_info p = proc(pid, NULL, state);
    p.has_app_info = 1;
    snprintf(p.title_id_alt, sizeof(p.title_id_alt), "%s", title);
    return p;
}

/* Raw application info as the system would write it, title at `offset`. */
static void raw_info(unsigned char *raw, size_t len, unsigned int app_id, size_t offset, const char *title) {
    memset(raw, 0, len);
    raw[0] = (unsigned char)(app_id & 0xFF);
    raw[1] = (unsigned char)((app_id >> 8) & 0xFF);
    raw[2] = (unsigned char)((app_id >> 16) & 0xFF);
    raw[3] = (unsigned char)((app_id >> 24) & 0xFF);
    if (title) memcpy(raw + offset, title, strlen(title));
}

static void test_appinfo_parse(void) {
    unsigned char raw[128];
    akeno_proc_info p;

    /* samples/ps layout: app_type (e.g. 1) at 16, title ID at 20. */
    raw_info(raw, sizeof(raw), 0x60000123, AKENO_APPINFO_OFFSET_TITLE_PS, "PPSA01234");
    raw[16] = 1;
    memset(&p, 0, sizeof(p));
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK(p.app_id == 0x60000123);
    CHECK_STR(p.title_id, "PPSA01234");
    CHECK_STR(p.title_id_alt, ""); /* Bytes 16..24 are app_type and "PPSA0": not a title ID. */
    CHECK(p.raw_head[0] == 0x23 && p.raw_head[20] == 'P');

    /* samples/test_privileges layout: title ID at 16; offset 20 then starts with digits. */
    raw_info(raw, sizeof(raw), 7, AKENO_APPINFO_OFFSET_TITLE_ALT, "CUSA00001");
    memset(&p, 0, sizeof(p));
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK_STR(p.title_id, "");
    CHECK_STR(p.title_id_alt, "CUSA00001");

    /* Not NUL-terminated after nine characters, lower case, or too short: rejected. */
    raw_info(raw, sizeof(raw), 1, AKENO_APPINFO_OFFSET_TITLE_PS, "PPSA012345");
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK_STR(p.title_id, "");
    raw_info(raw, sizeof(raw), 1, AKENO_APPINFO_OFFSET_TITLE_PS, "ppsa01234");
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK_STR(p.title_id, "");
    raw_info(raw, sizeof(raw), 1, AKENO_APPINFO_OFFSET_TITLE_PS, "PPSA0123");
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK_STR(p.title_id, "");

    /* A system application is shaped like a title ID (it votes for the layout) but is no game. */
    raw_info(raw, sizeof(raw), 1, AKENO_APPINFO_OFFSET_TITLE_PS, "NPXS40028");
    akeno_appinfo_parse(raw, sizeof(raw), &p);
    CHECK_STR(p.title_id, "NPXS40028");
    CHECK(akeno_looks_like_title_id(p.title_id) && !akeno_is_game_title_id(p.title_id));

    /* A short answer never reads past its length. */
    raw_info(raw, sizeof(raw), 1, AKENO_APPINFO_OFFSET_TITLE_PS, "PPSA01234");
    akeno_appinfo_parse(raw, 25, &p);
    CHECK_STR(p.title_id, "");
    akeno_appinfo_parse(raw, 2, &p);
    CHECK(p.app_id == 0);
}

static void test_layout_vote(void) {
    akeno_game_scan scan;

    /* Every process agrees on offset 16: the alternative layout is used. */
    akeno_proc_info alt[] = { proc_alt(10, "NPXS40028", AKENO_PROC_ACTIVE), proc_alt(11, "PPSA04321", AKENO_PROC_ACTIVE) };
    akeno_game_classify(alt, 2, 99, &scan);
    CHECK(scan.layout == AKENO_LAYOUT_ALT);
    CHECK(scan.layout_votes_alt == 2 && scan.layout_votes_ps == 0);
    CHECK(scan.kind == AKENO_SCAN_GAME);
    CHECK_STR(scan.title_id, "PPSA04321");

    /* Offset 20 everywhere (samples/ps). */
    akeno_proc_info ps[] = { proc(10, "NPXS40028", AKENO_PROC_ACTIVE), proc(11, "CUSA00042", AKENO_PROC_ACTIVE) };
    akeno_game_classify(ps, 2, 99, &scan);
    CHECK(scan.layout == AKENO_LAYOUT_PS && scan.kind == AKENO_SCAN_GAME);
    CHECK_STR(scan.title_id, "CUSA00042");
    CHECK(scan.probe_count == 2);
    CHECK(strncmp(scan.probes[0], "10 ", 3) == 0);

    /* Title IDs at both offsets (in different processes): the layout is not trusted, nothing is reported. */
    akeno_proc_info mixed[] = { proc(10, "NPXS40028", AKENO_PROC_ACTIVE), proc_alt(11, "PPSA04321", AKENO_PROC_ACTIVE) };
    akeno_game_classify(mixed, 2, 99, &scan);
    CHECK(scan.layout == AKENO_LAYOUT_CONFLICT);
    CHECK(scan.kind == AKENO_SCAN_UNVERIFIED);
    CHECK_STR(scan.title_id, "");
    CHECK(scan.candidates == 0 && scan.error[0]);
    CHECK_STR(akeno_scan_kind_name(scan.kind), "unverified-layout");

    /* No title IDs anywhere: unknown layout, no game. */
    akeno_proc_info none[] = { proc(10, NULL, AKENO_PROC_ACTIVE), proc(11, "", AKENO_PROC_ACTIVE) };
    akeno_game_classify(none, 2, 99, &scan);
    CHECK(scan.layout == AKENO_LAYOUT_UNKNOWN && scan.kind == AKENO_SCAN_NONE);

    /* The process list can be empty. */
    akeno_game_classify(NULL, 0, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_NONE && scan.processes == 0);

    /* An unverified scan ends a confirmed game like a failed one (after debouncing). */
    akeno_game_debounce d;
    memset(&d, 0, sizeof(d));
    snprintf(d.current, sizeof(d.current), "PPSA04321");
    akeno_game_classify(mixed, 2, 99, &scan);
    CHECK(akeno_game_debounce_update(&d, &scan, 2) == 0);
    CHECK(akeno_game_debounce_update(&d, &scan, 2) == 1);
    CHECK_STR(d.current, "");
}

static void test_title_ids(void) {
    CHECK(akeno_is_game_title_id("PPSA01234"));
    CHECK(akeno_is_game_title_id("CUSA00001"));
    CHECK(!akeno_is_game_title_id("NPXS40028")); /* System applications never count. */
    CHECK(!akeno_is_game_title_id("PPSA0123"));
    CHECK(!akeno_is_game_title_id("PPSA012345"));
    CHECK(!akeno_is_game_title_id("ppsa01234"));
    CHECK(!akeno_is_game_title_id("PPSA0123X"));
    CHECK(!akeno_is_game_title_id("../../etc"));
    CHECK(!akeno_is_game_title_id(""));
    CHECK(!akeno_is_game_title_id(NULL));
    CHECK(akeno_looks_like_title_id("NPXS40028"));
    CHECK(!akeno_looks_like_title_id("NPXS4002"));
    CHECK(!akeno_looks_like_title_id(NULL));
}

static void test_classify(void) {
    akeno_game_scan scan;
    akeno_proc_info none[] = { proc(1, NULL, AKENO_PROC_ACTIVE), proc(2, "NPXS40028", AKENO_PROC_ACTIVE), proc(3, "", AKENO_PROC_ACTIVE) };
    akeno_game_classify(none, 3, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_NONE);
    CHECK(scan.processes == 3 && scan.app_processes == 1 && scan.candidates == 0);

    /* One game with several processes, next to system apps. */
    akeno_proc_info one[] = { proc(10, "NPXS40028", AKENO_PROC_ACTIVE), proc(11, "PPSA01234", AKENO_PROC_ACTIVE),
                              proc(12, "PPSA01234", AKENO_PROC_ACTIVE), proc(13, NULL, AKENO_PROC_ACTIVE) };
    akeno_game_classify(one, 4, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_GAME);
    CHECK_STR(scan.title_id, "PPSA01234");
    CHECK(scan.candidates == 1);

    /* A stopped (suspended) game is not activity. */
    akeno_proc_info stopped[] = { proc(20, "CUSA00001", AKENO_PROC_STOPPED) };
    akeno_game_classify(stopped, 1, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_NONE && scan.stopped_games == 1);

    /* Two different games: ambiguous, nothing reported. */
    akeno_proc_info two[] = { proc(30, "PPSA01234", AKENO_PROC_ACTIVE), proc(31, "CUSA00001", AKENO_PROC_ACTIVE) };
    akeno_game_classify(two, 2, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_AMBIGUOUS && scan.candidates == 2);
    CHECK_STR(scan.title_id, "");

    /* This app itself is ignored, whatever it reports; info-less processes never count. */
    akeno_proc_info self[] = { proc(99, "PPSA09999", AKENO_PROC_ACTIVE), proc(40, NULL, AKENO_PROC_ACTIVE) };
    self[1].has_app_info = 0;
    snprintf(self[1].title_id, sizeof(self[1].title_id), "PPSA01234");
    akeno_game_classify(self, 2, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_NONE);

    /* Many candidates do not overflow the candidate list. */
    akeno_proc_info many[6];
    for (int i = 0; i < 6; i++) {
        char id[16];
        snprintf(id, sizeof(id), "PPSA0000%d", i);
        many[i] = proc(50 + i, id, AKENO_PROC_ACTIVE);
    }
    akeno_game_classify(many, 6, 99, &scan);
    CHECK(scan.kind == AKENO_SCAN_AMBIGUOUS && scan.candidates == 6);
}

static akeno_game_scan scan_of(akeno_scan_kind kind, const char *title) {
    akeno_game_scan scan;
    memset(&scan, 0, sizeof(scan));
    scan.kind = kind;
    snprintf(scan.title_id, sizeof(scan.title_id), "%s", title ? title : "");
    return scan;
}

static void test_debounce(void) {
    akeno_game_debounce d;
    memset(&d, 0, sizeof(d));
    akeno_game_scan a = scan_of(AKENO_SCAN_GAME, "PPSA01234");
    akeno_game_scan b = scan_of(AKENO_SCAN_GAME, "CUSA00001");
    akeno_game_scan none = scan_of(AKENO_SCAN_NONE, NULL);
    akeno_game_scan failed = scan_of(AKENO_SCAN_FAILED, NULL);
    akeno_game_scan ambiguous = scan_of(AKENO_SCAN_AMBIGUOUS, NULL);

    CHECK(akeno_game_debounce_update(&d, &a, 2) == 0); /* Seen once: not yet. */
    CHECK_STR(d.current, "");
    CHECK(akeno_game_debounce_update(&d, &a, 2) == 1);
    CHECK_STR(d.current, "PPSA01234");
    CHECK(akeno_game_debounce_update(&d, &a, 2) == 0);

    /* A single glitch does not end the session. */
    CHECK(akeno_game_debounce_update(&d, &failed, 2) == 0);
    CHECK(akeno_game_debounce_update(&d, &a, 2) == 0);
    CHECK_STR(d.current, "PPSA01234");

    /* Switching games needs two scans of the new game. */
    CHECK(akeno_game_debounce_update(&d, &b, 2) == 0);
    CHECK(akeno_game_debounce_update(&d, &a, 2) == 0); /* Flapping back resets. */
    CHECK(akeno_game_debounce_update(&d, &b, 2) == 0);
    CHECK(akeno_game_debounce_update(&d, &b, 2) == 1);
    CHECK_STR(d.current, "CUSA00001");

    /* Ambiguity or failures end the activity after two scans: no claim without evidence. */
    CHECK(akeno_game_debounce_update(&d, &ambiguous, 2) == 0);
    CHECK(akeno_game_debounce_update(&d, &failed, 2) == 1);
    CHECK_STR(d.current, "");
    CHECK(akeno_game_debounce_update(&d, &none, 2) == 0);

    /* confirm = 1 reacts immediately. */
    CHECK(akeno_game_debounce_update(&d, &a, 1) == 1);
    CHECK_STR(d.current, "PPSA01234");
}

static void test_clean_names(void) {
    char name[160];
    snprintf(name, sizeof(name), "  Astro\tBot\x07  ");
    CHECK(akeno_clean_title_name(name, 128) == 0);
    CHECK_STR(name, "Astro Bot");
    snprintf(name, sizeof(name), "Caf\xC3\xA9 Racer\xE2\x84\xA2");
    CHECK(akeno_clean_title_name(name, 128) == 0);
    CHECK_STR(name, "Caf\xC3\xA9 Racer\xE2\x84\xA2");
    /* Truncation never splits a character: "é" is 2 bytes and would straddle byte 4. */
    snprintf(name, sizeof(name), "Caf\xC3\xA9");
    CHECK(akeno_clean_title_name(name, 4) == 0);
    CHECK_STR(name, "Caf");
    snprintf(name, sizeof(name), "Bad \xC3\x28 bytes");
    CHECK(akeno_clean_title_name(name, 128) == -1);
    snprintf(name, sizeof(name), "\xED\xA0\x80"); /* Encoded surrogate. */
    CHECK(akeno_clean_title_name(name, 128) == -1);
    snprintf(name, sizeof(name), "   ");
    CHECK(akeno_clean_title_name(name, 128) == -1);
    char long_name[400];
    memset(long_name, 'x', 300);
    long_name[300] = '\0';
    CHECK(akeno_clean_title_name(long_name, 128) == 0 && strlen(long_name) == 128);
}

static void test_param_json(void) {
    char out[AKENO_TITLE_NAME_MAX];
    const char *ps5 = "{\"titleId\":\"PPSA01234\",\"localizedParameters\":{\"defaultLanguage\":\"de-DE\","
                      "\"en-US\":{\"titleName\":\"Racing Game\"},\"de-DE\":{\"titleName\":\"Rennspiel\"}}}";
    CHECK(akeno_param_json_title(ps5, strlen(ps5), "PPSA01234", out, sizeof(out)) == 0);
    CHECK_STR(out, "Rennspiel");
    const char *no_default = "{\"localizedParameters\":{\"ja-JP\":{\"titleName\":\"JP\"},\"en-US\":{\"titleName\":\"Racing Game\"}}}";
    CHECK(akeno_param_json_title(no_default, strlen(no_default), "PPSA01234", out, sizeof(out)) == 0);
    CHECK_STR(out, "Racing Game");
    const char *only_other = "{\"localizedParameters\":{\"defaultLanguage\":\"fr-FR\",\"ja-JP\":{\"titleName\":\"JP name\"}}}";
    CHECK(akeno_param_json_title(only_other, strlen(only_other), "PPSA01234", out, sizeof(out)) == 0);
    CHECK_STR(out, "JP name");
    /* A file for a different title is never used. */
    CHECK(akeno_param_json_title(ps5, strlen(ps5), "PPSA09999", out, sizeof(out)) == -1);
    CHECK(akeno_param_json_title("{\"localizedParameters\":{}}", 26, "PPSA01234", out, sizeof(out)) == -1);
    CHECK(akeno_param_json_title("not json", 8, "PPSA01234", out, sizeof(out)) == -1);
    CHECK_STR(out, "");
}

/* Builds a minimal PS4-format param.sfo with TITLE and TITLE_ID entries. */
static size_t make_sfo(unsigned char *buf, size_t cap, const char *title, const char *title_id) {
    memset(buf, 0, cap);
    const char keys[] = "TITLE\0TITLE_ID\0";
    size_t key_start = 20 + 2 * 16;
    size_t data_start = key_start + sizeof(keys);
    size_t title_len = strlen(title) + 1, id_len = strlen(title_id) + 1;
    memcpy(buf, "\0PSF", 4);
    buf[4] = 1, buf[5] = 1;
    buf[8] = (unsigned char)key_start;
    buf[12] = (unsigned char)data_start;
    buf[16] = 2;
    unsigned char *e0 = buf + 20, *e1 = buf + 36;
    e0[0] = 0, e0[2] = 0x04, e0[3] = 0x02, e0[4] = (unsigned char)title_len, e0[8] = 128, e0[12] = 0;
    e1[0] = 6, e1[2] = 0x04, e1[3] = 0x02, e1[4] = (unsigned char)id_len, e1[8] = 16, e1[12] = 128;
    memcpy(buf + key_start, keys, sizeof(keys));
    memcpy(buf + data_start, title, title_len);
    memcpy(buf + data_start + 128, title_id, id_len);
    return data_start + 128 + 16;
}

static void test_param_sfo(void) {
    unsigned char sfo[512];
    char out[AKENO_TITLE_NAME_MAX];
    size_t len = make_sfo(sfo, sizeof(sfo), "Classic Game", "CUSA00001");
    CHECK(akeno_param_sfo_title(sfo, len, "CUSA00001", out, sizeof(out)) == 0);
    CHECK_STR(out, "Classic Game");
    CHECK(akeno_param_sfo_title(sfo, len, "CUSA00002", out, sizeof(out)) == -1);
    /* Truncated or corrupted files are rejected without reading out of bounds. */
    CHECK(akeno_param_sfo_title(sfo, 40, "CUSA00001", out, sizeof(out)) == -1);
    sfo[16] = 0xFF;
    CHECK(akeno_param_sfo_title(sfo, len, "CUSA00001", out, sizeof(out)) == -1);
    CHECK(akeno_param_sfo_title((const unsigned char *)"PSF", 3, "CUSA00001", out, sizeof(out)) == -1);
}

static void write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(data, f);
    fclose(f);
}

static void test_meta_lookup(void) {
    char root[] = "/tmp/akeno-meta-XXXXXX";
    if (!mkdtemp(root)) {
        CHECK(0);
        return;
    }
    char path[700];
    const char *dirs[] = { "/mnt", "/mnt/sandbox", "/mnt/sandbox/pfsmnt", "/mnt/sandbox/pfsmnt/PPSA01234-app0",
                           "/mnt/sandbox/pfsmnt/PPSA01234-app0/sce_sys", "/user", "/user/appmeta", "/user/appmeta/PPSA01234" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
        mkdir(path, 0700);
    }
    /* An unusable earlier candidate (wrong title) is skipped; the mounted package is used. */
    snprintf(path, sizeof(path), "%s/user/appmeta/PPSA01234/param.json", root);
    write_file(path, "{\"titleId\":\"PPSA05555\",\"localizedParameters\":{\"en-US\":{\"titleName\":\"Wrong\"}}}");
    snprintf(path, sizeof(path), "%s/mnt/sandbox/pfsmnt/PPSA01234-app0/sce_sys/param.json", root);
    write_file(path, "{\"titleId\":\"PPSA01234\",\"localizedParameters\":{\"en-US\":{\"titleName\":\"Mounted Game\"}}}");

    char name[AKENO_TITLE_NAME_MAX], source[300];
    CHECK(akeno_game_meta_lookup(root, "PPSA01234", name, sizeof(name), source, sizeof(source)) == 0);
    CHECK_STR(name, "Mounted Game");
    CHECK_STR(source, "/mnt/sandbox/pfsmnt/PPSA01234-app0/sce_sys/param.json");
    CHECK(akeno_game_meta_lookup(root, "PPSA07777", name, sizeof(name), source, sizeof(source)) == -1);
    CHECK(akeno_game_meta_lookup(root, "../../etc", name, sizeof(name), source, sizeof(source)) == -1);
    CHECK(akeno_game_meta_lookup(NULL, "PPSA01234", name, sizeof(name), source, sizeof(source)) == -1);

    unlink(path);
    snprintf(path, sizeof(path), "%s/user/appmeta/PPSA01234/param.json", root);
    unlink(path);
    for (size_t i = sizeof(dirs) / sizeof(dirs[0]); i-- > 0;) {
        snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
        rmdir(path);
    }
    rmdir(root);
}

static void test_icon_lookup(void) {
    char root[] = "/tmp/akeno-icon-XXXXXX";
    if (!mkdtemp(root)) {
        CHECK(0);
        return;
    }
    char path[700];
    const char *dirs[] = { "/user", "/user/appmeta", "/user/appmeta/PPSA01234", "/user/appmeta/CUSA00001" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
        mkdir(path, 0700);
    }
    static const unsigned char png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    snprintf(path, sizeof(path), "%s/user/appmeta/PPSA01234/icon0.png", root);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(png, 1, sizeof(png), f);
        fclose(f);
    }
    /* Not a PNG (for example an HTML file renamed to icon0.png): ignored. */
    char fake[700];
    snprintf(fake, sizeof(fake), "%s/user/appmeta/CUSA00001/icon0.png", root);
    write_file(fake, "<html><script>alert(1)</script></html>");

    size_t len = 0;
    char source[160];
    unsigned char *data = akeno_game_icon_lookup(root, "PPSA01234", &len, source, sizeof(source));
    CHECK(data != NULL && len == sizeof(png));
    CHECK_STR(source, "/user/appmeta/PPSA01234/icon0.png");
    free(data);
    CHECK(akeno_game_icon_lookup(root, "CUSA00001", &len, source, sizeof(source)) == NULL);
    CHECK(akeno_game_icon_lookup(root, "PPSA07777", &len, source, sizeof(source)) == NULL);
    CHECK(akeno_game_icon_lookup(root, "../../etc", &len, source, sizeof(source)) == NULL);
    CHECK(akeno_game_icon_lookup(root, "NPXS40028", &len, source, sizeof(source)) == NULL);
    CHECK(akeno_game_icon_lookup(NULL, "PPSA01234", &len, source, sizeof(source)) == NULL);

    unlink(path);
    unlink(fake);
    for (size_t i = sizeof(dirs) / sizeof(dirs[0]); i-- > 0;) {
        snprintf(path, sizeof(path), "%s%s", root, dirs[i]);
        rmdir(path);
    }
    rmdir(root);
}

void run_game_tests(void);
void run_game_tests(void) {
    test_title_ids();
    test_appinfo_parse();
    test_layout_vote();
    test_classify();
    test_debounce();
    test_clean_names();
    test_param_json();
    test_param_sfo();
    test_meta_lookup();
    test_icon_lookup();
}
