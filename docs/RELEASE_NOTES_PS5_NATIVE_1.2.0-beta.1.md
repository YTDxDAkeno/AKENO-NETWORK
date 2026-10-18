# AKENO NETWORK Native 1.2.0-beta.1: release notes

**Beta, pre-release, not run on a PS5.** Everything below is tested on PCs only (unit tests,
the UI integration test, the updater against a mock GitHub, end to end with the real server).
Test plan: [HARDWARE_TEST_PLAN_PS5_NATIVE_1.2.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.2.0-beta.1.md).

## New: built-in updater

- Settings → **Updates**: current version, stable or beta channel, check for updates, automatic
  checks (at start and every 6 hours), latest release with size and date, changelog, download and
  install with progress, speed and remaining size (cancel and retry), restart to apply, history.
- After an automatic check: "A new AKENO NETWORK update is available. Install now?" with
  **Update** and **Later**.
- Only signed releases of `YTDxDAkeno/AKENO-NETWORK` over verified HTTPS are installed; the package
  is checked file by file; nothing from it is ever run by the updater.
- Versions install side by side; your sign-in, pairing and settings are never touched. A version
  that does not start twice is skipped by the launcher and AKENO goes back to the previous one.
- **Installs are refused until the owner creates the release signing key** and a build carries
  it. Details: [PS5_NATIVE_UPDATER.md](PS5_NATIVE_UPDATER.md).

## Party voice during games: what is known, and the Voice Lab

AKENO Native is a foreground app: the launcher closes the game to start it, so its voice stops
when a game starts. Voice during a game needs a separate background payload. On 12.20 a background
payload's audio output is accepted but was not heard, and its controller access is refused
(`0x809B0081`); background microphone capture has never been measured.

1.2.0-beta.1 therefore ships **no** background voice. It ships the **Voice Lab**
(`experimental/voice-lab/`), a separate diagnostic payload that measures microphone samples,
audible output variants, echo, timing and network during a real game and writes a report. The
background voice service is built only if that report shows it can work
([PS5_BACKGROUND_VOICE.md](PS5_BACKGROUND_VOICE.md)).

## Quick access while playing

Unchanged and honestly labelled: the verified way back to AKENO during a game is the message
notification with "Open in AKENO" ([QUICK_ACCESS.md](QUICK_ACCESS.md)). The OnionHEN companion stays
experimental and the owner's decision ([IN_GAME_ACCESS.md](IN_GAME_ACCESS.md)).

## Fixes

- Network errors while downloading were reported as the wrong kind (a TLS or timeout failure
  could read as a different error).
- AKENO now records every pause of more than one second in its own running (for example while the
  system shows the home screen; not yet observed on a PS5) and what happened to voice
  and the microphone; the report has a new section "[Suspend and resume (test F)]".

## Requirements

- The native-IPC daemon of this package.
- For voice, the AKENO server with the voice gateway, as for 1.1.0-beta.1.
