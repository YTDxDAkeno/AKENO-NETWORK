# Automatic game activity and playtime

AKENO NETWORK shows friends which game you are playing, Discord-style, and keeps a tracked
history: last played, last session, total tracked playtime. On a PS5 the AKENO console app
detects the game itself; you never type a game name.

> **Hardware status:** game detection has been tested on Linux with a simulated process list
> and a real AKENO server. It has **not** run on a PS5 yet (Alpha 0.2 hardware test pending,
> see [HARDWARE_TEST_PLAN_ALPHA_0.2.md](HARDWARE_TEST_PLAN_ALPHA_0.2.md)).

## How it works

```
 console app (activity worker)                     AKENO server
 ┌────────────────────────────────────┐            ┌──────────────────────────────────────┐
 │ every 10 s: list processes (read-only)          │ POST /activity/heartbeat              │
 │   sysctl(KERN_PROC_PROC)            │  heartbeat │   one session per user and title      │
 │   sceKernelGetAppInfo(pid) → raw    │ ─────────► │   server timestamps only              │
 │ classify → exactly one game?        │  every 60 s│ POST /activity/stop                   │
 │ debounce: 2 scans agree             │            │ no heartbeat for 180 s → session ends │
 │ name from the console's param files │            │   at its last heartbeat               │
 └────────────────────────────────────┘            │ presence.updated → friends (privacy)  │
                                                   └──────────────────────────────────────┘
```

### Detection on the console (`clients/ps5/app/game_detect.c`, `activity.c`, `platform_ps5.c`)

1. **Process list.** `sysctl(KERN_PROC_PROC)` lists processes, exactly as the SDK's `samples/ps`.
   For each process, `sceKernelGetAppInfo(pid, buf)` is called with a **zeroed 512-byte buffer**.
   Both are read-only. AKENO never attaches to, injects into, hooks or writes to another process.
2. **Undocumented layout, resolved by the console itself.** The two SDK samples disagree on where
   the title ID is: offset 20 (`samples/ps`, with an `app_type` field before it) or offset 16
   (`samples/test_privileges`). AKENO does not guess. It decodes both offsets and accepts only a
   string shaped like a title ID (`[A-Z]{4}[0-9]{5}` followed by a NUL byte). A real title ID can
   match at only one of the two offsets (one starts four bytes into the other, where digits
   would have to be letters), so every scan **votes**:
   - only offset 20 matches → layout of `samples/ps`;
   - only offset 16 matches → layout of `samples/test_privileges`;
   - both match in different processes → **conflict: nothing is reported** (`unverified-layout`);
   - neither → nothing to decide from; no game.

   Diagnostics shows the votes and the first 32 raw bytes of up to four processes, so the first
   hardware test settles the question with evidence.
3. **Classification.** A game is a title ID starting with `PPSA` (PS5) or `CUSA` (PS4) followed by
   five digits. System applications (`NPXS…`) never count. Stopped processes (suspended, zombie)
   are ignored. **Exactly one** distinct game must have running processes; with two or more the
   scan is *ambiguous* and nothing is reported. A failed or truncated process list reports
   nothing.
4. **Debounce.** A change (start, switch, end) is accepted only after **two consecutive scans
   agree**, so short-lived processes, listing glitches and fast switches do not create sessions.
5. **Name.** Read from the console's own metadata, read-only and size-limited, in this order:
   `/system_data/priv/appmeta/<id>/param.json|.sfo`, `/user/appmeta/<id>/param.json|.sfo`,
   `/mnt/sandbox/pfsmnt/<id>-app0/sce_sys/param.json|.sfo`. The file must name the same title ID;
   text is cleaned to valid UTF-8 and cut at 128 bytes. Which paths a payload can read is a
   hardware question; if none works, friends see the title ID (an administrator can give it a
   name on the server, see below).

### What "playing" means — the foreground limitation

AKENO reports that **a game process is running**, not that the game is on screen. The SDK has a
`sceSystemServiceGetAppFocusedAppStatus` stub, but no sample, no header and no verified
structure; calling it would mean guessing a firmware-specific layout, which this project does not
do. Consequences: a game left running in the background (for example while you use the web
browser) still counts as "playing". If the system stops a suspended game's processes (process
state `SSTOP`), AKENO ignores them; whether and when the PS5 does this is part of the hardware
test. The page and the website say "detected" and "running", never "in focus".

### Reporting (`apps/server/src/modules/activity/service.ts`)

- `POST /activity/heartbeat {titleId, titleName?, platform:"PS5"}` — immediately on a new game or
  a switch, then every 60 s. Only **paired devices with the `activity:write` permission**; web
  sessions get `DEVICE_ONLY`.
- The server owns every timestamp. A user has at most one running session per title: two consoles
  reporting the same game share one session and nothing is counted twice.
- `POST /activity/stop` when the game ends or the app exits (only the device that sent the last
  heartbeat can stop a shared session).
- **Only confirmed time counts.** A session without a heartbeat for 180 s ends **at its last
  heartbeat** (sweeper every 30 s, plus checks on every read). Time before AKENO started tracking
  is never estimated; the history says "tracked since …".

| Situation | What happens |
| --- | --- |
| Game starts / closes | Session created after 2 scans; stop sent after 2 scans without it (`stopped`) |
| Game switch | Old session ends (`switched`), new one starts |
| Two games detected | Nothing reported; a reported game ends after debouncing |
| Rest mode, crash, power loss, AKENO killed | No more heartbeats: the session ends at the last heartbeat (`stale`), nothing extra counted |
| Network outage | Heartbeats retried every 15 s; if the gap exceeds 180 s the old session ends at its last heartbeat and a new one starts |
| Server restart | Running sessions are reloaded from PostgreSQL; heartbeats continue them |
| Two consoles, same game | One shared session; different games on two consoles are separate sessions |
| Console unpaired / permission removed | Its sessions end at their last heartbeat (`device_revoked`) |
| Tracking turned off on the website | Running session ends (`tracking_disabled`); new reports refused (409), the console shows "turned off on the website" and retries every 5 min |
| Game excluded on the website | Running session of it ends (`excluded`); reports of it refused (409 `ACTIVITY_TITLE_EXCLUDED`), the console shows "not shared" |
| Turned off on the console | The console sends a stop and no further heartbeats |
| Credential expired or revoked | 401: the console stops reporting and asks to pair again |

## Privacy

Set on the website (Settings → Privacy); enforced by the server on every path (REST, realtime,
profile, friend list):

| Setting | Effect |
| --- | --- |
| Share the game I am playing (default on) | Off: no tracking at all; running session ends |
| Who can see what you are playing (default friends) | Everyone / friends / nobody. Never while you appear offline or **invisible**, never across blocks |
| Who can see your play history (default friends) | Recent games, last played, totals |
| Show my playtime (default on) | Off: others see which games and when, but durations and totals are `null` ("hidden") |
| Excluded games (title IDs, up to 50) | Never tracked or shown as current; earlier sessions hidden from everyone but you |
| Invisible status | Sessions reported while invisible are marked **private** and never appear in anyone else's history, also later |
| Clear play history | Deletes all tracked sessions |

On the console: Settings → "Share the game I am playing from this console" is a local off switch.

### Permission and reauthorization

Game activity needs the device permission `activity:write`, shown as a checkbox when you approve
a pairing. **Consoles paired with Alpha 0.1 do not have it**, and it is never added silently.
Allow it on the website: Settings → Security → Paired devices → **Allow game activity**
(`PATCH /devices/:id`, owner's web session only — a device can never change its own permissions).
The console notices within about 5 minutes; no new pairing is needed.

## Names and artwork

Curated names set by an administrator (`PUT /admin/game-titles/:titleId`) override names reported
by consoles. AKENO does not download game artwork: it shows a generated icon (initials on a
gradient), so there are no broken thumbnails and no licensing questions. Artwork can be added
later only from a source whose license allows it.

## API summary

| Route | Who | Purpose |
| --- | --- | --- |
| `POST /activity/heartbeat`, `POST /activity/stop` | paired device with `activity:write` | Report the running game |
| `GET /users/:handle/activity` | signed in (devices: `profile:read`) | Current game, recent games, recent sessions, tracked since — privacy applied |
| `DELETE /activity/history` | owner, website | Clear the history |
| `PATCH /devices/:id {scopes}` | owner, website | Grant or remove device permissions |
| `PUT/DELETE /admin/game-titles/:titleId` | administrator | Curated names |

Realtime: friends receive `presence.updated` (with `game`), the owner's devices `activity.self`.

## Tests

- C unit tests (`tests/unit/test_game.c`): title IDs, raw parsing at both offsets, layout votes
  incl. conflict, classification, debouncing, names from `param.json`/`param.sfo`, hostile files.
- Server (`apps/server/test/activity.test.ts`): sessions, switching, stale sessions, multiple
  devices, permissions, input validation, audiences, invisible/private sessions, blocks,
  tracking off, excluded games, hidden playtime, reauthorization, unpairing, restart.
- Console app end to end (`clients/ps5/tests/integration/run.mjs`, simulated process list, real
  server): permission granted later, start/switch/ambiguity/suspended/close, conflicting and
  alternative layouts, listing failure, crash and restart, console and website switches,
  excluded game, exit.
- Website (`apps/web/tests/e2e/game-activity.spec.ts`): live friend list and profile, privacy,
  reauthorizing an older console, hidden playtime, excluded games.
