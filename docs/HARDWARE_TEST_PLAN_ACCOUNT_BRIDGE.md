# Account Bridge hardware test plan (read-only, firmware 12.20)

This plan runs the **opt-in, read-only** Account Bridge check on your console for the first time.
It is the technically justified next step toward native Remote Play on 12.20 (see
[PS5_ACCOUNT_BRIDGE.md](PS5_ACCOUNT_BRIDGE.md) §5): it answers, read-only, whether the registry and
Remote Play reads work on 12.20, whether your current local user has an account ID, and whether
Remote Play is enabled.

**Nothing here writes to the console, activates a user, creates an account ID, or calls a Remote
Play function.** No account registry entry is changed; no Sony service is contacted. The account ID
is reduced to present/absent inside the console and is never stored, shown or sent. The check runs
only when you press the button.

Run this only after you have **approved the exact build** (the SHA-256 from `SHA256SUMS` of the
"PS5 console build" artifact for this branch). Nothing is sent to a console automatically. Use the
console that already runs the release app on firmware 12.20.

## Before you start

- [ ] Back up anything important on the console; be ready to reboot it. (The check only reads, but
      treat every first hardware run as able to fail.)
- [ ] Note: firmware, kstuff version, loader name and version.
- [ ] Pair the console with your AKENO account first if it is not already paired
      ([DEVICE_PAIRING.md](DEVICE_PAIRING.md)); the link step needs a paired device.
- [ ] Select on the console the **local user** you want to check (the one you normally use).
- [ ] Keep `fallback-alpha-0.1/akeno-network-ps5-alpha-0.1.elf` at hand. If the app stops behaving,
      stop, reboot, and use the fallback.
- [ ] You do **not** need a second account for this plan.

Stop AKENO before switching builds: **Exit AKENO** in the page, or reboot.

## A. The app still works (must all pass first)

| # | Step | Expected |
| --- | --- | --- |
| A1 | Send the approved `akeno-network-ps5.elf` with your loader | Start notification; the browser opens the page |
| A2 | The console is still paired | Friends load; "Live" is green |
| A3 | Open **Diagnostics** (bottom of the page, or the Diagnostics button on the pairing screen) | The page loads; you see an **Account Bridge (read-only)** section |

If A1–A3 do not pass, stop and use the fallback; do not continue.

## B. The link (no console write)

| # | Step | Expected / report |
| --- | --- | --- |
| B1 | In **Account Bridge (read-only)**, read the **Local user** line | Shows your selected user's name and a numeric id, labelled "not verified by Sony". Report exactly what it shows (name resolved yes/no). |
| B2 | Press **Link this local user** | **Link** becomes `linked`. On the website, Settings → Security → Paired devices shows the user under this device, labelled "local, not verified by Sony". Report yes/no. |
| B3 | On the website, press **Unlink**, then on the console press **Link this local user** again | The label disappears, then reappears. (Confirms the link is removable and reversible.) |

## C. The opt-in read-only account check

This is the first time these reads run on a console. Each returns read-only data; copy the lines
into the report verbatim.

| # | Step | Expected / report |
| --- | --- | --- |
| C1 | From the console **home screen** (not during a game), press **Run read-only account check** | It finishes in a second or two and shows an **Account ID** line and a **Remote Play** line. Report both exactly. |
| C2 | Read the **Account ID** line | One of: `account_id_present`, `no_account_id`, `check_failed`. Report which. |
| C3 | Read the **Remote Play** line (the assessment) | One of: `prerequisites-present`, `blocked-no-account-id`, `blocked-setting-off`, `blocked-no-library`, `presence-only`, `incomplete`. Report the code and the sentence after it. |
| C4 | Scroll to **Recent log** in Diagnostics | Copy the `bridge.*` lines (dlopen / dlsym / read / call). They show exactly which libraries loaded and which reads ran. Report them. |
| C5 | Press **Run read-only account check** a second time | Same result; no crash. (Confirms the reads are repeatable.) |

## D. Stability

| # | Step | Expected |
| --- | --- | --- |
| D1 | After the checks, use the page normally for a few minutes, then **Exit AKENO** | No crash; the app stops cleanly |
| D2 | If anything crashed or froze at any point | Stop, reboot, note the last `bridge.*` log line reached, and report it |

## What the result means (no action, just understanding)

- **`account_id_present` + `prerequisites-present`** — the local user already has an account ID and
  Remote Play is enabled. The next justified step would be a separate, still read-only Remote Play
  *status* probe (no PIN), decided separately.
- **`no_account_id` / `blocked-no-account-id`** — native Remote Play pairing cannot proceed without
  an account ID, and AKENO does not create one. Creating one is activation: a separate, higher-risk
  decision with its own prototype requirements (PS5_ACCOUNT_BRIDGE.md §6), not part of this work.
- **`blocked-setting-off`** — Remote Play is turned off in the console's own settings.
- **`blocked-no-library` / `incomplete` / `check_failed`** — a library or read did not resolve on
  12.20; the `bridge.*` log lines say which. This is itself a useful result to record.

Nothing above is a go-ahead to attempt pairing. Pairing (PIN generation, a client entering the ID)
stays a separate decision and is not justified until this read-only check confirms the
prerequisites hold on 12.20.

## Report

Send: the B/C/D results (what each line showed), the `bridge.*` log lines, firmware, kstuff and
loader versions, and the SHA-256 you tested. The results go into
[FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md); nothing is marked verified before that.
