# PS5 capture diagnostic — hardware test plan (firmware 12.20)

This runs the experimental **read-only** diagnostic ELF on the real PS5 and sends its report
back. It answers the open questions in [PS5_DIRECT_CAPTURE_RESEARCH.md](PS5_DIRECT_CAPTURE_RESEARCH.md)
(do the capture libraries exist on 12.20? do the PS4-era symbols resolve? is the broadcast status
queryable?) **without calling any capture, encoder or broadcast function.**

Nothing here has been run on a PS5. Do not read a green CI run as hardware success.

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
- **RESOLVE** ("Resolve symbols"): `dlopen` each candidate library and record which names
  resolve. `dlopen` can have side effects (it runs a module's init), so it is behind this
  explicit button and is the only loading the diagnostic ever does. Still nothing is *called*.

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
6. Press **"Resolve symbols (loads libraries, calls nothing)"**. Screenshot again. Now each
   candidate shows whether it loaded and which symbols resolved (✓/✗), plus any load error.
7. Press **"Copy/export"** if offered, or screenshot the JSON, and send me:
   - the STATIC screenshot, the RESOLVE screenshot, and the exported JSON;
   - the `firmware`, `buildId`, `generatedAt` values (so I can tie it to the exact ELF);
   - the browser console if anything looked wrong (optional).
8. **Optional second pass, during a game** (only if you are comfortable): repeat steps 4–7 while a
   game is running, to see whether library/symbol availability differs. This remains read-only.
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
(reported as a load error) or the payload exits. Reload your stable AKENO ELF. Report what you
saw. Do not retry with any build that is not the exact checksummed artifact above.
