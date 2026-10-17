# AKENO Account Bridge

The Account Bridge links the **local user selected on the PS5** to the **AKENO NETWORK account**
the console is already paired to, and offers an **opt-in, read-only** look at that user's account
ID and the console's Remote Play prerequisites. It writes nothing to the console and contacts no
Sony service.

This document covers what is built and tested, what is read-only diagnosis of unverified PS5
behaviour, and what a *separate* activation prototype would require. The research that led here is
in [PS5_ACCOUNT_BRIDGE_FEASIBILITY.md](PS5_ACCOUNT_BRIDGE_FEASIBILITY.md); the rules are in
[SAFETY.md](SAFETY.md).

Status labels, as elsewhere in the docs:
**VERIFIED ON HARDWARE** · **AUTOMATED TEST ONLY** · **SOURCE-VERIFIED** (read in source, not run)
· **UNKNOWN**.

## 1. What this is, and what it is not

| | |
| --- | --- |
| **Is** | A local, additive link between the console's selected user and the paired AKENO account. An optional read-only report of whether that user has an account ID and whether Remote Play is enabled. |
| **Is not** | A PSN sign-in. No Sony service is contacted, no credential is collected, no licence or online access is granted. The local user name is a display label, not identity. |
| **Does not** | Write to the account registry, create or change an account ID, activate a user, call any Remote Play function, or claim that Remote Play pairing or gameplay streaming works. |

The paired-device token from [DEVICE_PAIRING.md](DEVICE_PAIRING.md) (RFC 8628, tested on 12.20) is
the **only** proof of ownership. The bridge adds no second authentication system and no new table.

## 2. The link (working)

**Status: AUTOMATED TEST ONLY** (server unit tests, web e2e, client integration test; not yet on
hardware, because the console reads in §3 have not run on a console — the link itself is pure
server/web work).

The link is five nullable columns on the existing `device_credentials` row, so it reuses the
record the device token already lives on — no new table, no duplicate auth:

| Column | Meaning |
| --- | --- |
| `console_user_id` | The local user ID the PS5 reports (`sceUserServiceGetForegroundUser`). A plain integer, local to that console. |
| `console_user_name` | The local user name, cleaned to a safe label. Unverified; never a PSN identity. |
| `console_user_linked_at` | When the current user was linked. |
| `console_account_state` | The last read-only account state (see §3): `not_checked`, `no_account_id`, `account_id_present`, `check_failed`. |
| `console_account_checked_at` | When that state was last read. |

A database check constraint couples the first three all-or-none, and another limits the state to
those four values. The migration is additive and reversible
(`apps/server/src/database/migrations/0005_account_bridge.sql`), per the production-server rules in
SAFETY.md.

### API

All three routes are on the existing device/session auth; no new scope grants anything new.

- `PUT /devices/current/console-user` — the console links its selected user (device token; rate
  limited). Body: `{ "localUserId": number, "userName": string, "accountState"?: state }`.
  Re-linking the **same** user keeps the previous `linkedAt` and account state; linking a
  **different** user replaces it and resets the state to `not_checked`. Only a user change is
  audited (`device.console_user_linked`).
- `DELETE /devices/current/console-user` — the console removes the link (device token).
- `DELETE /devices/:id/console-user` — the website removes the link (session). Unpairing the device
  removes it too.

The website shows the linked user under the device, labelled **local, not verified by Sony**, with
an **Unlink** button (`apps/web/src/features/devices/PairedDevicesCard.tsx`).

## 3. The read-only account & Remote Play check (opt-in diagnosis)

**Status: SOURCE-VERIFIED (calls and registry keys) / UNKNOWN (12.20 outcome).** The console-side
code is built and its logic is unit-tested on Linux; the host simulation drives the integration
test. **No call in `platform_ps5_account.c` has run on a console in this project yet. The first run
on hardware is itself the verification.**

The check runs **only when the user starts it** (`POST /api/account-bridge/check` on the console's
local UI). It never runs automatically and is not part of the link unless the user opts in.

### What it does, in order

1. **Presence.** `dlopen` `libSceRegMgr.sprx` and `libSceRemoteplay.sprx`; `dlsym` the read
   functions and the Remote Play pairing names. This only reports whether each library and symbol
   exists. (`libSceUserService.sprx` is already mapped by the release ELF.)
2. **Read** (only if the registry read functions resolved), for the selected user:
   - scan registry slots 1–16 for the one whose user-ID entry equals the selected user, to find the
     slot;
   - read the account-ID entry and reduce it to **present / absent** — the value itself is never
     stored, logged or sent;
   - read the console-wide Remote Play setting and the per-user Remote Play value.
3. **Assess.** Turn the read-only facts into one of the codes below. It never says pairing works.

Every step is logged with the exact operation and flushed before and after the call, so if a call
ever hangs or crashes a console the last line names what ran (same pattern as the capture
diagnostic, [PS5_DIRECT_CAPTURE_RESEARCH.md](PS5_DIRECT_CAPTURE_RESEARCH.md)). Libraries are opened
with `dlopen` and **never** closed, because starting/unloading unverified modules is the risky
part, not looking up a symbol in an already-mapped one.

### Calls and registry keys (confirmed before use)

Per the owner's instruction — *confirm actual availability, calling convention, required privileges
and failure behaviour; do not call unverified functions merely because their symbols exist* — every
call was confirmed against at least two independent public sources plus the SDK stubs, and reduced
to read-only use. None requires elevated privileges beyond what the already-running AKENO process
has; each returns a non-zero code on failure, which the check treats as "could not read" and never
as a go-ahead.

| Call / key | Convention | Confirmed in |
| --- | --- | --- |
| `sceUserServiceGetForegroundUser(int*)` | returns 0, user ID out-param | ps5-payload-dev/linkdev `pairui.c`; onionHEN `account_activator.cpp` (12.20) |
| `sceUserServiceGetUserName(int, char*, size_t)` | returns 0 | onionHEN `account_activator.cpp` |
| `sceRegMgrGetInt(int key, int*)` | returns 0 | ps5-payload-dev/offact `offact.c`; linkdev `regmgr.c`; onionHEN |
| `sceRegMgrGetBin(int key, void*, size_t)` | returns 0 | same three; reduced here to present/absent |
| `0x07800100` user *n* user ID (int) | slot *n* adds `(n−1)·0x10000` | all three sources, `reg_entity.h` |
| `0x07800500` user *n* account ID (8 bytes) | read, reduced to non-zero/zero | all three sources |
| `0x41810000` Remote Play enabled (int) | console-wide | onionHEN `remote_play.cpp` |
| `0x07807801` user *n* Remote Play (int) | per-user | linkdev register table |

Never read: sign-in ID, account type/flags, passcode, Remote Play registration keys.

### Assessment codes (the Remote Play answer)

The assessment is deliberately conservative. The best it ever returns is *prerequisites present* —
it never asserts that pairing or streaming works, because neither has been observed on 12.20.

| Code | Meaning |
| --- | --- |
| `not-checked` | The user has not run the check. |
| `blocked-no-library` | The Remote Play library or a pairing function was not found. |
| `presence-only` | Libraries/functions exist (looked up, not called); per-user read not done yet. |
| `blocked-no-account-id` | The user has no account ID. Native Remote Play pairing needs one; AKENO does not create one. |
| `blocked-setting-off` | Remote Play is turned off in the console settings. |
| `incomplete` | A read-only value could not be read, so no conclusion is possible. |
| `prerequisites-present` | Everything AKENO can read is in place. Pairing was **not** attempted and is **not** verified; streaming is **not** verified. |

## 4. Working functionality vs. unverified PS5 capability

| Item | Status |
| --- | --- |
| Device pairing (the proof the link relies on) | VERIFIED ON HARDWARE (12.20) |
| Server link: columns, routes, scopes, migration, audit, unlink-on-unpair | AUTOMATED TEST ONLY (21 server tests) |
| Website label and Unlink | AUTOMATED TEST ONLY (e2e) |
| Console link + opt-in check wiring, logic, JSON | AUTOMATED TEST ONLY (unit + integration, Linux/simulated) |
| `dlopen`/`dlsym`/`sceRegMgrGetInt`/`GetBin` on a real 12.20 console | UNKNOWN (never run on hardware) |
| Account ID present/absent on this owner's user | UNKNOWN (never read) |
| Native Remote Play pairing with a self-generated ID on 12.20 | UNKNOWN |
| Gameplay streaming | UNKNOWN |

## 5. Is there a technically justified next step toward native Remote Play pairing on 12.20?

**Yes — one read-only step, and it does not require activation.** The honest state is that the
*prerequisites* are now checkable but **nothing about pairing on 12.20 has been observed**, so the
justified next step is diagnosis, not a pairing attempt.

1. **Run the opt-in check once on the real console** (owner-approved build, by hand). This is the
   first time any `platform_ps5_account.c` call runs on hardware. It answers three unknowns at
   once, read-only: do the registry/Remote Play libraries and reads work on 12.20; does the current
   local user already have an account ID; is Remote Play enabled. Record the result (and the import
   set staying unchanged) in [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md).

   - If the check returns `prerequisites-present`, there is a justified, still read-only follow-up:
     a Remote Play **status** probe (e.g. `sceRemoteplayGetRemoteplayStatus`/`GetConnectionStatus`
     from the pinned SDK names) to see what the service reports for this user — **without**
     generating a PIN, which changes system state and needs its own approval.
   - If it returns `blocked-no-account-id`, native pairing cannot proceed without an account ID,
     and AKENO does not create one. That is where the separate activation prototype in §6 would come
     in — a distinct, higher-risk decision, not part of this milestone.

2. **Only after that**, and only with a separate written decision, does a pairing attempt (PIN
   generation, a client such as chiaki-ng entering the ID) become a question worth testing. It is
   not justified before the read-only check has told us the prerequisites actually hold on 12.20.

In short: the next justified step is to **run the read-only check on hardware and record what
12.20 reports**. Everything past that (status probe, then pairing) is gated on that result and, for
anything that changes console state, on separate approval.

## 6. What a separate activation prototype would require (not in this milestone)

The owner asked to document what an *experimental activation prototype* would need. **Nothing below
is built or approved.** Writing a self-generated account ID is a persistent system modification and
runs, as onionHEN does it, inside a system process — both on the "Never" list in SAFETY.md. It
would need a new, explicit, written decision and would live in a **separate experimental ELF**,
never the release ELF. Prerequisites before it could ever run once, on hardware:

1. **A disposable test user.** Create a throwaway local user on the console specifically for the
   experiment. Never a user that holds real saves, purchases or a real PSN account. Keep the
   console offline throughout.
2. **A full account backup, before any write.** Record the exact before-values of every registry
   entry the prototype would touch — for that user: account ID, account type, flags; and the
   console-wide Remote Play enable setting — so each can be restored byte-for-byte. onionHEN keeps
   no such backup; the prototype must.
3. **A tested restore path.** A restore function that rewrites the recorded before-values, verified
   on the disposable user (write a change, restore, read back equal) before any real run. Writes
   must be ordered so a failure part-way is still restorable; onionHEN writes three values
   non-atomically with no undo, which the prototype must not copy.
4. **Reversibility documented and accepted.** Spell out what cannot be undone: deleting the user to
   recover removes its saves; a future PSN sign-in on a locally activated user has an **unknown**
   outcome (ID mismatch may block sign-in). The decision to proceed accepts these in writing.
5. **Independent code.** onionHEN is GPLv3; AKENO's own sources are MIT. Any prototype is written
   independently from the public register/API facts, citing onionHEN as reference only, with
   licensing recorded in [LICENSES.md](LICENSES.md).
6. **A per-build hardware test plan the owner approves**, like the existing alpha plans: the exact
   build SHA-256, the stop/restore steps, and the fallback, recorded before the build is run.

Recommendation remains as in the feasibility report: do the read-only link and check (done); leave
activation to onionHEN, run by the owner on a throwaway user if they choose, unless and until a
separate decision is taken to build the prototype above.

## 7. Where the code is

- Console logic (pure, unit-tested): `clients/ps5/app/account_bridge.{h,c}`
- Console reads (PS5, read-only): `clients/ps5/app/platform_ps5_account.c`; host simulation:
  `clients/ps5/app/platform_host_account.c`; interface: `clients/ps5/app/platform_account.h`
- Console app glue and routes: `clients/ps5/app/app_account.c`, `app_routes.c`
- Server: `apps/server/src/modules/devices/{service,routes,scopes}.ts`,
  `apps/server/src/database/schema/devices.ts`, migration `0005_account_bridge.sql`
- Shared contracts/schemas: `packages/shared/src/{constants.ts,contracts/devices.ts,schemas/devices.ts}`
- Website: `apps/web/src/features/devices/PairedDevicesCard.tsx`
- Tests: `apps/server/test/devices.test.ts`, `clients/ps5/tests/unit/test_account_bridge.c`,
  `clients/ps5/tests/integration/run.mjs`, `apps/web/tests/e2e/device-pairing.spec.ts`
