# Alpha 0.2 hardware test plan

Run this only after you have **approved the exact build** (SHA-256 from `SHA256SUMS` of the
"PS5 console build" artifact `akeno-network-ps5-alpha-0.2-rc`). Nothing is sent to a console
automatically. Use the console that ran Alpha 0.1 (firmware 12.20) first; 13.60 only after 12.20
passes.

**Before you start**

- [ ] Back up anything important on the console; be ready to reboot it.
- [ ] Note: firmware, kstuff version, loader name and version.
- [ ] The AKENO server runs this revision (game activity needs it). Without a server update,
      everything except game activity and the new privacy options still works.
- [ ] Keep `fallback-alpha-0.1/akeno-network-ps5-alpha-0.1.elf` at hand. If anything below
      breaks the basics (A1–A4), stop, reboot, and use Alpha 0.1 again.
- [ ] Have a friend account (or a second account in a PC browser) ready to send messages.

Stop AKENO before switching builds: **Exit AKENO** in the page, or reboot.

## A. The Alpha 0.1 basics still work (must all pass)

| # | Step | Expected |
| --- | --- | --- |
| A1 | Send `akeno-network-ps5.elf` with your loader | Notification "AKENO NETWORK Alpha 0.2 started."; the browser opens the page (badge "Alpha 0.2") |
| A2 | If the console was paired with Alpha 0.1 | It is still paired (same `/data/akeno`); Friends load; "Live" is green |
| A3 | Have a friend send you a message (page open, then page closed) | Native notification "AKENO: <name>: <text>" both times |
| A4 | Diagnostics → Test connection | "OK … TLS …", firmware 12.20 |

## B. Change server (Alpha 0.1 bug)

| # | Step | Expected |
| --- | --- | --- |
| B1 | Only if unpaired: pairing screen → **Change server**, type a few characters, wait 10 s | The form stays open and keeps the text |
| B2 | Press ◯ (or select Cancel) | Back to the pairing screen; the server address is unchanged |

## C. Game activity

| # | Step | Expected |
| --- | --- | --- |
| C1 | Paired with Alpha 0.1? On the website: Settings → Security → Paired devices → **Allow game activity** (shown only for consoles without the permission) | Confirmation "Permission granted." (If you start a game before this, the Games tab shows "Permission needed" and changes within about 5 minutes after you allow it.) |
| C2 | Diagnostics → Game detection, with no game running | Note the **App info layout** line (offset 20 or 16 votes), the processes count and the **Raw probes** (copy them into the report) |
| C3 | Start a PS5 game (e.g. Baldur's Gate 3, PPSA…) and wait 30 s | Games tab: "Now playing <name or title ID>"; on the website your friend sees "Playing … · PlayStation 5 · <minutes>" without reloading |
| C4 | Is the **name** shown (not just PPSA…)? | Report yes/no and the "from …" path in Diagnostics |
| C5 | Play 5+ minutes, then close the game, wait 30 s | Session ends; Recently played shows the game with a last session of about the time played |
| C6 | Start a PS4 game (CUSA…) if you have one | Detected as well |
| C7 | Switch from one game to another | The first ends ("switched"), the second appears |
| C8 | Put the console in rest mode while a game runs, wake it after 5 min | The session ended at the last heartbeat (no rest-mode time counted); after waking, a new session starts if the game resumes |
| C9 | Leave a game running and open the AKENO page via the browser | Report: does the game stay "playing" or does it disappear? (foreground limitation) |
| C10 | Website → Privacy: exclude the running game (or hide playtime) | Games tab shows "Not shared"; friends no longer see it |

## D. Getting to a message while playing

| # | Step | Expected |
| --- | --- | --- |
| D1 | In a game, have a friend message you | Native notification appears over the game (proven in Alpha 0.1) |
| D2 | Use the PS button / app switcher to go to the browser (if it is still open) | The page shows "New message from …" with **Open** focused; ✕ opens the conversation |
| D3 | Reply with the on-screen keyboard; while typing, have the friend send another message | Your half-typed text stays; the reply is sent with ✕/Enter |
| D4 | Return to the game | Report: does the game continue, or was it closed? |
| D5 | With the browser closed, send the ELF again | The page opens **directly at that conversation** (no second copy starts) |

## E. Experimental button notification (optional)

| # | Step | Expected / report |
| --- | --- | --- |
| E1 | Settings → turn on "Experimental: … 'Open in AKENO' button" | — |
| E2 | Diagnostics → **Send a test notification with a button** | Report exactly: did a toast appear? Did it show a button? What happened when you selected it? Diagnostics "Status" text? |
| E3 | Have a friend message you (out of a game, then in a game) | Same questions. If no toast appears at all, turn the switch off: plain notifications return |

## F. Stability

| # | Step | Expected |
| --- | --- | --- |
| F1 | Leave AKENO running 1 hour with a game, then Exit AKENO | No crash; "AKENO NETWORK stopped."; the session ends |
| F2 | Pull the network cable / disable Wi-Fi for 5 min while a game runs, then reconnect | "Live" recovers; the old session ended at its last heartbeat and a new one starts |

## Report

Send: the table results (pass / fail / what you saw), firmware, kstuff and loader versions, the
SHA-256 you tested, and `/data/akeno/akeno.log` (via FTP; it contains no tokens). The results go
into FIRMWARE_COMPATIBILITY.md; nothing is marked verified before that.
