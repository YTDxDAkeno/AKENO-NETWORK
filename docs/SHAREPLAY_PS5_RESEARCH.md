# SharePlay — PS5 capture, virtual controller and LAN research

_Research date: 2026-10-16. Console: PS5, firmware 12.20, kstuff, ps5-payload-dev SDK pinned at
`f5d40f02770e9b0001dd9e18056eca70b3da8b40` (the SDK the AKENO console app is built with)._

This report answers two questions that decide whether "true" PS5 SharePlay is possible in AKENO
today: can our payload **capture the video of a running game**, and can it **create a second
controller that a game accepts as Player 2**? It also covers LAN tunnelling (Feature F).

Rules applied (from the project brief and `docs/SAFETY.md`): no undocumented function
signatures, no kernel patches or offsets, no cross-process memory hooks or code injection, no
claims without verification. Where a capability depends on any of these, it is reported as such
and **not built**.

Status labels: **VERIFIED ON HARDWARE** · **AUTOMATED TEST ONLY** · **SOURCE-VERIFIED** ·
**UNKNOWN** · **UNSUPPORTED**.

## 1. What the pinned SDK actually provides

The SDK ships headers only for its own helpers (`include/ps5/kernel.h`, `payload.h`, …) and
**stub libraries** (`sce_stubs/*.c`) that list exported symbol names so a payload can link
against system libraries. A stub is a name, nothing else: no prototype, no structure layout, no
semantics. Inspected directly in the SDK checkout:

| Library stub | Relevant names present | Declarations in the SDK |
| --- | --- | --- |
| `libScePad.c` | `scePadInit`, `scePadOpen`, `scePadRead`, `scePadReadState`, … and **`scePadVirtualDeviceAddDevice`, `scePadVirtualDeviceInsertData`, `scePadVirtualDeviceDeleteDevice`, `scePadRemoteControlInsertData`**, `scePadSetProcessPrivilege` | **None** |
| `libSceVideoOut.c` | `sceVideoOutOpen`, `sceVideoOutRegisterBuffers`, `sceVideoOutSubmitFlip`, `sceVideoOutGetBufferInformation`, … (display output for the calling process) | **None** |
| `libSceRemoteplay.c` | `sceRemoteplayInitialize`, `…GetConnectionStatus`, `…Prohibit`, `…ProhibitStreaming`, … (control of the system Remote Play server) | **None** |
| Screenshot / video recording / capture encoder (`libSceScreenShot`, `libSceVideoRecording`, `libSceAvCapture`, …) | **not present** | — |

## 2. Native gameplay capture — UNSUPPORTED

| Question | Finding | Label |
| --- | --- | --- |
| Can a payload read the framebuffer of another running title? | No SDK interface. `sceVideoOut*` drives the calling process's own display buffers; reading another process's buffers would need cross-process memory access, which AKENO does not do. | UNSUPPORTED |
| Screenshot interface usable for continuous capture? | No screenshot/recording stubs in the pinned SDK. Community apps (for example "PS5 Captures", "PShare") only **serve files the system already saved** (`Create` button captures) — not a live stream. | UNSUPPORTED (live) |
| Hardware H.264 encoder reachable from a payload? | No encoder interface in the SDK. | UNKNOWN |
| An existing PS5 homebrew that streams live gameplay? | None found. Remote-viewing projects found are AirPlay *receivers* (other devices → PS5) or Remote Play/Moonlight for reaching the console, not capturing from it. | UNKNOWN (none found) |
| The system's own Remote Play server | Exists (`libSceRemoteplay` controls it). Its stream is consumed by **official** clients (PS Remote Play for Windows/Android/iOS) or open-source ones (chiaki-ng). Reusing it from a payload is not possible with known interfaces. | SOURCE-VERIFIED (exists) / UNSUPPORTED (from AKENO) |

**Consequence in code:** `ps5-native` is a declared SharePlay source that the server always
refuses (`409 SOURCE_UNSUPPORTED`) and the UI shows as "not supported yet";
`akeno_sp_native_capture_open()` in `clients/ps5/app/shareplay_rx.c` returns `-1` with the
reason. It never falls back to test video.

**What works today instead (real PS5 picture, real game sound):**

| Path | Equipment | Notes |
| --- | --- | --- |
| HDMI capture card → PC → AKENO "Capture card" source | A UVC/USB capture card (ideally with HDMI passthrough so the TV still gets the picture), a Windows PC with Chrome/Edge | Lowest added latency of the options. **Turn off HDCP on the PS5** (Settings → System → HDMI → Enable HDCP), or the card receives a black picture. Some cards expose sound as a separate audio input; AKENO picks it up by name when it can, otherwise streams video only and says so. |
| Official PS Remote Play app on the host's PC → AKENO "Window" source | Windows PC on the same network as the PS5, PS Remote Play app | No extra hardware. Adds Remote Play's own encode/decode latency before AKENO's. Share **only the Remote Play window** (the start dialog says so). |
| chiaki-ng window → AKENO "Window" source | As above, with the open-source client | Same as above; unofficial. |

Neither path is "native PS5 capture"; both are labelled as what they are.

## 3. Virtual second controller (Player 2) — UNSUPPORTED in AKENO

### 3.1 Primary source: the SDK

`libScePad` **exports** `scePadVirtualDeviceAddDevice`, `scePadVirtualDeviceInsertData` and
`scePadVirtualDeviceDeleteDevice` (SOURCE-VERIFIED: names in `sce_stubs/libScePad.c`). The SDK
declares **no prototypes or data structures** for them. Under the project rules AKENO cannot call
them: their signatures are undocumented.

Note that a function for *reading* pads (`scePadRead*`) says nothing about *creating* one; only
the `VirtualDevice*` names suggest creation, and only by name.

### 3.2 Community implementations (read, not copied)

| Project | What it claims | How (from its source, read on 2026-10-16) | Compatible with AKENO's rules? |
| --- | --- | --- | --- |
| [YetAnotherControllerEnabler](https://github.com/MegaCadeDev/YetAnotherControllerEnabler) (GPL-3.0) | Publishes a virtual DualSense that games read like a real one | Own prototypes (`include/pad_data.h`) for `scePadVirtualDeviceAddDevice(void *param, int type)` with a **guessed parameter struct** (unknown fields filled with sentinel values and scanned afterwards for the handle); **raises its own kernel credentials** (`kernel_set_ucred_authid/caps`); **attaches with ptrace to SceShellCore and SceShellUI**, changes memory protection and calls functions inside them to create the pad and to press PS for player assignment | **No** (undocumented signatures, kernel credential changes, cross-process injection) |
| Ghostpad / GhostControl (forks) | Virtual DualSense over TCP / USB controllers via a virtual pad | Per their READMEs and PRs: patching `libScePad` inside SceShellCore and dismissing the assignment screen through MBus calls; on 13.60 injection from the payload process fails (`0x803B0003`) so creation and injection move **into SceShellCore** | **No** |
| [remotePad](https://github.com/xfangfang/remotePad) / Control4Free (PS4, GoldHEN) | Virtual DualShock 4 controllers on PS4 | PS4 + GoldHEN plugin system; not applicable to PS5 | Not applicable (PS4) |

Reported error codes from these projects (unverified by us): `0x803B0006` from `AddDevice`
while the console's "who is using this controller?" assignment screen is pending; `0x803B0003`
from `InsertData` called from a payload process on 13.60.

**Assessment.** A virtual pad *appears* to be technically possible on some firmware, but every
known implementation relies on guessed structures plus privilege escalation and code injection
into system processes, which is exactly what AKENO must not do. Whether a **safe, documented,
same-process** path exists is **UNKNOWN**. Therefore the adapter stays disabled:
`akeno_sp_virtual_pad_open()` always returns `-1` with the reason, and the web app states that
Player 2 input "controls nothing" on the console.

### 3.3 What would be needed (not done)

1. A documented prototype and structure layout for `scePadVirtualDevice*` from a credible source,
   or a reviewed reverse-engineering result accepted by the project owner as an explicit
   exception.
2. Proof that a payload can create and feed the pad **in its own process**, without raising
   kernel credentials or touching SceShellCore/SceShellUI.
3. A way to assign the virtual pad to a **second local user** (PS5 local multiplayer generally
   needs each controller signed in to a user — UNKNOWN whether this can be done without the
   assignment screen workaround above).
4. A gameplay-only filter at the injection point (PS/Create are already blocked in transport).

Only then a separate, experimental ELF (like the audio lab) and the Stage D hardware test.

### 3.4 Hardware-assisted alternative (research only, not built)

PS5 games accept only authenticated controllers (DualSense, or licensed third-party
controllers/adapters with an authentication chip). A **hardware** Player 2 would be: guest's
controller → AKENO controller link → a PC or microcontroller next to the PS5 → a USB device the
PS5 accepts as a licensed controller. Building one requires licensed authentication hardware;
whether existing open-source gamepad firmware can do this for PS5 games without such hardware
is **UNKNOWN** (not verified in this session). It would **not** be a software-only solution.

## 4. Capability matrix

| Capability | Status | Evidence |
| --- | --- | --- |
| Receiving a PC gamepad in the browser (Gamepad API, standard layout) | **AUTOMATED TEST ONLY** | `apps/web/tests/unit/shareplay.test.ts`, `shareplay.spec.ts` (scripted gamepad) — no real controller in CI |
| Transporting controller state (guest → server → host) with grant, epoch, sequence, rate limit, release | **AUTOMATED TEST ONLY** | `apps/server/test/shareplay.test.ts` (21), browser E2E with real LiveKit |
| Reading the relayed state in the AKENO console app (diagnostic receiver, watchdog) | **AUTOMATED TEST ONLY** (Linux host build) | `clients/ps5/tests/unit/test_shareplay.c`, integration step "receives SharePlay Player 2 input"; PS5 ELF compiles and passes the import gate, **never run on a PS5** |
| Creating a virtual gamepad | **UNSUPPORTED** (in AKENO) | §3: undocumented signatures; known methods need kernel credential changes and injection |
| Assigning it as Player 2 | **UNKNOWN** | §3.3 |
| A PS5 game recognizing it | **UNKNOWN** for AKENO (community claims only) | §3.2 |
| Separate Player 1 and Player 2 control | **UNKNOWN** | needs the above |
| Returning control safely after disconnection | **AUTOMATED TEST ONLY** for the transport (release + 500 ms watchdog at every receiver); UNKNOWN at a console adapter | tests above |
| Native PS5 game capture | **UNSUPPORTED** | §2 |
| Streaming real PS5 gameplay via capture card / Remote Play window | Implemented in the browser (**AUTOMATED TEST ONLY** with generated video); **not verified with a PS5** | Stage A/C in `SHAREPLAY_HARDWARE_TEST.md` |

## 5. LAN multiplayer over the internet (Feature F) — feasibility only

- **Works only for games with a real LAN mode** (the game finds other consoles on the local
  network and plays over it). It cannot turn split-screen/local co-op or online-only games into
  LAN games. This report does not list compatible PS5/PS4 titles: none were verified, and game
  compatibility must not be invented.
- **Mechanics:** LAN games typically discover each other with UDP broadcast or multicast, which
  does not cross routers. A tunnel must bridge **layer 2** (broadcasts) between two homes, e.g. a
  PC on each side bridging the console's network segment into a VPN (tools of the XLink Kai /
  bridged-VPN family). Routed VPNs (plain WireGuard/Tailscale) do not forward broadcasts without
  extra relays.
- **On the console:** the PS5 has no user-configurable VPN client. A jailbroken payload acting as
  a VPN would mean changing the console's network stack — not done.
- **NAT traversal:** the bridge PCs, not the consoles, would handle NAT (relay or hole punching).
- **Conclusion:** possible only with a PC bridge per home and only for verified LAN-capable
  titles; latency is that of the tunnel. **No prototype was built**: without one verified
  compatible title there is nothing to test it against. It stays a research item; SharePlay
  (stream + Player 2) remains the primary goal.

## Sources

- ps5-payload-dev SDK at the pinned commit: `sce_stubs/libScePad.c`, `libSceVideoOut.c`,
  `libSceRemoteplay.c`, `include/ps5/*.h` (read locally).
- [MegaCadeDev/YetAnotherControllerEnabler](https://github.com/MegaCadeDev/YetAnotherControllerEnabler) — `include/pad_data.h`, `src/pad_assign.c` (read locally at `3fd0665`).
- [AssPayload/Ghostpad](https://github.com/AssPayload/Ghostpad) (search result; repository returned 404 on fetch), [psx-place thread](https://www.psx-place.com/threads/release-ghostpad-virtual-dualsense-over-tcp-for-jailbroken-ps5-payload-pc-gui.50468/).
- [REXCRAFT88/Ghostcontrol-PS5-USB-Controller-expanded](https://github.com/REXCRAFT88/Ghostcontrol-PS5-USB-Controller-expanded) and its [PR #18](https://github.com/REXCRAFT88/Ghostcontrol-PS5-USB-Controller-expanded/pull/18); [StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher](https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher).
- [xfangfang/remotePad](https://github.com/xfangfang/remotePad), [MoHadiShibli/Control4Free](https://github.com/MoHadiShibli/Control4Free/tree/main) (PS4).
- [The7mirrors/ps5-captures](https://github.com/The7mirrors/ps5-captures), [PShare](https://homebrew.page/app/PPSA99773/), [GitHub topic ps5-homebrew](https://github.com/topics/ps5-homebrew).
- [GP2040-CE](https://github.com/OpenStickFoundation/GP2040-CE) (hardware gamepad firmware; PS5 authentication details not verified).
