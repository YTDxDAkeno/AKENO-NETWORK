# AKENO NETWORK Native (PS5) — 1.0.0-beta.1

A native SDL2 foreground app for the PS5: 1920×1080 UI, DualSense navigation, and the AKENO social
features — friends, profiles, activity, messages with its own controller keyboard, parties,
requests, invitations, notifications and settings — through the AKENO daemon's local interface
(the daemon keeps the only server connection). Not a browser wrapper.

**Status:** Native 0.1 ran on a PS5 with firmware 12.20. Native 0.2 and 1.0.0-beta.1 have **not**
run on a PS5; everything newer than 0.1 is tested on PCs only.

- Why and how: [docs/PS5_NATIVE_ADR.md](../../docs/PS5_NATIVE_ADR.md)
- Client, security, limitations: [docs/PS5_NATIVE_CLIENT.md](../../docs/PS5_NATIVE_CLIENT.md)
- Daemon interface (IPC protocol 2): [docs/PS5_NATIVE_PHASE2.md](../../docs/PS5_NATIVE_PHASE2.md)
- Install, first run, controller, troubleshooting, rollback: [docs/native/](../../docs/native/README.md)
- Hardware test: [docs/HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md](../../docs/HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md)
- Changes: [CHANGELOG.md](CHANGELOG.md); handoff: [docs/HANDOFF_PS5_NATIVE.md](../../docs/HANDOFF_PS5_NATIVE.md)
- CI and the release package: `.github/workflows/ps5-native.yml`

```sh
scripts/fetch-deps.sh
cmake -S . -B build -DAKN_SANITIZE=ON && cmake --build build && ctest --test-dir build
node tests/integration/run.mjs build/akeno-native
```
