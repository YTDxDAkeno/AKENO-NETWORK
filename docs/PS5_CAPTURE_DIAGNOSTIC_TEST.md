# PS5 capture diagnostic — hardware test plan (firmware 12.20)

This runs the experimental **read-only** diagnostic ELF on the real PS5 and sends its report
back. It answers the open questions in [PS5_DIRECT_CAPTURE_RESEARCH.md](PS5_DIRECT_CAPTURE_RESEARCH.md)
(do the capture libraries exist on 12.20? do the PS4-era symbols resolve? is the broadcast status
queryable?) **without calling any capture, encoder or broadcast function.**

Nothing here has been run on a PS5. Do not read a green CI run as hardware success.

## v2: what changed after the first hardware run (and why)

On firmware 12.20 the first build's **STATIC** tier worked and confirmed the candidate library
files exist, but pressing a single **"Resolve symbols"** button that probed all six libraries at
once made the app disappear ("AKENO NETWORK is not running"), with no RESOLVE line in `akeno.log`.
That earlier build only logged *after* the whole batch finished, so the log could not say which
library, or which operation, was in progress when the app exited.

**Likely cause, not yet confirmed.** On the pinned SDK, `dlopen` of a module that is not already
mapped calls `sceKernelLoadStartModule`, which *loads and starts* the module, and the old
`dlclose` afterwards called `sceKernelStopUnloadModule`, which *stops and unloads* it. Either
starting, or (more likely) unloading, an unverified system capture/broadcast module can terminate
the process. **This stays a hypothesis until a v2 log from the real console identifies the exact
operation.** Do not treat it as confirmed.

v2 changes, to make the next run safe and diagnostic:

- **One library at a time.** There is no "resolve all" button. You probe each library on its own,
  with a separate confirmation, so a crash is isolated to one named library.
- **Flushed, per-operation log.** `akeno.log` gets a line *before and after* every `dlopen` and
  every `dlsym`, each flushed to storage immediately. If the app exits mid-probe, the last line
  names the exact operation and library that were in progress.
- **No unloading.** v2 never calls `dlclose`/`sceKernelStopUnloadModule`. A probed module is left
  loaded for the short life of the diagnostic; the process exits normally and the OS reclaims it.
- **Visible probing/err state.** The page shows a "probing…" indicator and, if the app stops
  answering, a message telling you to reload the stable app and send `akeno.log`.

## What this ELF is and is not

- It is a **separate experimental build**: `akeno-network-ps5-capture-diagnostic.elf`. It is
  **not** the stable AKENO app and must not replace it. Keep your current working ELF as the
  fallback.
- It is **read-only**: it reports firmware/build info, which candidate capture libraries exist
  (stat), and — only after you press a button — whether candidate symbols resolve (dlsym). It
  **calls nothing**, changes no console setting, touches no DNS or account, and never contacts
  PSN or Twitch.
- It passes the same import safety gate as the release ELF (same system imports).

## Which file to download

From the **PS5 console build** GitHub Actions run for this commit, artifact
`akeno-network-ps5-capture-diagnostic`:

- `akeno-network-ps5-capture-diagnostic.elf` — the ELF to load.
- `SHA256SUMS`, `BUILDINFO.txt` — verify the SHA-256 before loading.
- `fallback/akeno-network-ps5.elf` — your current stable app, to return to afterwards.

Check the checksum matches `SHA256SUMS` before sending it to the console.

## Safety — what is and is not done

Done (read-only): firmware query, library-file existence, symbol **presence** lookup, local
resolver **count** (addresses redacted), AKENO server *configuration* (not probed here).

Never done: no capture/encoder/broadcast call; no kernel patch; no SceShellCore change; no
process injection; no credential change; no DNS/account/firmware change; no PSN/Twitch contact;
no memory dumps; no account identifiers in the output.

Two tiers:
- **STATIC** (on open / "Refresh"): no library is loaded at all.
- **RESOLVE** (per-library "Probe this library" + confirm): `dlopen` **one** candidate library and
  record which of its names resolve. `dlopen` can have side effects (it loads and starts the
  module), so it is behind an explicit per-library confirmation and is the only loading the
  diagnostic ever does. The module is **not** unloaded afterwards. Still nothing is *called*.

## Steps

Run from the **home screen**, not inside a game, for the first pass.

1. Verify the SHA-256 of the ELF against `SHA256SUMS`.
2. Load `akeno-network-ps5-capture-diagnostic.elf` with your usual ELF loader. Your stable AKENO
   app is not affected; this is a separate payload.
3. Open its page in the PS5 browser (the app opens it, as the normal app does).
4. Go to **Diagnostics → PS5 capture capability diagnostic**.
5. **STATIC pass.** Screenshot the whole section. It shows firmware, build id, pinned SDK, the
   library folder, which candidate libraries are **present**, the capability verdict, and the
   candidate list with each symbol (no ✓/✗ yet — nothing loaded).
6. **RESOLVE, one library at a time.** For the **first** candidate, press **"Probe this
   library"**, then **"Probe"** on the confirmation. Wait for the row to update (loaded? which
   symbols resolved ✓/✗, or a load error). Screenshot the row.
   - If the app is still running, move to the **next** library and repeat. Do them one by one.
   - **If the app disappears** ("AKENO NETWORK is not running") on a particular library, stop.
     That library is the one to report. Do not probe it again. Reload your stable AKENO app
     (step 9) and collect `akeno.log` (step 7) — its last lines name the exact operation.
7. Collect the evidence and send me:
   - the STATIC screenshot and each per-library RESOLVE screenshot (or the exported JSON);
   - **`akeno.log`** from the app's data folder — this is the key new artifact; its
     `capture.resolve.*` lines show the last `dlopen`/`dlsym` before any crash;
   - the `firmware`, `buildId`, `generatedAt` values (so I can tie it to the exact ELF).
8. **Optional second pass, during a game** (only if you are comfortable): repeat the per-library
   probes while a game is running, to see whether library/symbol availability differs. Still
   read-only, still one library at a time.
9. Exit the diagnostic and reload your stable AKENO app.

## What I will learn from the results

- Which capture-related libraries actually exist on 12.20 (file presence).
- Whether the PS4-era screenshot/record/broadcast symbol **names** resolve on 12.20 (presence
  only — still not proof they are callable).
- Whether the GameLiveStreaming/SharePlay **status** hooks resolve (track B groundwork).
- The redacted resolver summary (to reason about the track-B DNS conflict without exposing your
  jailbreak DNS).

## Functions intentionally NOT called (and why)

Every `sceScreenShot*`, `sceVideoRecording*`, `sceGameLiveStreaming*`, `sceRemoteplay*` and
`sceVideoOut*` function: their PS5 signatures are unverified, and calling an unknown-signature
function can crash the process or have side effects. The diagnostic only checks that a name
exists. Turning any of them into a real capture path is a **later, separately-approved** step
that must first establish the signature from a verified source
([PS5_DIRECT_SHAREPLAY_PLAN.md](PS5_DIRECT_SHAREPLAY_PLAN.md)).

## If something goes wrong

The diagnostic is read-only; the worst expected case is that `dlopen` of a candidate fails
(reported as a load error) or the payload exits when a particular library is loaded. If the app
exits:

1. Reload your stable AKENO ELF (it is untouched — this is a separate payload).
2. Grab `akeno.log` from the app's data folder before anything overwrites it. The `capture.resolve.*`
   lines are flushed as they happen, so the **last** line names the library and the exact
   operation (`dlopen` / `dlsym`) that was in progress. That is what pins down the cause.
3. Send me `akeno.log` and say which library you had just pressed "Probe" on.
4. Do not probe that same library again, and do not retry with any build that is not the exact
   checksummed artifact above.
