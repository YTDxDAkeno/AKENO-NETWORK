# AKENO NETWORK Native: the built-in updater (1.2.0)

Status: implemented and tested on a PC against a mock GitHub (`tests/updater`). **Not run on a
PS5.** No release has been published in the update format yet, and the release signing key does
not exist yet: until the owner creates it (below), every install is refused with a clear message.

## How AKENO Native is installed on a PS5 (what the updater has to work with)

These are facts from the launcher's source (ps5-payload-dev/websrv 7748df9), not assumptions:

- The websrv homebrew launcher lists folders that contain a `homebrew.js`. It runs that file in a
  sandboxed frame (`window.workingDir`, `ApiClient.fsGetFileText(path)`), and `main()` returns the
  tile text and an `onclick` that returns `{path, args, daemon}`.
- Without `daemon: true` the launcher closes the running game (the "BigApp") and starts the ELF as
  the foreground application (`src/websrv.c:186`). AKENO Native is started this way.
- The ELF is read from the folder at every start, so a new ELF in the folder is a new version.
  Nothing is installed into the system: no package (PKG), no firmware, no system partition, no
  registry, no PSN setting is touched by the updater or by AKENO.
- The folder lives on `/data` (user storage). AKENO can write there; it cannot write to system
  partitions and does not try.

The updater therefore installs **side by side** in the AKENO folder and lets `homebrew.js` pick
the ELF.

## Install layout

```
AKENONative/
  eboot.elf, homebrew.js, sce_sys/icon0.png   installed by hand ("factory"); never changed
  update/versions/<version>/...               versions installed by the updater
  update/active.json                          which version starts, its state, the previous one
  update/staging/                             download and unpacking; wiped before every use
  update/history.log                          one line per install, start, confirmation, rollback
```

- `active.json` is written atomically (temporary file, `fsync`, `rename`). It holds `format`,
  `version`, `elf`, `state` (`pending` or `confirmed`), `attempts`, `max_attempts` (2), the
  previous `{version, elf}` and up to four `rejected` versions.
- Personal data is never in the AKENO folder: settings, the sign-in and the pairing stay in
  `/data/akeno-native/` and the daemon's folder, which the updater never reads or writes. Updater
  preferences are `/data/akeno-native/updater.json` (channel, automatic checks, last check).
- `homebrew.js` (`package/homebrew.js`) starts `active.elf` with `--launcher --install-dir <dir>`.
  It accepts only `eboot.elf` or `update/versions/<semver>/eboot.elf`; anything else, a missing
  or unreadable `active.json`, falls back to `eboot.elf`. Its long-press menu offers "Start the
  previous version" and "Start the version installed by hand". It never writes files.
- The launcher reads only the top-level `homebrew.js`. A `homebrew.js` inside an update is stored
  but does not take effect; changes to the launcher entry need a manual install.

## Rollback

1. After an install, `active.json` says `pending`, attempts 0.
2. Each start of the new version counts itself (`attempts + 1`) before opening the screen.
3. After 20 seconds of normal running it writes `confirmed`.
4. If a pending version has started twice without confirming (crash, hang, black screen),
   `homebrew.js` starts the previous version instead. That version records the rollback, adds the
   failed version to `rejected` (never offered again) and shows a notification.
5. The user can always pick "Start the version installed by hand" from the tile's options.

A failure at any step before the final `rename` of `active.json` leaves the running version and its
data untouched; leftovers in `staging/` are wiped at the next use.

## Release format

A GitHub release in `YTDxDAkeno/AKENO-NETWORK` with tag `native-<semver>` (for example
`native-1.2.0-beta.2`) and exactly these assets:

| Asset | Content |
| --- | --- |
| `akeno-native-update.json` | Manifest: version, tag, channel, package name, size and SHA-256, every file's path, size and SHA-256, requirements (`firmware_min/max`, `daemon_protocol_min/max`, `min_installed_version`), notes |
| `akeno-native-update.json.sig` | ECDSA P-256 signature (DER) over SHA-256 of the manifest bytes |
| `akeno-native-<version>.zip` | The files: `eboot.elf`, optionally `homebrew.js`, `sce_sys/icon0.png`, `daemon/akeno-network-ps5-native-ipc.elf` |

Build it with `node clients/ps5-native/tools/make-update.mjs` (usage in the file header). It reads
the private key from `AKN_UPDATE_SIGNING_KEY` or `--key`, writes the three assets and uploads
nothing. Publishing a release stays a manual step that needs the owner's approval.

Channels: **stable** sees releases that are not marked pre-release; **beta** also sees
pre-releases. Versions are compared with semantic versioning (pre-release identifiers included,
build metadata ignored). Drafts are ignored.

## What is checked, in order

1. HTTPS only, certificate verified with the bundled CA list. Only these hosts, also after
   redirects (at most 5): `api.github.com`, `github.com`, `objects.githubusercontent.com`,
   `release-assets.githubusercontent.com`, `github-releases.githubusercontent.com`.
2. Every asset URL must be `https://github.com/YTDxDAkeno/AKENO-NETWORK/releases/download/<tag>/<name>`;
   assets of other repositories or tags are refused. Sizes are bounded before downloading.
3. The manifest signature against the public key compiled into the app (`src/core/update_key.h`).
   Without a key nothing is installed.
4. The manifest must name the same version and tag as the release, and only allowed file paths.
5. Compatibility: firmware range (from the console), daemon protocol (asked from the running
   daemon), minimum installed version, not a rejected version, newer than the running version.
6. The package: size and SHA-256 against the signed manifest, then a strict ZIP reader: no
   ZIP64, encryption, links, absolute paths, `..`, duplicates or overlapping data; CRC-32 and a
   compression-ratio limit; exactly the files the manifest lists, each with its size and SHA-256.
7. Files are written to `staging/`, moved to `versions/<v>/`, then `active.json` is replaced.

Nothing from the archive is ever executed by the updater: no scripts, no installers. The new ELF
runs only when the user starts AKENO again from the launcher.

## In the app

Settings → Updates: Current version, Update channel, Check for updates, Automatic update checks,
Latest release, View changelog, Download and install (progress, speed, remaining bytes; Square
cancels, Cross retries), Restart to apply, Update history.

With automatic checks on, AKENO checks at start and every 6 hours while it runs, and asks
"A new AKENO NETWORK update is available. Install now?" with **Update** and **Later**. A version
is announced once; "Later" keeps it in Settings → Updates.

## Signing key (owner, once)

```
clients/ps5-native/tools/update-keygen.sh ~/akeno-update-key     # any folder outside the repository
```

- Store `akeno-native-update-private.pem` as the GitHub Actions secret `AKN_UPDATE_SIGNING_KEY`,
  keep one offline backup, delete other copies.
- Commit only the rewritten `src/core/update_key.h`.
- Losing the private key means every installed copy needs one manual install of a build with a new
  public key.

## Tests (PC)

- `tests/unit/test_update.c`: semver, release selection, manifest, signature, ZIP reader,
  install/boot/confirm/rollback, preferences and the screen model.
- `tests/updater/run.mjs`: `akn-update` against `mock-github.mjs` (rate limits, truncated,
  stalled, chunked and corrupt downloads, foreign redirects, HTTP 500, wrong keys and repositories,
  incompatible releases, an install killed half-way) and the `homebrew.js` launch logic.
- `tests/updater/ui.mjs`: the real app with a virtual controller: update question, install,
  restart, manual check, damaged download and retry, incompatible release.

None of this is PS5 evidence. The hardware test is in
`docs/HARDWARE_TEST_PLAN_PS5_NATIVE_1.2.0-beta.1.md`.
