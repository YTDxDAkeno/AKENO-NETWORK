# AKENO NETWORK — Alpha 0.4 (release candidate)

**Status: release candidate, not deployed.** Builds on Alpha 0.3 (also not deployed yet). Voice
was tested end to end against a real LiveKit server in Chromium (see PROJECT_STATUS.md,
session 5 part 2); a real two-device test, phones, Firefox and Safari are still open. Voice stays
off until you enable it (see "Enabling voice"). PS5 voice is research only.

## New

- **Voice chat in parties.** Everyone in an AKENO party can talk from a PC or phone browser:
  press *Join voice* on the party page (your browser asks for the microphone once). You see who
  is in voice, who is speaking (green ring) and who is muted, plus your connection quality.
- **Controls:** mute, deafen (hear nobody), leave, per-person volume and *Mute for me* (only you
  stop hearing them), push-to-talk (a key on desktop, a hold button on phones), voice activity
  with an adjustable sensitivity, or an open microphone.
- **Voice stays connected while you browse** AKENO: a voice bar in the sidebar (above the bottom
  navigation on phones) shows the party, who is speaking, mute and leave.
- **Join from an invitation:** party invitations offer *Join voice* to accept and connect in
  one tap.
- **Settings → Voice & Audio:** microphone and speaker choice (where the browser allows),
  input and output volume, voice mode and sensitivity shown on a live level meter, push-to-talk
  key, a microphone test (optionally hearing yourself), echo cancellation, noise suppression,
  automatic gain control, a test sound and *Disconnect*. Settings stay in your browser.
- **Reliable reconnection:** short network drops are bridged; after a longer outage the app
  reconnects with a fresh authorization and tells you what is happening. Being removed from the
  party, the party ending, or joining from another tab are shown as such.

## Security and privacy

- **Nothing is recorded.** Audio goes from your browser to the party's voice room on your own
  server (LiveKit) and on to the other members; it never passes through the AKENO app and is
  never stored, transcribed or analysed.
- Only party members can join a party's voice room, with a join pass from the AKENO server that
  is valid for that room only and expires within minutes. Leaving or being removed from the
  party, the party closing, a block, suspension, account deletion or signing out ends access at
  once; the server re-checks every join and every 30 seconds.
- Blocked users never share a voice room; non-members see nothing about who is in voice.
- Voice is encrypted between each browser and your server (WebRTC). Like messages, it is not
  end-to-end encrypted.

Details: VOICE.md, SECURITY.md.

## Enabling voice (operator)

Voice needs the LiveKit container (`voice` Compose profile) and two firewall openings
(**7881/tcp, 7882/udp**). Nothing changes until you configure it:

1. Inspect the VPS (CPU, memory, disk, traffic allowance, existing services) — checklist in
   VOICE.md. Measured: an 8-person party with everyone talking used ~12 % of one CPU core and
   74 MB in the LiveKit container, ~1.7 Mbit/s server upload in that worst case.
2. Generate `LIVEKIT_API_KEY`/`LIVEKIT_API_SECRET`, set `LIVEKIT_URL=wss://<domain>/livekit`
   (and `LIVEKIT_NODE_IP` if needed) in `.env`.
3. Open 7881/tcp and 7882/udp in the provider firewall and `ufw` — **your decision; nothing in
   this release changes the firewall.**
4. `docker compose --profile https --profile voice up -d`, then check
   `GET /api/v1/voice/status` and a two-person call.

Without these steps parties show "Voice chat is not set up on this server" and everything else
works as before.

## Upgrading

No database migration (voice state is kept in memory; voice preferences in the browser).
Upgrading from Alpha 0.2 means upgrading through Alpha 0.3's migration `0004`; follow
RELEASE_NOTES_ALPHA_0.3.md → Upgrading (backup, upgrade check against a copy of the production
data, deploy). The voice profile can be enabled later, independently.

## PS5

- The release console app is unchanged by this release (its compiled code is byte-identical
  to Alpha 0.3's; only the console page script carries a dormant section).
- **Experimental audio lab** (separate ELF, never the release): a read-only check of the
  console's audio libraries and a confirmed one-second test tone — research for future PS5
  voice. **It has never run on a PS5** and needs your approval before any test.
- Microphone capture on the PS5 is not possible with the available SDK; PS5 voice in parties is
  not available. Full findings: PS5_AUDIO_RESEARCH.md.

## Known limitations

- Voice is verified in Chromium only (automated, real LiveKit server); not yet on a real phone,
  in Firefox or Safari, through TURN, or on the production network.
- iPhone/iPad: voice stops when the screen locks or Safari is in the background; per-person
  volume is not possible there (use *Mute for me*).
- Networks that only allow HTTPS on port 443 need TURN, which is not enabled yet.
- Push-to-talk keys work only while the AKENO tab is focused.
- Per-person volume goes up to 100 % (playback through media elements keeps echo cancellation
  working).
