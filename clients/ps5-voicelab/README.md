# AKENO Voice Lab (EXPERIMENTAL)

Background-audio diagnostics for party voice during games: microphone capture (A), audio output
(B), full duplex (C), process and network survival (D), and game audio alongside AKENO audio (E),
all from a background payload, at home and while a game runs. It writes
`/data/akeno-voicelab/report.txt` and ends.

What it measures, its safety rules, the hardware test plan and how to read the report:
[docs/PS5_BACKGROUND_VOICE.md](../../docs/PS5_BACKGROUND_VOICE.md).

- `src/lab.c`: the plan and the report (portable, tested on a PC against `backend_host.c`)
- `src/lab_pcm.c`: PCM statistics, beeps, the echo ring
- `src/lab_net.c`: daemon and server probes
- `src/backend_ps5.c`: the only file that calls console functions
- `console/check-voicelab.sh`: import gate for the PS5 ELF
