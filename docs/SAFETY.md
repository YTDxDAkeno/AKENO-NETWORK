# Safety rules

A previous homebrew experiment crashed a console and required a system reset. These rules exist
so that does not happen again, and so that AKENO never puts accounts or other people at risk.
Homebrew is never risk-free; these rules reduce risk, they do not remove it.

## Never (without a new, explicit, written decision)

- Automatically send, inject or load anything onto a console. Every hardware test is started by
  a person, by hand, after approving that specific build.
- Kernel modification, firmware or system-file changes, persistent system modifications.
- Hooks into, or writes to the memory of, system processes; firmware-specific offsets or patches.
- Changes to, redirection of, or impersonation of Sony services; custom trust anchors; disabling
  TLS verification; collecting or relaying PSN credentials; bypassing online authentication.
- Anything that affects consoles, accounts or networks other than your own.
- Destructive tests against the production server or its database.

## The PS5 payload SDK runtime (disclosure)

Every payload built with ps5-payload-sdk starts with the SDK's runtime (`crt`). Before AKENO's
`main()` runs, that runtime uses the kernel read/write access handed over by the ELF loader to
raise **its own process's** credentials and syscall permissions, using firmware-specific offsets
maintained by the SDK. The diagnostic ELF that already ran on the console did the same. AKENO's
own code contains no offsets, calls none of the SDK's kernel-memory helpers, and touches no other
process, system file or Sony service; CI enforces this (`clients/ps5/console/check-imports.sh`).
If this runtime behaviour is not acceptable, no payload built with this SDK can be used.

## Keeping the proven start-up intact (Alpha 0.2)

The same runtime refuses to start a payload when a needed library (`DT_NEEDED`) or a strong
symbol cannot be resolved. A new import could therefore stop a build that worked from starting at
all on some firmware. Rules, enforced by `clients/ps5/console/check-imports.sh` in CI:

- Every **strong** import must be in `console/reviewed-imports.txt`, which is exactly the import
  set of the Alpha 0.1 ELF that ran on a PS5 (12.20). Adding one requires a review and a hardware
  test.
- **Optional** system functions are weak imports (the SDK libc's own pattern), checked for being
  weak; if a firmware lacks one, only its feature reports "unavailable"
  (`sceKernelGetAppInfo`).
- **Experimental** functions are not imported at all: `sceNotificationSend` is loaded with
  `dlopen`/`dlsym` from `libSceNotification.sprx`, only after the user turns the experiment on,
  and only in `platform_ps5.c`. Any failure falls back to the proven call.
- Every CI run rebuilds Alpha 0.1 from `592c23a` and requires the hardware-tested SHA-256, so the
  fallback build stays available and the toolchain stays reproducible.
- **PS5 audio research** (Alpha 0.4) lives in a separate audio lab ELF only, never in the release
  ELF. It must import exactly what the release ELF imports (CI compares them), loads
  `libSceAudioOut.sprx`/`libSceAudioIn.sprx` with `dlopen` only when the user starts a check,
  never captures audio, and uses declarations from a working PS5 homebrew driver only. No
  microphone, controller-ownership or PSN voice access. See PS5_AUDIO_RESEARCH.md.

## Game detection is read-only

Game detection lists processes (`sysctl(KERN_PROC_PROC)`) and asks the system for each process's
application info (`sceKernelGetAppInfo`) into a zeroed, oversized buffer. It never attaches to,
injects into, hooks, signals or writes to another process, reads no other process's memory, and
uses no offsets. Where the SDK samples disagree on a structure layout, the console's own answers
decide and a conflict reports nothing (see GAME_ACTIVITY.md). Title metadata is read from files,
read-only and size-limited, with title IDs validated before they become part of a path.

## Always

- Develop and test on PCs and Linux first (`clients/ps5` runs and is tested there).
- Use the paired-device flow; never put an account password on the console.
- Bound everything: timeouts on every network operation, size limits on every response,
  a maximum number of reconnect attempts, no unbounded allocation.
- Fail closed: unknown firmware, missing components or invalid responses disable a feature and
  report why; they never trigger a fallback that touches the system.
- Provide a clean exit: the client stops on request (SIGINT/SIGTERM or the UI), and unpairing the
  device on the website signs it out immediately.
- Record every hardware test in [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md).

## Production server

- Additive, reversible database migrations only; take a backup before deploying.
- Tests use disposable databases whose names are checked before they are reset
  (`akeno_test_*`, `akeno_e2e*`, `akeno_client_it`).
- Load tests run against disposable instances, never production.
- No GitHub write credentials on the VPS.

## Before the first console test (checklist)

Alpha 0.1 passed its first hardware test on 12.20 (see FIRMWARE_COMPATIBILITY.md). For every
new build (currently Alpha 0.2):

- [ ] The console's data is backed up; a reboot is acceptable.
- [x] SDK version documented with source and license (ps5-payload-sdk `f5d40f0`, GPLv3+; see
      LICENSES.md). The loader is the user's own; record its name and version when testing.
- [x] The build artifact, its checksum and its source commit are recorded (`SHA256SUMS` and
      `BUILDINFO.txt` in the CI artifact), with the verified Alpha 0.1 fallback next to it.
- [x] The build only uses networking, file storage in its own directory (`/data/akeno`), system
      notifications, the system browser and read-only process information; the import gate
      enforces the list and the proven import set.
- [x] The test plan and the way to stop the app are written down
      (HARDWARE_TEST_PLAN_ALPHA_0.2.md; Exit button, or reboot; fallback to Alpha 0.1).
- [ ] You approved this specific build (its SHA-256) for hardware testing.
