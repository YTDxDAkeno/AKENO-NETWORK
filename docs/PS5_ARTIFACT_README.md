# AKENO NETWORK for PS5 — Alpha 0.5 release candidate (build artifact)

This folder is the output of the **PS5 console build** GitHub Actions workflow. Nothing in it has
run on a PS5 yet; test it with `HARDWARE_TEST_PLAN.md` (PS5 firmware 12.20 with kstuff).

## Contents

| File | Use |
| --- | --- |
| `akeno-network-ps5.elf` | **Release candidate.** Messaging, game activity, notifications as before, plus profiles, Quick Access and the controller shortcut experiment (off by default). |
| `akeno-network-ps5-audiolab-v2.elf` | **Experimental audio lab v2.** The same app plus Diagnostics → Audio lab v2. Only for test C. |
| `fallback/akeno-network-ps5-previous-ae94851.elf` | The previous release build (`ae94851`), byte-identical to CI run 38558019000. |
| `fallback/akeno-network-ps5-audiolab-v1-ae94851.elf` | The audio lab v1 you tested (`ae94851`), byte-identical to CI run 38558019000. |
| `fallback/akeno-network-ps5-alpha-0.1.elf` | Alpha 0.1, byte-identical to the first build that ran on your PS5. |
| `debug/` | Unstripped ELF files (symbols, for crash analysis) and the toolchain diagnostic. Not needed to use AKENO. |
| `SHA256SUMS` | Checksums of every ELF. |
| `BUILDINFO.txt` | Source commit, workflow run, SDK, compiler, imports and hardware status. |
| `HARDWARE_TEST_PLAN.md` | Tests A (release), B (quick access) and C (audio). |
| `RELEASE_NOTES.md`, `AUDIO_RESEARCH.md` | What changed; the audio findings. |

## Download

1. Open the repository on GitHub → **Actions** → **PS5 console build**.
2. Select the run of the commit you want (branch `claude/laughing-ptolemy-op1d4e`; `BUILDINFO.txt`
   names the commit).
3. At the bottom of the run page, under **Artifacts**, download **akeno-network-ps5-alpha-0.5-rc**
   (everything above, as a ZIP). Single files are also there: **akeno-network-ps5-elf** and
   **akeno-network-ps5-audiolab-v2-elf** (GitHub wraps each in a ZIP too). You must be signed in to
   GitHub to download artifacts; they are kept for 90 days.
4. Unzip, then check the files:
   - Linux/macOS: `sha256sum -c SHA256SUMS`
   - Windows PowerShell: `Get-FileHash .\akeno-network-ps5.elf -Algorithm SHA256` and compare with
     `SHA256SUMS` / `BUILDINFO.txt`.

## Install and start

Send the ELF with the same loader you used before (nothing in this project sends files to a console
automatically). AKENO keeps everything in `/data/akeno`: your pairing and settings carry over between
all builds in this folder. Only one AKENO runs at a time — use **Exit AKENO** before starting another
build; sending the *same* build again just reopens its page.

## Going back

Exit AKENO and send a file from `fallback/`. Nothing else needs to be undone: no build changes
system files or the server.

## Safety summary

- The release ELF imports exactly the same system functions as the build that already ran on your
  PS5; new controller functions are loaded only when you turn the shortcut on.
- No game process injection, kernel hooks, firmware offsets or input interception; the PS button is
  never touched; no microphone code exists; audio plays only after you confirm a test.
- Logs and the audio report contain no tokens or passwords.
