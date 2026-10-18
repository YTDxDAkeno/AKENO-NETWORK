# Troubleshooting — AKENO NETWORK Native 1.0.0-beta.1

Always note the **build ID** (Home, bottom right, or Settings → System) and keep
`/data/akeno-native/native-report.txt` and `native.log`. They contain states and counts, never
other people's names, IDs or message texts, and never keys or tokens.

| What you see | Why | What to do |
| --- | --- | --- |
| The launcher does not list AKENO NETWORK | folder not at `/data/homebrew/AKENONative/` or `homebrew.js` missing | copy the whole `AKENONative` folder again |
| Starting it closes your game | the launcher starts homebrew in the foreground | expected; see `docs/IN_GAME_ACCESS.md` for the experimental in-game route |
| One notification, then nothing (sent with the ELF loader) | the background protection: started from an ELF loader it opens no screen | start it from the launcher |
| "The AKENO daemon is not running" | nothing answers on 127.0.0.1:8642 | send `daemon/akeno-network-ps5-native-ipc.elf` |
| "This AKENO daemon has no native interface" | the release daemon runs | Exit AKENO on its page; send the native-IPC daemon |
| "The daemon refused the native key" | `native.key` missing, not private, or from an earlier start | Exit AKENO and send the daemon again (it writes a new key) |
| "This console is not paired. Pair it on the AKENO page first." | the daemon has no device token | pair on the AKENO page (AKENO Native never pairs) |
| "The AKENO daemon does not allow this from AKENO Native." | a change outside the daemon's write list (for example an older IPC-1 daemon) | use the native-IPC daemon from this package |
| Social screens read only, no Write/Accept | the daemon speaks protocol 1 (Native 0.2's daemon) | send this package's daemon |
| A message shows "Not sent · Triangle: try again" | the daemon or the server did not answer | △ in the chat sends the failed messages again with the same message ID, so the server never stores one twice |
| "AKENO Native is still busy with earlier changes." | 16 changes are already waiting | wait a moment |
| Duplicate notification for the open chat | the daemon is not the native-IPC build of this package | send this package's daemon |
| No cover for a game | the server has no administrator-verified mapping for its title ID and the game is not installed | expected (AKENO never guesses covers); an administrator can add one in the admin area |
| "Port 8642 is used by another program" (daemon start) | a daemon still runs | Exit AKENO, then send again |
| Controller banner "No controller" | the DualSense is off or assigned to another user | press the PS button; Control Center → Accessories |
| The screen freezes | | PS button → close AKENO Native; if the console does not respond, hold power until it beeps twice; send the logs |

## Logs

| File | Written by | Holds |
| --- | --- | --- |
| `/data/akeno-native/native.log` | AKENO Native | start, platform, screens, request results (route kinds, status codes), errors; redacted |
| `/data/akeno-native/native-report.txt` | AKENO Native (on exit and on "Save report") | build, firmware, controller and audio results, connection, counts (friends, messages, party, inbox), "Requests: N (W writes, F failed)" |
| `/data/akeno/akeno.log` | the daemon | `native.ready`, `native.hello`, `native.write <route>` (route patterns only, no IDs) |

Leave `device.json`, `instance.key` and `native.key` on the console; never send them to anyone.
