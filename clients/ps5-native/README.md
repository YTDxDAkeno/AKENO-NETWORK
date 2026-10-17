# AKENO NETWORK Native (PS5 prototype)

A native SDL2 foreground app for the PS5: 1920×1080 UI, DualSense navigation, a foreground audio
test (milestone V1) and the AKENO account read from the daemon's existing pairing. Not a browser
wrapper; the daemon in `clients/ps5` is unchanged.

- Why and how: [docs/PS5_NATIVE_ADR.md](../../docs/PS5_NATIVE_ADR.md)
- Build, install, security, limitations: [docs/PS5_NATIVE_CLIENT.md](../../docs/PS5_NATIVE_CLIENT.md)
- Hardware test: [docs/HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md](../../docs/HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md)
- CI: `.github/workflows/ps5-native.yml`

```sh
scripts/fetch-deps.sh
cmake -S . -B build -DAKN_SANITIZE=ON && cmake --build build && ctest --test-dir build
node tests/integration/run.mjs build/akeno-native
```

Nothing here has run on a PS5 yet.
