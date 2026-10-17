# AKENO LAN Play — feasibility and transport research

This document answers the question the master spec asks first: **what can AKENO LAN Play actually
do, with which hardware, and what remains unproven?** It is deliberately blunt about limits. LAN
Play ships in this change as a **control plane only** — sessions, roles, authorization, credentials
and revocation inside AKENO parties — with **no network transport**. Every surface honestly reports
*"Transport not available"* until a verified transport is connected and tested on real hardware.

LAN Play is **not** SharePlay. SharePlay streams one device's screen and relays one controller.
LAN Play would carry two separate game instances' own LAN traffic so they see each other as if on
the same local network. They share the party UI and AKENO's authorization; nothing else.

## 1. What works today, what needs a bridge, what is unverified

| Capability | Status | Why |
| --- | --- | --- |
| Create / end a LAN session in an AKENO party | **Works today** | Pure control plane (this change). |
| Roles (host / player), membership checks, invitations reuse | **Works today** | Built on the existing party system. |
| Expiring connection credentials, revocation, reconnection | **Works today** | Implemented and tested with a stub transport. |
| Honest capability + compatibility reporting | **Works today** | `GET /lanplay/status`, `/lanplay/games`. |
| Actually moving a game's LAN packets (PC↔PC) | **Requires an external bridge** (not in this build) | Needs a transport that captures/forwards L2 frames; none is wired. |
| PS5 game traffic over the tunnel | **Requires a companion bridge next to the PS5** | A normal PS5 payload cannot capture another game process's Ethernet frames. |
| PS5↔PS5, PS5↔PC gameplay | **Unverified / blocked** | Needs the bridge **and** real hardware + a compatible game; not demonstrable in CI. |

Nothing here claims a working connection. A green CI run proves the control plane and the build,
never console compatibility (see `docs/SAFETY.md`).

## 2. My hardware and the exact limitations

- **PlayStation 5, firmware 12.20, jailbroken (kstuff), offline local account, no PSN.** LAN Play's
  value for this console is precisely that it targets **offline LAN / system-link** modes, which do
  not need PSN. It cannot and must not be used to reach a game's online PSN services.
- **Custom DNS `62.210.38.117` on the PS5.** This is a hard constraint. The AKENO LAN Bridge design
  (below) is **L2, device-scoped and host-side**: it filters by the console's MAC address and never
  changes the console's DNS, PSN routing, Sony certificates or any system network setting. Compare
  XLink Kai (§4), which *does* take over the console's addressing — that approach is rejected as the
  primary path for exactly this reason.
- **Windows 11 PC (Ryzen, RTX 5090).** This is the realistic place to run a bridge: it can capture
  Ethernet frames via Npcap and reach the Internet. It is the first target for a real transport.
- **Remote participants in Germany / the US.** High-latency, so the data plane must be UDP-first with
  a relay fallback and must measure latency/jitter/loss rather than hide them.

## 3. Fundamental technical truths (enforced in code and docs)

1. A LAN tunnel cannot create a multiplayer mode. LAN Play works **only** for titles with a genuine
   offline LAN / system-link mode; it never makes an online-only PSN game work without PSN, and a
   matching title on two platforms is not proof the builds interoperate.
2. A normal PS5 homebrew payload with socket access **cannot** transparently capture another game
   process's Ethernet frames. Native interception, injection, virtual adapters and kernel hooks are
   unverified and are **not** attempted. Byepervisor-Reborn is not a verified solution.
3. Therefore the realistic data plane is an **external companion bridge** next to each device
   (Track A). The native in-console adapter (Track B) stays **research-only / BLOCKED**.

## 4. Transport technology comparison

For each option: what it moves, whether it carries L2 broadcast/arbitrary protocols, OS-privilege
needs, whether it can reach a physical PS5, and licensing/security consequences.

| Technology | Transports | L2 broadcast? | Arbitrary TCP/UDP LAN? | Admin rights | Reaches a physical PS5? | Separate device? | License | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **XLink Kai** | Captured console Ethernet frames over its own network | Yes (its model) | Yes | Yes (pcap/Npcap) | **Yes** (PS4 mode on PS5) | Yes (PC/Pi) | **Closed-source freeware**, no redistribution | Proven hardware path, but see constraints below |
| **WireGuard** | IPv4/IPv6 packets (L3 VPN) | **No** (no Ethernet broadcast) | IP yes; L2 discovery no | Yes (adapter) | Only via extra L2 bridging | Yes (runs on a PC) | Open (GPLv2 / MIT tooling) | Great crypto, wrong layer for LAN discovery alone |
| **TUN** | L3 IP packets | No | IP only | Yes | No | Yes | Open | Same L2 gap as WireGuard |
| **TAP (virtual Ethernet)** | **L2 Ethernet frames** | **Yes** | **Yes** | Yes (driver) | Via bridging to the NIC | Yes | Open (OpenVPN/Wintun ecosystems) | The right layer; broad and risky if unscoped |
| **Npcap / Wintun (Windows)** | Raw frames (Npcap) / L3 (Wintun) | Npcap: yes | Npcap: yes | **Yes** | Yes (capture the port the PS5 is on) | Yes (the Windows PC) | Npcap: proprietary, **user-installed**, not redistributable | The capture primitive a Windows bridge would use |
| **QUIC** | Application streams/datagrams over UDP | n/a | As a carrier for our own framing | No | n/a | n/a | Open | A good *carrier* for tunneled frames across the Internet (NAT-friendly, encrypted) |

Key corrections the spec insisted on, confirmed by this comparison:

- **WireGuard alone does not provide Ethernet broadcast bridging.** It is an L3 VPN. Console LAN
  discovery that relies on L2 broadcast/multicast needs a TAP/Npcap-style L2 component in addition.
- **Not all console games discover peers the same way.** Some use directed UDP, some broadcast, some
  multicast; the bridge must support the mechanism each verified game actually uses, per title.

### XLink Kai specifics (and why it is a fallback, not the primary path)

XLink Kai is the closest proven tool and reaches a physical PS5 through **PS4 backward-compatibility
mode**. But per Team XLink's own documentation:

- It requires the console to take an address in the **`10.253.0.0/16`** range for PS4 use, and it
  **blocks the console's Internet** once connected. That directly conflicts with my PS5's custom DNS
  (`62.210.38.117`) and offline setup, which AKENO must not disturb.
- It is **closed-source freeware** with **no redistribution rights**, and compatibility is per title;
  cross-platform (PC↔console) play is explicitly not supported by it.

So XLink Kai is worth supporting as an **optional, clearly-labelled external proof-of-concept /
fallback** that the user runs themselves — never relabelled as "native AKENO networking", and never
driven through undocumented APIs or scraping. It is **not** the architecture AKENO builds on.

## 5. Could a supported third-party engine be integrated without reimplementing its packet forwarding?

**Yes — and that is the recommended direction.** The control plane in this change is built around an
explicit **transport adapter interface** (`apps/server/src/modules/lanplay/transport.ts`). A future,
independently maintained and verified networking component — for example a standalone **Wintun/TAP +
QUIC** bridge process, or a documented, supported integration surface of an existing LAN-tunnel
engine — can implement that interface and plug into AKENO's authorization, sessions and credentials
**without AKENO reimplementing any low-level packet forwarding itself**. The engine owns the data
plane; AKENO owns identity, membership and revocation.

This keeps us honest about the constraint from this development session: AKENO does **not** ship its
own packet-capture/injection engine here. The transport is a separate, swappable component.

## 6. Recommendation

1. **Track A, PC-first.** The first real transport should be a small companion bridge that runs on a
   Windows PC (and later Linux), using a user-installed capture primitive (Npcap/Wintun) and a
   NAT-friendly encrypted carrier (QUIC/UDP) to another bridge or an AKENO relay. Prove **PC↔PC**
   first, with real UDP delivery between two isolated environments, before anything console-related.
2. **PS5 via the Windows bridge**, device-scoped by MAC, L2, never touching the console's DNS/PSN.
3. **Track B (native in-console) stays research-only** until a documented, safe interface is proven.
4. **Integrate the transport through the adapter interface**, not by baking a VPN into the server.

The missing component is exactly that verified transport bridge. Everything above the transport —
parties, authorization, credentials, revocation, honest status — exists now and is tested.

See also: `LAN_PLAY_ARCHITECTURE.md`, `LAN_PLAY_SECURITY.md`, `LAN_PLAY_COMPATIBILITY.md`,
`LAN_PLAY_TEST_PLAN.md`.
