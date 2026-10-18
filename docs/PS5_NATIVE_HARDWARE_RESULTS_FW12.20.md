# AKENO NETWORK Native 0.1 — hardware results on firmware 12.20

**Real hardware evidence.** Reported by the console owner on 2026-10-17: PS5, firmware **12.20**,
kstuff, the existing AKENO daemon started with the owner's ELF loader, and AKENO NETWORK Native
0.1 started from a homebrew launcher, both running **at the same time**. The owner confirmed the
results from the screen and from the app's own `native-report.txt` and `native.log`. Only what was
reported is recorded here; anything not mentioned stays **unverified**.

## Builds

| Component | Build | Identified by |
| --- | --- | --- |
| AKENO NETWORK Native | **`d094e62bcaf1-ps5`**: `akeno-native-ps5-0.1.0-prototype` from the push run [39058830774](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39058830774) of `d094e62`, `eboot.elf` SHA-256 `7eb99ff5a8c6cce49da25a6a52201931001dca7c0a9d2a6bbce6a0ac24eb7560` | the owner's `native-report.txt` (build ID) |
| AKENO daemon | reported its version as **0.5.0-alpha** through its loopback probe | the Alpha 0.5 release ELF and its audio lab builds all report 0.5.0-alpha; which one ran was not reported |

**Known-good fallback: `d094e62bcaf1-ps5`.** Every "PS5 native client" CI run rebuilds it from
`d094e62` and stops unless it is byte-identical to `7eb99ff5…`. It ships as
`fallback/native-0.1-d094e62bcaf1/AKENONative/`, which is the folder to restore.

The pull-request run [39058834341](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39058834341)
built the same tree from the PR's merge commit (build ID `47488b65f4c2-ps5`, SHA-256
`0856094cf434b7e8114bad396fe8f942e87b408e2c847d015cf811aec913e4db`). That file was **not** run on a
PS5. CI keeps rebuilding and verifying it, and it stays in the artifact as
`fallback/native-0.1-47488b65f4c2/` for reference only. It is not the tested build.

## Results

| Area | Test plan | Result on 12.20 | Status |
| --- | --- | --- | --- |
| Foreground application | N3 | The app ran as the **foreground application `FAKE00000`** (the title that websrv-style homebrew launchers start) | **Verified on hardware** |
| Native video | N3 | Native output **1920×1080**, full-screen AKENO UI on the TV | **Verified on hardware** |
| DualSense | N4 | Controller **opened successfully**; **2,274** controller events received; **48** focus movements; **0** dropped controller events | **Verified on hardware** (input and navigation) |
| Full controller coverage | N4 | Not itemised: every single button, the L2/R2 analog values, the touchpad, hold ◯ and disconnect/reconnect were not reported one by one | **Partially unverified** |
| Foreground audio (voice roadmap V1) | N5 | **Four** native audio tests completed successfully; **all four marked HEARD** by the owner's direct feedback | **Verified on hardware: audible** |
| Account | N6 | Existing account authentication successful: the existing pairing, read-only, `GET /api/v1/devices/current` → **HTTP 200**, signed in, **14** device permissions | **Verified on hardware** |
| Daemon at the same time | N6 | Daemon liveness probe: "Running, version 0.5.0-alpha"; both processes ran simultaneously | **Verified on hardware** |
| Background protection (native ELF sent with the ELF loader) | N1 | `native.log` shows that the background-context protection triggered in the earlier ELF-loader attempts: the app recognised it was not the foreground application | **Verified on hardware** (from the log; the notification and "nothing closed" were not reported separately) |
| Clean exit and second start | N7 | not reported | **Unverified** |

## What this means

- **Controller and audible sound work for AKENO in the foreground.** The background daemon on the
  same console gets `scePadOpen` `0x809B0081`, and its `sceAudioOut` output is accepted but silent
  ([daemon results](PS5_HARDWARE_RESULTS_FW12.20.md)). The foreground app opened the DualSense,
  received input without drops, and played a tone the owner heard four times out of four. The
  foreground-application hypothesis of the [ADR](PS5_NATIVE_ADR.md) is now **confirmed for
  AKENO** on 12.20.
- **Voice roadmap V1 is met:** a native AKENO app plays audible sound. **Voice chat is not
  claimed.** No microphone was used. Party audio, two-way voice and voice during a game are
  untested. Those steps (V2–V5) are still ahead.
- The native app runs **next to the daemon** and shares the existing pairing without interfering.
  The daemon keeps the only realtime connection; the native app only reads the credential. Phase 2
  builds on this ([PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)).
- The safety design holds on hardware. Sent with the ELF loader, the app runs as a background
  process, recognises this and stops before it opens anything.

## Still open

1. Full controller coverage: the Controller test with every button, both sticks, L2/R2, the
   touchpad and hold ◯; switching the controller off and on.
2. Clean exit (Home → Exit) and a second start from the launcher.

Both are repeated in the [0.2 hardware test plan](HARDWARE_TEST_PLAN_PS5_NATIVE_0.2.md) (P7, P8).

## Unchanged by these results

No server, VPS, production or Account Bridge change was made. The daemon release ELF is still the
hardware-tested Alpha 0.5 RC file (CI compares it on every commit). The browser page stays the
working client.

## Native 0.2 preview — owner report (recorded 2026-10-18)

The owner ran Native 0.2 on the same PS5 with firmware 12.20 and earlier provided screenshots of
**real friend profiles and game activity** on the console. The build ID was not recorded with the
report (the 0.2 preview CI build is `4b4602c7d0b7`, run 39065377144). This confirms that the 0.2
social screens displayed real data through the daemon on hardware; it does **not** establish
complete coverage of the 0.2 test plan (P1–P8 were not reported item by item) and says nothing
about 1.0.0-beta.1, which is tested with
[HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md).
