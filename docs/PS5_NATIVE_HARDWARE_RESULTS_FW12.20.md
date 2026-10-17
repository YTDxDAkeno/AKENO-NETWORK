# AKENO NETWORK Native 0.1 — hardware results on firmware 12.20

**Real hardware evidence.** Reported by the console owner on 2026-10-17: PS5, firmware **12.20**,
kstuff, the existing AKENO daemon started with the owner's ELF loader, and AKENO NETWORK Native
0.1 running **at the same time**. Only what was reported is recorded here; every step that was not
mentioned stays **untested**.

## Builds

| Component | Build | How it is identified |
| --- | --- | --- |
| AKENO NETWORK Native | `akeno-native-ps5-0.1.0-prototype` of `d094e62`. Two CI runs published it with the same code and different build IDs: the push run [39058830774](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39058830774) (build ID `d094e62bcaf1-ps5`, `eboot.elf` SHA-256 `7eb99ff5a8c6cce49da25a6a52201931001dca7c0a9d2a6bbce6a0ac24eb7560`) and the pull-request run [39058834341](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39058834341), which built the PR's merge commit `47488b65f4c2` (same tree; build ID `47488b65f4c2-ps5`, SHA-256 `0856094cf434b7e8114bad396fe8f942e87b408e2c847d015cf811aec913e4db`) | **to be confirmed**: the System screen and `native-report.txt` show the build ID |
| AKENO daemon | reported its version as **0.5.0-alpha** through its loopback probe | the release ELF and the audio lab builds of Alpha 0.5 all report 0.5.0-alpha; which one ran was not reported |

Both candidates are kept as the **fallback**: every "PS5 native client" CI run rebuilds them from
`d094e62` and stops unless each is byte-identical to its SHA-256 above, then ships them as
`fallback/native-0.1-d094e62bcaf1/AKENONative/` and `fallback/native-0.1-47488b65f4c2/AKENONative/`
next to the new build. They differ only in the build ID text.

## Results

| Area | Test plan | Result on 12.20 | Status |
| --- | --- | --- | --- |
| Native app and daemon at the same time | N6 | Both ran simultaneously; the native app was in the foreground, the daemon kept serving its loopback interface | **Verified on hardware** |
| Daemon liveness probe (`GET http://127.0.0.1:8642/api/instance`) | N6 | "AKENO daemon: Running, version 0.5.0-alpha" | **Verified on hardware** |
| AKENO server over HTTPS from the native app (mbedTLS 3.6.7, CA bundle) | N6 | "AKENO server: HTTP 200" for `GET /api/v1/devices/current` | **Verified on hardware** |
| Existing pairing reused read-only | N6 | "Existing pairing: valid" | **Verified on hardware** |
| Account and device permissions | N6 | "Account: signed in", **14** device permissions granted | **Verified on hardware** |
| Full-screen native UI on the TV | N3 | The Connection screen was shown and read on the TV | **Verified on hardware** (the screen itself; N3's other checks were not reported) |
| DualSense navigation | N4 | The Connection screen is reached from Home with the D-pad or stick and ✕, so basic navigation worked | **Inferred** from the report; the full controller test (every button, hold ◯, disconnect) was not reported |
| Foreground audio test (voice roadmap V1) | N5 | not reported | **untested** |
| Background safety check (native ELF sent to elfldr) | N1 | not reported | untested |
| Exit and second start | N7 | not reported | untested |

## What this means

- A native application started from a homebrew launcher runs in the **foreground on 12.20 while
  the AKENO daemon runs in the background**, and both use the same pairing without interfering:
  the daemon keeps the only realtime connection, the native app only reads the credential.
- The native app reaches the daemon over loopback and the AKENO server over verified TLS from
  the foreground process. This is the foundation Phase 2 builds on: the native screens talk to
  the daemon over a local interface, and the daemon stays the single client of the server
  ([PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)).
- It does **not** show that foreground audio is audible (N5 not reported), and it says nothing
  about voice. Voice chat is not claimed.

## Still wanted from the console

1. `native-report.txt` and `native.log` from `/data/akeno-native/` (both redacted: no token,
   server host or IP address). Its `[Controller]` and `[Audio]` sections answer N4 and N5.
2. The result of the audio test (N5): was the tone heard, at which level and format?
3. The build ID on the System screen (`d094e62bcaf1-ps5` or `47488b65f4c2-ps5`) and which
   launcher started the app.

## Unchanged by these results

No server, VPS, production or Account Bridge change was made. The daemon release ELF is still the
hardware-tested Alpha 0.5 RC file (CI compares it on every commit). The browser page stays the
working client.
