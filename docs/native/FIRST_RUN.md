# First run — AKENO NETWORK Native 1.0.0-beta.1

After [INSTALL.md](INSTALL.md). Start AKENO Native from the homebrew launcher with the native-IPC
daemon running.

## Home

- Top left: AKENO NETWORK and the pill **BETA · 1.0**. Top right: the controller state and your
  `@online ID`.
- "Welcome back, <your name>" and "signed in through this console's AKENO pairing" — AKENO Native
  uses the console's existing pairing; there is no separate login.
- **AKENO SOCIAL — Live from the AKENO daemon on this console**: Friends, Messages, Party, Inbox,
  Profile, Activity, Settings. Badges show live counts ("3 ONLINE", "2 NEW", "IN PARTY",
  invitations, "PLAYING").
- **NATIVE PLATFORM CHECKS**: Controller test, Audio test, Connection, System, Exit.
- Bottom right: version, build ID and firmware. Use them in every report.

If the social line says something else instead:

| Line | Meaning | What to do |
| --- | --- | --- |
| Connecting to the AKENO daemon... | first contact | wait a few seconds |
| The AKENO daemon is not running | nothing answers on 127.0.0.1:8642 | send the native-IPC daemon with your ELF loader |
| This AKENO daemon has no native interface | the release daemon runs | Exit AKENO on its page, send `daemon/akeno-network-ps5-native-ipc.elf` |
| The daemon refused the native key | key file missing, wrong mode, or from an older daemon start | restart the daemon (Exit AKENO, send it again) |

## A first tour

1. **Friends** — your friends as on the website: online first, with their game. ✕ opens a profile,
   □ writes to that friend.
2. **Messages** — direct chats, your groups and your party chat, newest first, unread counts.
   ✕ opens one; ✕ in a chat opens the keyboard ([CONTROLLER.md](CONTROLLER.md)); OPTIONS sends.
   While a chat is open, the daemon does not show a system notification for that chat (it does for
   every other one); it shows them again about 20 seconds after you leave it.
3. **Party** — create a party, invite friends, write in the party chat, accept or decline
   invitations, leave (with confirmation). The VOICE panel says honestly that AKENO Native has no
   voice of its own yet; it shows who is in voice on other devices.
4. **Inbox** — friend requests (✕ accept, □ decline with confirmation), invitations and
   notifications ("Mark notifications as read").
5. **Profile / Activity** — your profile, this console's detected game, your recent games and
   friends playing now. Game covers come from the server's administrator-verified mapping, so a
   game that is not installed still shows its cover; otherwise the installed icon; otherwise a
   neutral placeholder. AKENO never guesses a cover from a game's name.
6. **Settings** — message notifications, friend and invitation notifications, sharing the game you
   play, your online status (Online, Away, Busy, Invisible), and the platform checks. Shown under
   **NOT CHANGED HERE**: the server, pairing, console name and the daemon's own settings stay on
   the AKENO page.

## What AKENO Native writes to the server

Only through the daemon, only from this fixed list (the daemon refuses everything else, the server
checks the console's permissions again): open a direct conversation, send a message, mark read,
create/join/leave a party, invite to a party, accept/decline friend requests and invitations, mark
notifications read, set your online status, and three settings (message notifications, social
notifications, game activity). Each message carries a unique ID, so a retry never sends it twice.

## Exit

Home → **Exit** → Exit (or ◯ on Home). A diagnostic report is saved on the way out
(`/data/akeno-native/native-report.txt`). You return to the PS5 home screen.
