# PS5 offline account activation and AKENO account bridge — feasibility and risk

**Status: read-only research. Nothing in this document has been built, sent to a console or run
on hardware.** No account registry was read or written, no Sony service was contacted, and no
production system was changed. Any prototype needs a separate, explicit approval (see
[Decisions needed](#7-decisions-needed-before-any-prototype)).

Question from the project owner: the PS5 (firmware 12.20) has only an **offline local user**.
Can AKENO offer *optional* local account activation, link that local PS5 identity to an existing
AKENO NETWORK account, and would native Remote Play pairing then work for that user?

Status labels, as in [SHAREPLAY_PS5_RESEARCH.md](SHAREPLAY_PS5_RESEARCH.md):
**VERIFIED ON HARDWARE** · **AUTOMATED TEST ONLY** · **SOURCE-VERIFIED** (read in source, not run)
· **UNKNOWN** · **UNSUPPORTED**.

## 1. Short answer

| Question | Answer | Status |
| --- | --- | --- |
| Can a local offline user be "activated" without Sony? | Yes, in the narrow sense OnionHEN uses: it writes a self-generated 64-bit account ID, account type `"np"` and a flags value into the console's account registry. That makes the system *treat* the user as having an account ID. It is **not** a PSN sign-in, grants no PSN access, licences or online services, and nothing at Sony knows about it. | SOURCE-VERIFIED (onionHEN `b23ffe6`) |
| Can AKENO do that itself under current project rules? | **No, not without a new written decision.** It is a persistent system modification and, as OnionHEN does it, runs inside ShellUI (a system process). Both are on the "Never" list in [SAFETY.md](SAFETY.md). | Policy |
| Can a local PS5 identity be linked to an AKENO account? | **Yes, safely, without activation.** The console app is already paired to an AKENO account by the device-authorization flow ([DEVICE_PAIRING.md](DEVICE_PAIRING.md), tested on 12.20). A link record can attach a *local, unverified* console-user label to that paired device. AKENO's existing device token is the only proof of ownership; the PS5 identifier is display data, never a credential. | Design only |
| Does native Remote Play pairing work for a locally activated user? | OnionHEN's code drives the console's own Remote Play service (`libSceRemoteplay`) to show a PIN and wait for a client, and refuses to start unless the user has a non-zero account ID. Whether a client (in practice chiaki-ng with a manually entered account ID; the official app signs in to PSN and supplies its own ID) completes pairing with a self-generated ID on **12.20** has **not** been observed by this project. | SOURCE-VERIFIED (code path) / UNKNOWN (12.20 outcome) |
| Does that give gameplay streaming? | **Not verified.** Pairing is registration only; OnionHEN's own README says streaming stays with Sony's native service. No stream has been seen by this project. | UNKNOWN |

## 2. What onionHEN actually does

Source: `github.com/aydencharles/onionHEN`, commit `b23ffe6` (2026-09-12), GPLv3. Read only; no
code was copied into AKENO.

### 2.1 Account activation (`source/shellui/src/account_activator.cpp`)

Runs **inside ShellUI** (OnionHEN injects its Toolbox into the Settings app; `Activator` is
built from the Toolbox page handlers `toolbox_xml.cpp` / `onpress_misc.cpp`).

1. Finds the foreground user (`sceUserServiceGetForegroundUser`) and scans registry account
   slots 1–16 for the slot whose user-ID entry equals it. Entity number =
   `(slot − 1) × 0x10000 + base` (`include/onion/reg_entity.h`).
2. Reads, per slot:

   | Value | Registry base (slot 1) | Read with | Written by activation |
   | --- | --- | --- | --- |
   | User ID | `0x7800100` | `sceRegMgrGetInt` (via ShellUI hook) | no |
   | Account ID (8 bytes) | `0x7800500` | `sceRegMgrGetBin` | **yes** |
   | Account type (≤ 17 chars) | `0x780b007` | `sceRegMgrGetStr` | **yes**, `"np"` |
   | Account flags | `0x7800800` | `sceRegMgrGetInt` | **yes**, `4098` (`0x1002`) |

3. "Not activated" means **account ID == 0**. `Activate()` refuses if the ID is already non-zero.
4. The new ID is a hash of the **user name only**:
   `id = 0; for each byte c: id = 0x100000001B3 × (id XOR c)` (64-bit). The source writes the
   start value as `0x5EAF00D / 0xCA7F00D`, which is integer division and equals **0**, so this
   is FNV-1a with a zero offset basis. Consequences:
   - **Deterministic and guessable**: the same name gives the same ID on every console
     (`"Player1"` → `0x8D2742CC0A14238C` on any PS5). It is an identifier, not a secret.
   - **Low entropy for short names** (`"a"` → `0x61000000A4D3`), and `char` sign extension
     makes non-ASCII names hash differently from standard FNV.
   - Not drawn from Sony's ID space; a collision with a real PSN account ID is unlikely but not
     excluded.
5. Writes ID, then type, then flags with three separate `sceRegMgrSet*` calls. **No backup of
   the previous values, no rollback if the second or third write fails, and no deactivate
   function.** The previous type and flags are only logged.
6. Tells the user to reboot. The UI warns (upstream wording): *"You may lose your existing game
   saves because the account ID will change."*

### 2.2 Remote Play pairing (`source/shellui/src/remote_play.cpp`)

Also inside ShellUI. Functions are resolved by name at run time from `libSceRemoteplay.sprx`
(`prx.cpp`), so no firmware offsets are involved for these calls.

1. Refuses to show the page unless the foreground user is activated (account ID ≠ 0).
2. First use: reads registry `0x41810000` (Remote Play enable setting) and, if not `1`,
   **writes `1`** — a second persistent system-setting change — then `sceRemoteplayInitialize`.
3. `sceRemoteplayGeneratePinCode` → 8-digit PIN shown as `NNNN NNNN`.
4. A thread polls `sceRemoteplayConfirmDeviceRegist(&status, &error)` until `status == 2`
   (paired), an error, or the page closes; then `sceRemoteplayNotifyPinCodeError(1)` invalidates
   the PIN. Note the loop has **no sleep or timeout** between polls (busy-wait while the page is
   open).
5. Shows the account ID as Base64 of its 8 little-endian bytes (12 characters ending in `=`) —
   the form Remote Play clients that accept a manually entered account ID expect — and can save
   it with the PIN to `/usbN/remote_play_info.txt`.

OnionHEN's README: *"The feature provides pairing and registration only. Video streaming and
client transport remain handled by Sony's native Remote Play service."*

### 2.3 Firmware 12.20

| Item | Finding | Status |
| --- | --- | --- |
| OnionHEN stack on 12.20 | Has explicit 12.20 entries: `libhijacker/source/offsets.cpp` (`V1220`), HomeUI and Settings UI profiles (`homeui_top_nav_profiles.inc`, `debug_settings_route_policy.hpp`, with a unit test for the 12.20 route). These are **firmware-specific offsets and patches** for ShellUI injection. | SOURCE-VERIFIED |
| Registry entity numbers above | Constants, not version-keyed in the source. Whether they are the same on 12.20 is assumed by OnionHEN, not proven here. | UNKNOWN on this console |
| `libSceRemoteplay` / `libSceRegMgr` names | Present as stubs in the SDK AKENO pins (`ps5-payload-sdk f5d40f0`, `sce_stubs/libSceRemoteplay.c`, `libSceRegMgr.c`), including `GeneratePinCode`, `ConfirmDeviceRegist`, `GetRemoteplayStatus`, `GetConnectionStatus`, `RegMgrGetBin/GetStr/GetInt`. Stubs are names only: no prototypes or semantics. | SOURCE-VERIFIED (names) |
| Calling these from a plain payload process (AKENO's model) instead of ShellUI | Not shown anywhere we read. Privilege or IPC requirements are unknown. | UNKNOWN |
| OnionHEN prerequisites | A kernel exploit, an external `elfldr` on 9021, its own loader on 9020, `kstuff`, and ShellUI injection. | SOURCE-VERIFIED (README) |

## 3. Local activation is not PSN authentication

| | Local activation (OnionHEN style) | Genuine PSN sign-in |
| --- | --- | --- |
| Who issues the account ID | The console itself, from a hash of the user name | Sony, after authenticating the user |
| Proof of identity | None; anyone can compute the same ID | Sony credentials and tokens |
| PSN Store, licences, cloud saves, trophies sync, online multiplayer, friends, messages | **No** | Yes |
| Known to Sony servers | No | Yes |
| What it changes | Three registry values on one console | Sony-side account state plus console registry |
| Reversible by the user from system menus | Not known; no tool provided by OnionHEN | Sign out / delete user |

AKENO must never label a locally activated user as "PSN", "signed in" or "verified", never send
the generated ID to any Sony endpoint, and never use it as an AKENO credential. This follows
[PSN_COMPATIBILITY_RESEARCH.md](PSN_COMPATIBILITY_RESEARCH.md): AKENO does not emulate, proxy or
impersonate Sony services. Local activation as described does not contact or impersonate Sony;
it does, however, make local system components believe an account ID exists, which is why it is
treated here as a system modification that needs your decision.

## 4. Linking a local PS5 identity to an AKENO account

The safe bridge does **not** require activation and does not touch the registry.

**Already in place (tested on 12.20):** the console app pairs to an AKENO account with the
RFC 8628-style flow; the device receives a scoped bearer token (`device_credentials`). That
token, not anything read from the console, is the proof that this console belongs to this AKENO
account.

**Proposed (not built), all additive:**

1. The console app reports a *local console-user label* for the foreground user, read with
   calls the app already makes (`sceUserServiceGetForegroundUser`, `sceUserServiceGetUserName`;
   `libSceUserService` is already a reviewed strong import). Optionally, an **opt-in, read-only**
   `sceRegMgrGetBin` of the account ID to report "offline / has account ID" — that is a new
   import, so it must be loaded with `dlopen`/`dlsym` behind the experiment switch and pass the
   import review in SAFETY.md.
2. The server stores it per paired device in a new table (additive migration), for example
   `device_console_users(device_credential_id, local_user_id, display_name, id_state,
   account_id_hmac, first_seen_at, last_seen_at)`, where `id_state ∈ {offline, locally_activated,
   unknown}` and the account ID, if reported at all, is stored only as an HMAC with a server
   secret. Rows disappear when the device is unpaired (cascade).
3. The website shows it as "PS5 user *Name* (local, not verified)" under the paired device. It
   never enables extra scopes and never appears where PSN identity would.

This gives the AKENO profile a stable link to "the person at this console" without any write to
the PS5. Status: design only.

## 5. Remote Play pairing with a locally activated user

- **Code path exists** in OnionHEN and uses only by-name system functions (SOURCE-VERIFIED).
- **Client side:** the official PS Remote Play app requires a PSN sign-in and registers with
  that account's ID, so it is not expected to pair with a self-generated ID (inferred, not
  tested). Clients that accept a manually entered Base64 account ID (chiaki-ng) are what
  OnionHEN's display and USB export are built for (inferred from the code).
- **On 12.20 with this console:** UNKNOWN. No report from this project.
- **Relevance to SharePlay:** a paired PC client could feed AKENO's existing "Window" source
  ([SHAREPLAY_HARDWARE_TEST.md](SHAREPLAY_HARDWARE_TEST.md), stage A2) without a capture card or
  the official app. It is still Remote Play, so it does not meet the "direct capture without
  Remote Play" priority, and it inherits Remote Play's latency. **Gameplay streaming through
  this route is not verified.**
- An AKENO payload calling `sceRemoteplayGeneratePinCode` from its own process (not ShellUI)
  might avoid the system-process injection, but whether it works is UNKNOWN, and it still
  needs the account ID to be non-zero, i.e. activation first.

## 6. Risk assessment

Ratings are for *this* console with an offline user on 12.20, if OnionHEN-style activation
were applied.

| Area | Risk | Detail | Mitigation if ever approved |
| --- | --- | --- | --- |
| Existing save data | **High / unknown** | Upstream warns saves may be lost because the account ID changes. PS5 save data is commonly reported to be bound to the user and its account ID (not verified by this project); how an offline user's saves (ID 0) behave after the ID becomes non-zero is not documented. | Back up first: copy saved data to USB (Settings → Saved Data and Game/App Settings) and make a full system backup (Settings → System → System Software → Back Up and Restore). Test with a **new throwaway local user** that has no saves, never the main user. |
| Account identifiers | Medium | Deterministic, name-derived, guessable ID; no secret. Changing the user name later does not change the stored ID. Theoretical collision with a real PSN ID. | Treat as a label only. If AKENO ever generated an ID, use a random 64-bit value and record it, never the name hash. |
| Future PSN sign-in on the same user | **Unknown** | What the console does when a locally activated user later signs in to a real PSN account (ID mismatch) is not documented in anything we read. Could block sign-in or require deleting the user. | Keep activation to a throwaway user. |
| Licences | Medium | No PSN licences are created or affected for the activated user; digital games bought on another user remain tied to that user's real account. Disc games are unaffected. Using activation to run content without a licence is out of scope for AKENO and not something this project will support. | Document clearly; no licence-related code. |
| Trophies | Unknown | Trophies earned under a fake ID may never sync or may conflict later. | Document. |
| Reversibility | **Poor** | OnionHEN keeps no backup of the old type/flags, has no undo, and writes three values non-atomically (a failure after the first write leaves a half-activated user). The Remote Play enable setting is also overwritten without recording the old value. Deleting the user removes the user *and its saves*. | Any prototype must record the exact before-values, write nothing it cannot restore, and have a tested restore path, before it is ever run. |
| Console stability | Medium–High | Requires the full OnionHEN chain (kernel exploit, kstuff, ShellUI injection with 12.20 offsets). AKENO's history includes a console that needed a reset after homebrew. | Not AKENO code; run only if you choose to, outside AKENO. |
| Policy / account | Medium | Modifying system software breaks Sony's terms; a console later seen online with modified state could be restricted. Local activation does not contact Sony, but subsequent online use could. | Keep the console offline while experimenting; never use a real PSN account on a test user. |
| Licensing of code | Low if handled | onionHEN is GPLv3. AKENO's PS5 ELF is already a GPLv3 combined work (SDK runtime), but AKENO's own sources are MIT. Copying onionHEN code would make those files GPLv3. | Write any AKENO code independently; cite as reference only. |

## 7. Decisions needed before any prototype

Nothing below is done. Each needs your explicit approval, separately.

1. **Amend SAFETY.md** to allow registry *reads* of the account ID from the AKENO process
   (opt-in, `dlopen`, read-only). Low risk; enables the "local / activated / unknown" label.
2. **Build the identity link** in section 4 (server migration + console report + website label).
   No console writes. Can be developed and tested entirely on Linux.
3. **Registry writes (activation) from AKENO:** not recommended. If you still want it, it would be
   a separate experimental ELF, never the release ELF, with before-value recording, a restore
   function, a throwaway user, and a hardware test plan you approve per build.
4. **Remote Play pairing diagnostic:** a read-only probe (`GetRemoteplayStatus`,
   `GetConnectionStatus`) is possible from the pinned SDK names; generating a PIN changes system
   state and needs its own approval.

Recommendation: do 1 and 2; leave activation to OnionHEN, used by you on a throwaway local user
if you choose, and record any result in [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md).

## Sources read

- onionHEN `b23ffe6`: `source/shellui/src/account_activator.cpp`, `include/account_activator.h`,
  `source/include/onion/reg_entity.h`, `source/shellui/src/remote_play.cpp`,
  `include/remote_play.hpp`, `onpress_misc.cpp`, `onpress_network.cpp`, `toolbox_xml.cpp`,
  `prx.cpp`, `hook_launch.cpp`, `libhijacker/source/offsets.cpp`,
  `include/onion/debug_settings_route_policy.hpp`, `homeui_top_nav_profiles.inc`,
  `i18n/en-US.json`, `README.md`.
- ps5-payload-sdk `f5d40f0`: `sce_stubs/libSceRemoteplay.c`, `sce_stubs/libSceRegMgr.c`.
- AKENO: [DEVICE_PAIRING.md](DEVICE_PAIRING.md), `apps/server/src/database/schema/devices.ts`,
  [SAFETY.md](SAFETY.md), [PSN_COMPATIBILITY_RESEARCH.md](PSN_COMPATIBILITY_RESEARCH.md),
  [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md),
  [SHAREPLAY_PS5_RESEARCH.md](SHAREPLAY_PS5_RESEARCH.md), [LICENSES.md](LICENSES.md).
