/*
 * Reproduces the "DetourFunction: relocation failed at +6 / Detour failed: sceRegMgrGetInt" failure
 * of a second onionHEN Toolbox injection into a SceShellUI that an earlier injection already hooked
 * (docs/SHELLUI_HOST_DIAGNOSIS.md). It compiles onionHEN's own relocator (pinned v0.9.0,
 * libonion_detour) on a PC and feeds it what PrepareDetour() would read at sceRegMgrGetInt:
 *
 *   pristine:  the function's real prologue         -> must relocate (the original host works)
 *   patched:   FF 25 00 00 00 00 <8-byte hook addr> -> what a FIRST injection leaves behind;
 *              onionHEN never removes its detours, so a second injection decodes the old hook
 *              address as instructions starting at +6.
 *
 * The hook address is load base + the build's offset of sceRegMgrGetInt_hook. The base is a 16 KiB
 * aligned user address that differs per boot, so the outcome is reported as a distribution over
 * bases. Nothing here runs on, or talks to, a console.
 *
 *   double_detour_check [--expect] <label>=<hex offset> ...
 *
 * With --expect, exits non-zero unless the recorded facts still hold: the pristine prologue
 * relocates, the original v0.9.0 offset never fails, the PR #14 offset fails at +6 for some bases.
 */
#include <onion/x64_relocator.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace {

constexpr size_t kHookLength = 14;          /* HOOK_LENGTH in onion/detour.h */
constexpr uint64_t kBaseFirst = 0x800000000ull; /* plausible SceShellUI mmap range */
constexpr uint64_t kBaseLast = 0x2000000000ull;
constexpr uint64_t kBaseStep = 0x4000ull * 977; /* 16 KiB pages, sampled */
constexpr uintptr_t kTarget = 0x900123450ull;    /* 16-byte aligned, as PrepareDetour requires */

/* A clang x86-64 prologue (push rbp; mov rbp,rsp; push r15..rbx; sub rsp,0x28). */
const uint8_t kPristine[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
                             0x53, 0x48, 0x83, 0xec, 0x28, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

struct Outcome {
    bool ok;
    size_t offset;
    std::string text;
};

Outcome relocate(const uint8_t *window) {
    uint8_t out[ONION_X64_TRAMPOLINE_CAPACITY];
    onion_x64_relocate_result r{};
    const bool ok = onion_x64_relocate(window, kTarget, out, kTarget - 0x100000, kHookLength, sizeof(out), &r);
    if (ok) return {true, 0, "accepted (silently chains to the old hook)"};
    char text[128];
    snprintf(text, sizeof(text), "relocation failed at +%zu: %s", r.error_offset, onion_x64_relocate_error_string(r.error));
    return {false, r.error_offset, text};
}

struct Distribution {
    int total = 0, failed = 0, failed_at_6 = 0;
};

Distribution scan(const char *label, uint64_t hook_offset) {
    std::map<std::string, int> outcomes;
    Distribution d;
    for (uint64_t base = kBaseFirst; base < kBaseLast; base += kBaseStep) {
        const uint64_t hook = base + hook_offset;
        uint8_t window[64];
        memset(window, 0xcc, sizeof(window));
        memcpy(window, kPristine, sizeof(kPristine));
        const uint8_t jump[] = {0xff, 0x25, 0, 0, 0, 0}; /* onion_x64_build_atomic_patch */
        memcpy(window, jump, sizeof(jump));
        memcpy(window + sizeof(jump), &hook, sizeof(hook));
        const Outcome o = relocate(window);
        outcomes[o.text]++;
        d.total++;
        if (!o.ok) d.failed++;
        if (!o.ok && o.offset == 6) d.failed_at_6++;
    }
    printf("second injection over %s (sceRegMgrGetInt_hook at +%#llx), %d load bases:\n", label,
           (unsigned long long)hook_offset, d.total);
    for (const auto &[text, count] : outcomes) printf("  %5.1f%%  %s\n", 100.0 * count / d.total, text.c_str());
    return d;
}

} // namespace

int main(int argc, char **argv) {
    bool expect = false;
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "--expect") == 0) {
        expect = true;
        first = 2;
    }
    uint8_t window[64];
    memset(window, 0xcc, sizeof(window));
    memcpy(window, kPristine, sizeof(kPristine));
    const Outcome pristine = relocate(window);
    printf("first injection over the pristine prologue: %s\n", pristine.ok ? "relocates" : pristine.text.c_str());
    bool facts = pristine.ok;
    for (int i = first; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (!eq) {
            fprintf(stderr, "usage: %s [--expect] <label>=<hex offset> ...\n", argv[0]);
            return 2;
        }
        const std::string label(argv[i], (size_t)(eq - argv[i]));
        const Distribution d = scan(label.c_str(), strtoull(eq + 1, nullptr, 16));
        if (label == "original-v0.9.0") facts = facts && d.failed == 0;
        if (label == "pr14-custom-host") facts = facts && d.failed_at_6 > 0;
    }
    if (expect && !facts) {
        fprintf(stderr, "double_detour_check: the recorded diagnosis no longer holds; revisit docs/SHELLUI_HOST_DIAGNOSIS.md\n");
        return 1;
    }
    return 0;
}
