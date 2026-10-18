# Hardware test plan — AKENO NETWORK Native 1.0.0-beta.1

For **your PS5 with firmware 12.20 (kstuff)**, the setup that ran Native 0.1 (`d094e62bcaf1-ps5`)
and the Alpha 0.5 RC daemon. **Nothing in 1.0.0-beta.1 has run on a PS5.** Native 0.2 was never
hardware-tested either, so this plan also covers what 0.2 introduced (friends, profiles,
activity through the daemon). Until you complete it, every statement about the console is a
PC-test result only.

Record ✅ / ❌ / what you saw for every step (template at the end); photograph the screens marked
📷. Have your phone or a PC with the AKENO website, and ideally a friend (or a second account on
the website) who can message you, invite you to a party and send a friend request.

## Before you start

1. **Approve this build.** Download **akeno-network-native-1.0.0-beta.1** from the "PS5 native
   client" workflow run of the commit you approved (or the draft GitHub release). Check
   `sha256sum -c SHA256SUMS` (Windows: `Get-FileHash <file> -Algorithm SHA256`).
2. Read `docs/INSTALL.md` (what is in the package, what changes) and keep `fallback/` on the PC.
3. Back up `/data/akeno/` to the PC.
4. **What changes compared with today**
   - The native-IPC daemon (protocol 2) is the Alpha 0.5 daemon plus the local interface. New in
     protocol 2: AKENO Native may make the changes on a fixed list (messages, read state, party
     create/join/leave/invite, accept/decline requests and invitations, notifications read, online
     status, three settings). Pairing, server, console name and the daemon's secrets cannot be
     changed through it. The daemon logs each change as `native.write <route>` (no IDs).
   - While a conversation is open in AKENO Native, the daemon does not show its system
     notification for that conversation (it shows all others).
   - AKENO Native writes only `/data/akeno-native/`.
   - No title is registered, no system file changed, no microphone opened, nothing auto-starts.
5. Emergency: PS button → close AKENO Native; if the console does not respond, hold power until it
   beeps twice; then **Rollback** below.

## B1 — Daemon (native-IPC, protocol 2)

| # | Step | Expected |
| --- | --- | --- |
| B1.1 | Page → **Exit AKENO** (confirm) | "AKENO NETWORK stopped." |
| B1.2 | Send `daemon/akeno-network-ps5-native-ipc.elf` with your ELF loader | "AKENO NETWORK Alpha 0.5 started."; the page shows your account 📷 |
| B1.3 | Page: Friends, Messages, a profile | Work as with the release daemon |
| B1.4 | A friend messages you (AKENO Native closed) | The daemon's notification appears |

## B2 — Install and start

| # | Step | Expected |
| --- | --- | --- |
| B2.1 | Replace `/data/homebrew/AKENONative/` with the package's `AKENONative` | three files |
| B2.2 | Open the launcher | **AKENO NETWORK — Native 1.0.0-beta.1** 📷 |
| B2.3 | Start it (home screen, no game) | Home in full screen; pill **BETA · 1.0**; "Welcome back, <name>"; AKENO SOCIAL: **Live from the AKENO daemon on this console**; seven tiles; bottom right "Native 1.0.0-beta.1 · build <id>-ps5 · 12.20" 📷 |
| B2.4 | Background protection: send `akeno-native.elf` with the ELF loader while the launcher copy is **not** running | One notification, no screen; `native.log` says background (as N1 in the 0.1 plan) |

## B3 — Friends, profiles, activity (0.2 functions, never hardware-tested)

| # | Step | Expected |
| --- | --- | --- |
| B3.1 | ✕ **Friends** | Same friends and presence as the website; online first 📷 |
| B3.2 | A friend goes online/offline or starts a game | Row changes within ~2 s without pressing anything |
| B3.3 | ✕ on a friend; ◯ | Their profile 📷; back on the same row |
| B3.4 | Home → **Profile** | Your profile |
| B3.5 | Home → **Activity** | This console's game state, your recent games, friends playing now 📷 |
| B3.6 | Pictures | Avatars/banners where the server has them; initials otherwise |

## B4 — Artwork

| # | Step | Expected |
| --- | --- | --- |
| B4.1 | A game in your recent games that **is not installed** on this console but has a cover on the website | The same cover in Activity / Profile 📷 |
| B4.2 | An installed game without a server cover | Its installed icon |
| B4.3 | A game with neither | A neutral placeholder — never another game's cover |

## B5 — Controller keyboard and messages

| # | Step | Expected |
| --- | --- | --- |
| B5.1 | Home → **Messages** | Your conversations (direct, groups, party) with unread counts, as on the website 📷 |
| B5.2 | Open a direct conversation | History, newest at the bottom; unread count clears on the website too |
| B5.3 | ✕ → keyboard; type "Hello from PS5 1" with ✕, △ (space), L1 (Shift), R1 (symbols), □ (delete), L2/R2 (cursor) | Each key does what `docs/CONTROLLER.md` says 📷 |
| B5.4 | OPTIONS | The message appears once in the chat and **once** on the website |
| B5.5 | Type é, ü, € from the symbols | Shown correctly here and on the website |
| B5.6 | ◯ with text typed; ✕ again | The draft is still there |
| B5.7 | Friend replies while the chat is open | The reply appears within ~2 s; **no** system notification for this chat |
| B5.8 | Friend writes in **another** conversation | The daemon's system notification appears |
| B5.9 | Leave the chat, wait ~30 s, friend writes again | The system notification appears again |
| B5.10 | Long history: up at the top / L1 | "Loading older messages..." then older ones |
| B5.11 | Friends → □ on a friend without a conversation | A new conversation opens; a message arrives on the website |
| B5.12 | Group chat on the website; open it here and write | Message arrives in the group |
| B5.13 | Airplane test (optional): pull the network cable, send, plug it back, △ | "Not sent · Triangle: try again", then sent **once** |

## B6 — Party

| # | Step | Expected |
| --- | --- | --- |
| B6.1 | Home → **Party** → Create a party | Party with you as LEADER; visible on the website 📷 |
| B6.2 | Invite friends → pick a friend | They receive the invitation (website) |
| B6.3 | Friend joins on the website | Member appears; "2 IN PARTY" badge on Home |
| B6.4 | Party chat: write | Arrives in the party chat on the website |
| B6.5 | VOICE panel | Says AKENO Native has no voice yet; shows members in voice on other devices. **No microphone, no sound from the party** 📷 |
| B6.6 | Leave the party (confirm) | "You left the party."; website agrees |
| B6.7 | Friend invites you to their party | Invitation on Party and Inbox; ✕ accept → you are a member |
| B6.8 | Another invitation; □ decline (confirm) | It disappears here and on the website |

## B7 — Inbox, requests, notifications

| # | Step | Expected |
| --- | --- | --- |
| B7.1 | Someone sends you a friend request | Inbox badge on Home; FRIEND REQUEST row |
| B7.2 | ✕ accept | They are in Friends; website agrees |
| B7.3 | A second request; □ decline (confirm) | Gone here and on the website |
| B7.4 | Mark notifications as read | Count goes to 0 here and on the website |

## B8 — Settings

| # | Step | Expected |
| --- | --- | --- |
| B8.1 | Home → **Settings** | Toggles show the same values as the AKENO page; NOT CHANGED HERE names pairing, server address, console name, profile, privacy and account 📷 |
| B8.2 | Message notifications off; friend messages you (AKENO Native closed) | No notification; turn it on again → notification |
| B8.3 | Online status → Away (✕), Busy, Invisible, Online (□ goes back) | The website shows each status |
| B8.4 | Share the game I'm playing: off, then on | The AKENO page shows the same value |

## B9 — Platform checks (still open since 0.1)

| # | Step | Expected |
| --- | --- | --- |
| B9.1 | Controller test: every button, both sticks, L2/R2, touchpad; hold ◯ 1 s | Each lights up; hold ◯ returns |
| B9.2 | One audio test | Heard |
| B9.3 | Connection | "Native interface: Connected … protocol 2" 📷 |
| B9.4 | Settings → System → Save report; Home → Exit → Exit | Back to the PS5; starting again works |

## B10 — Stability

| # | Step | Expected |
| --- | --- | --- |
| B10.1 | Leave AKENO Native open 15 minutes with a friend chatting and changing presence | No freeze, no slowdown; lists still update |
| B10.2 | Rest mode with AKENO Native closed, wake up, start it | Works; daemon reconnects (Connection) |
| B10.3 | With the **release** daemon: start AKENO Native | Social screens explain the missing interface; checks work |

## What to send back

1. The table below and the 📷 photos.
2. `/data/akeno-native/native-report.txt` and `native.log` (states and counts, no other people's
   names or texts).
3. From `/data/akeno/akeno.log`: the lines starting with `native.`. Never send `device.json`,
   `instance.key` or `native.key`.

## Rollback

`docs/ROLLBACK.md`: Native 0.1 from `fallback/native-0.1-d094e62bcaf1/AKENONative/` (hardware-tested);
daemon: Exit AKENO, send `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf` (hardware-tested).

## Results template

| Step | Result | Notes |
| --- | --- | --- |
| B1.1–B1.4 | | |
| B2.1–B2.4 | | |
| B3.1–B3.6 | | |
| B4.1–B4.3 | | |
| B5.1–B5.13 | | |
| B6.1–B6.8 | | |
| B7.1–B7.4 | | |
| B8.1–B8.4 | | |
| B9.1–B9.4 | | |
| B10.1–B10.3 | | |

The experimental In-Game Companion has its own proof (`docs/IN_GAME_ACCESS.md`) and needs your
separate decision about OnionHEN; it is not part of this plan.
