# Hardware test plan: AKENO NETWORK Native 1.2.0-beta.1

For your PS5 with firmware 12.20 (kstuff), started from websrv's launcher, with the native-IPC
daemon of the same package. **Nothing of 1.2.0-beta.1 has run on a PS5.** Record ✅ / ❌ / what you
saw for every step and keep the reports. A precise failure is a valid result: stop there and send
what you have.

What you need: the package `akeno-network-native-1.2.0-beta.1` from the approved CI run (check
`SHA256SUMS`); FTP or another way to read and write files under `/data`.

Safety: the updater writes only inside the AKENO folder (`update/`) and
`/data/akeno-native/updater.json`. It never touches firmware, system partitions or PSN settings.
The Voice Lab only plays tones and reads the microphone; it records and sends nothing. If the
console stops responding, hold the power button until it beeps twice; the factory `eboot.elf` of
the AKENO folder is never changed, and the tile's options can always start it.

## U0: Install

| # | Step | Expected |
| --- | --- | --- |
| U0.1 | Send `daemon/akeno-network-ps5-native-ipc.elf` as before | "AKENO NETWORK Alpha 0.5 started." |
| U0.2 | Copy `AKENONative/` over `/data/homebrew/AKENONative/` (your settings and sign-in are elsewhere and stay) | The tile says **AKENO NETWORK / Native** |
| U0.3 | Start it | Home with the pill **BETA · 1.2**; you are still signed in 📷 |

## U1: Updates screen (no release in the update format exists yet)

| # | Step | Expected |
| --- | --- | --- |
| U1.1 | Settings → **Updates** | Current version 1.2.0-beta.1, Update channel Beta, Automatic update checks On 📷 |
| U1.2 | Wait about 10 s after start, or press **Check for updates** | A result, not a hang: "up to date", or a message naming why (network, GitHub limit). Write it down |
| U1.3 | Switch the channel to Stable and back; switch automatic checks off and on; exit and restart AKENO | The choices are kept |
| U1.4 | **Update history** | Opens; may list the trial start of this version |
| U1.5 | Unplug the network cable / turn off Wi-Fi, **Check for updates** | A readable network error within about 30 s; AKENO keeps working |

## U2: Rollback in the launcher (by hand, no download)

This tests the part that protects you when an update does not start. Use FTP in
`/data/homebrew/AKENONative/`:

1. Create `update/versions/1.2.0-beta.99/` and copy `eboot.elf` into it.
2. Create `update/active.json`:
   ```json
   {"format":1,"version":"1.2.0-beta.99","elf":"update/versions/1.2.0-beta.99/eboot.elf",
    "state":"pending","attempts":2,"max_attempts":2,"previous":{"version":"","elf":"eboot.elf"}}
   ```

| # | Step | Expected |
| --- | --- | --- |
| U2.1 | Look at the AKENO tile in the launcher | "Native 1.2.0-beta.99 - did not start; using the previous version" 📷 |
| U2.2 | Start it | AKENO 1.2.0-beta.1 starts and shows a notification that 1.2.0-beta.99 was rolled back |
| U2.3 | Settings → Updates → Update history | A line "rolled back from 1.2.0-beta.99 to 1.2.0-beta.1" |
| U2.4 | FTP | `update/versions/1.2.0-beta.99/` is gone; `active.json` names 1.2.0-beta.1 with 1.2.0-beta.99 in `rejected` |
| U2.5 | Hold the tile (options) | "Start the version installed by hand" is offered and works |

To undo: delete the `update/` folder; the launcher then starts `eboot.elf` as before.

## U3: A real update (only after your approval)

Needs the signing key (docs/PS5_NATIVE_UPDATER.md, "Signing key"), a 1.2.0-beta.1 built with it,
and a published pre-release `native-1.2.0-beta.2`. Nothing here is done without your go-ahead.
Then: the question "A new AKENO NETWORK update is available. Install now?" appears after start;
**Update** shows progress, speed and remaining size; **Restart to apply**; the tile names 1.2.0-beta.2;
after 20 s of normal running the history says "confirmed".

## F: Suspend and resume (the foreground app)

| # | Step | Expected / report |
| --- | --- | --- |
| F.1 | In AKENO, join party voice (optional), then press PS and go to the home screen for 30 s, then come back | AKENO continues; voice reconnects if it was on |
| F.2 | Start a game from the home screen (this closes AKENO, as every launcher start does), then start AKENO again | AKENO starts normally |
| F.3 | Exit AKENO (a report is saved on the way out) and open `/data/akeno-native/native-report.txt` | `[Suspend and resume (test F)]` lists the pauses, their length and voice/microphone before and after. Send the report |

## Voice Lab (A–E): can voice continue during a game?

This is the evidence for the most important open question. Follow
`docs/BACKGROUND_VOICE.md` in the package (`docs/PS5_BACKGROUND_VOICE.md` in the repository), "Hardware test plan": start `experimental/voice-lab/AKENOVoiceLab`
from the home screen, follow its notifications, start a game when asked, then send
`/data/akeno-voicelab/report.txt` with every `____` filled in. Until this report exists, party
voice stops when a game starts, and AKENO does not claim otherwise.
