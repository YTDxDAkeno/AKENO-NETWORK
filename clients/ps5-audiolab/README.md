# AKENO Audio Lab 3.1 (EXPERIMENTAL)

A standalone, isolated PS5 audio-output one-shot. After launch it announces the test, waits five
seconds, plays exactly one unchanged default 440-Hz tone, closes its own port, writes reports, and
exits. It has no FTP/control-file command path and no repeat loop.

The payload uses `/data/akeno-audiolab/` only. It has no network, microphone, PSN, game control,
OnionHEN integration, routing/focus/volume control, or port-state query. The import gate also
rejects any reference to `sceAudioOutGetPortState`.

German owner documentation: [README-DE](package/README-DE.md),
[hardware test](package/HARDWARE-TEST-DE.md), and [safety](package/SAFETY.md).

## Build and validation

```sh
cmake -S clients/ps5-audiolab -B clients/ps5-audiolab/build -DAL_SANITIZE=ON
cmake --build clients/ps5-audiolab/build
ctest --test-dir clients/ps5-audiolab/build --output-on-failure

PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-audiolab/console
clients/ps5-audiolab/console/check-audiolab.sh clients/ps5-audiolab/build-ps5
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-audiolab/console delivery
```

The delivery ZIP contains the ELF, German guides, and `SHA256SUMS.txt`. A passing host test or
cross-build is not evidence of PS5 hardware behaviour.
