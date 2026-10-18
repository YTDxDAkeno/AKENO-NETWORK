# Hardware test plan — AKENO NETWORK Native 1.1.0-beta.1 (two-way party voice)

For **your PS5 with firmware 12.20 (kstuff)**, started from **websrv's launcher**, with the
**native-IPC daemon of the same package** (native interface 3). Nothing of 1.1.0-beta.1 has run on
a PS5. The audio *output* path is the one Native 0.1 played audibly on 12.20; the *microphone* has
never been opened by AKENO on a console. **This plan is the first time.** Record ✅ / ❌ / what you
saw for every step and keep the reports (end of this document). If a step fails, stop there and
send what you have: a precise failure is a valid result.

What you need:

- the package **akeno-network-native-1.1.0-beta.1** from the approved CI run (check
  `SHA256SUMS`);
- your AKENO server with **voice and the voice gateway set up** (docs/VOICE.md, "Operating the
  gateway"): `VOICE_GATEWAY_URL=wss://<domain>/voice-gateway`. Do this on a test setup first if
  you can; production is your decision;
- a **second person** ("the friend") on the website or the app on another device and network, a
  member of your AKENO friends, ideally with headphones (no echo);
- for V3: a headset for the PS5 (USB or 3.5 mm on the DualSense) if you have one.

Safety: the microphone opens only after you press **Join voice** (or **Start the test**) and
confirm. Nothing is recorded or stored; the report contains levels and counts only. Leave voice
with ◯ on the voice row or by leaving the party; Exit closes everything. If the console stops
responding: hold the power button until it beeps twice; then the rollback below.

## V0 — Install (as for 1.0.0-beta.1)

| # | Step | Expected |
| --- | --- | --- |
| V0.1 | Exit AKENO on its page; send `daemon/akeno-network-ps5-native-ipc.elf` | "AKENO NETWORK Alpha 0.5 started." |
| V0.2 | Copy `AKENONative` to `/data/homebrew/AKENONative/`; start **AKENO NETWORK — Native 1.1.0-beta.1 (voice)** from the launcher | Home, pill **BETA · 1.1** 📷 |
| V0.3 | Settings → System (or Settings, THIS BUILD) | "Daemon 0.5.0-alpha, native interface 3" |

## V1 — Microphone diagnostic (local only, nothing is sent)

| # | Step | Expected / report |
| --- | --- | --- |
| V1.1 | Settings → last row **Microphone test** → ✕ → read the confirmation → **Start the test** | The row shows "Opening the microphone...", then **Level …% · peak …% · 16000 Hz** (or 48000 Hz), or an error with a code 📷 |
| V1.2 | Speak into the DualSense (no headset connected) | The level bar moves while you speak, stays low when silent |
| V1.3 | □ (hear yourself) — only with a headset, or with the TV volume low | You hear yourself with a short delay; □ again stops it |
| V1.4 | ✕ (stop) | "Microphone test" row idle; top bar chip gone |
| V1.5 | If V1.1 showed an error | Note the exact text: "No microphone function on this system: dlopen(libSceAudioIn.sprx) failed: …", "The microphone could not be opened (0x……)", … — **this is the hardware result**; continue with V2 anyway (listening works without a microphone) |

## V2 — Join voice and listen

| # | Step | Expected |
| --- | --- | --- |
| V2.1 | Create a party in AKENO Native (or accept the friend's invitation) | Party screen with your party |
| V2.2 | The friend joins the party and **joins voice on the website** and talks | Their row says "in voice" |
| V2.3 | Party → **Join voice** → confirmation "Join voice with your microphone?" → **Join voice** | "Asking AKENO for a voice pass...", "Connecting to voice...", then **In voice**; the top bar shows **Voice** 📷 |
| V2.4 | The friend talks | **You hear them** (TV or headset); their row says **speaking** in green 📷 |
| V2.5 | The friend mutes on the website | You stop hearing them; their row "muted" |

## V3 — Talk (full duplex)

| # | Step | Expected |
| --- | --- | --- |
| V3.1 | You talk (DualSense microphone) | **The friend hears you**; on the website your avatar shows speaking; on the PS5 the Mute row shows your level |
| V3.2 | Both talk at the same time for a minute | Both hear each other; note echo, delay (estimate in seconds), dropouts |
| V3.3 | Connect a headset to the DualSense (or USB); repeat V3.1/V2.4 | Note whether the system now uses the headset microphone and plays to the headset |
| V3.4 | Mute (✕ on "Mute microphone") | The friend no longer hears you; the website shows you muted; top bar "Voice (muted)" |
| V3.5 | Unmute | The friend hears you again |
| V3.6 | Deafen | You hear nobody and nobody hears you; Undeafen, then Unmute: back to normal |
| V3.7 | Voice volume row: ◀ ▶ | The friend gets quieter / louder (0–200 %) |
| V3.8 | Microphone volume row: ◀ ▶ | The friend hears you quieter / louder |

## V4 — Network and cleanup

| # | Step | Expected |
| --- | --- | --- |
| V4.1 | Pull the console's network cable (or Wi-Fi off) for 10 s, then reconnect | "Reconnecting to voice..." then **In voice** again within ~15 s; audio both ways again |
| V4.2 | The friend removes you from the party (or you leave it on the website) | Voice ends at once: "You were removed from the voice chat." / "You are no longer in that party." |
| V4.3 | Join again, then **Leave voice** | "Not in voice"; the friend sees you leave; the microphone closes (V1.1 can start again) |
| V4.4 | Join again, then Home → Exit | AKENO Native closes normally; the friend sees you leave |
| V4.5 | 15 minutes in voice with talking on both sides | No freeze, no growing delay; note the round trip from the report |
| V4.6 | Join voice in AKENO Native while the same account is in voice on the website | The newer one takes over; the other says it joined elsewhere |

## V5 — In-game behaviour (observation only; not a feature of this build)

| # | Step | Expected / report |
| --- | --- | --- |
| V5.1 | In voice, press PS and open a game from the home screen (it starts in front; AKENO Native goes to the background) | Report what happens: does the friend still hear you / you hear them? After how long does the friend see you leave? |
| V5.2 | Return to AKENO Native | Report: still in voice, reconnecting, or stopped |

Voice during games is **not claimed**; V5 only collects evidence for the next milestone.

## What to send back

1. The results table and photos 📷.
2. `/data/akeno-native/native-report.txt` (section **[Voice]**: microphone state and code, rate,
   packets sent/received/concealed, levels, round trip) and `native.log` (lines `voice.*`, every
   2 s a `voice.stats` line).
3. From `/data/akeno/akeno.log`: the lines starting with `native.`.
4. If you run the gateway: its log lines for your session (`console voice session …`).

Never send `device.json`, `instance.key` or `native.key`.

## Rollback

AKENO Native: replace `/data/homebrew/AKENONative/` with
`fallback/native-1.0.0-beta.1-238aec4dbb0e/AKENONative/` (no voice, with its own daemon in that
folder) or `fallback/native-0.1-d094e62bcaf1/AKENONative/` (hardware-tested). Daemon: Exit AKENO,
send `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf`. Server: leave `VOICE_GATEWAY_URL` empty
and stop the `voice-gateway` service; browsers' voice is unaffected.

## Results template

| Step | Result | Notes |
| --- | --- | --- |
| V0.1–V0.3 | | |
| V1.1–V1.5 microphone diagnostic | | microphone state / code / rate: |
| V2.1–V2.5 listen | | |
| V3.1–V3.8 talk | | DualSense mic: · headset: · delay: · echo: |
| V4.1–V4.6 network, cleanup | | |
| V5.1–V5.2 in-game (observation) | | |

The 1.0.0-beta.1 plan (B1–B10, `docs/HARDWARE_TEST_PLAN_1.0.md` in the package) stays valid for the
rest of the app.
