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

- [ ] A non-primary console is available and its data is backed up.
- [x] SDK version documented with source and license (ps5-payload-sdk `f5d40f0`, GPLv3+; see
      LICENSES.md). The loader is the user's own; record its name and version when testing.
- [x] The build artifact, its checksum and its source commit are recorded (`SHA256SUMS` and
      `BUILDINFO.txt` in the CI artifact).
- [x] The build only uses networking, file storage in its own directory (`/data/akeno`), system
      notifications and the system browser; the import gate enforces the list.
- [x] The test plan and the way to stop the app are written down (PS5_CLIENT.md, H1–H11; Exit
      button, or send nothing further and reboot).
- [ ] You approved this specific build for hardware testing.
