# AKENO LAN Play — transport proof and the plan to real multiplayer

This records a **real networking proof** run with an established, maintained tool (not a
reimplemented packet engine), what it does and does not prove, a comparison of SoftEther vs
ZeroTier for our needs, how an **unmodified PS5** can be bridged, the exact game/platform pick for a
later hardware test, and the shortest concrete setup plan to reproduce the proof on real machines.

No packet-capture/injection/forwarding engine was written. The proof uses SoftEther VPN through its
documented commands plus standard OS tools (network namespaces, ordinary UDP sockets).

## 1. What was proven (reproducible)

Harness: `docs/lanplay-proof/run-proof.sh` (+ the four `*.py` helpers). On a Linux host with
`/dev/net/tun`, it starts **SoftEther VPN 5.01 (Build 9674)**, creates an L2 **Virtual Hub**,
connects **two client adapters** to it, isolates each in its own **network namespace** (two
"isolated PC environments" on one virtual LAN), and runs real traffic.

Measured results (this container, clean virtual link):

| Check | Result |
| --- | --- |
| L3 reachability over the tunnel (`ping`, ns1→ns2) | **4/4, 0% loss**, rtt avg 1.9 ms |
| ARP across the hub | peer MAC `5e:01:c0:a7:…` learned → genuine **L2** segment |
| **Bidirectional UDP** (500 datagrams, echoed) | **500/500 echoed, 0% loss**, rtt avg 1.03 ms, p95 1.61 ms, jitter 0.04 ms |
| **UDP broadcast discovery** (`255.255.255.255` beacon, ns1→ns2) | **received** across the L2 segment |
| SoftEther hub counters | ~1,000 unicast pkts each way + broadcast pkts → frames really traversed SoftEther |

**Why the broadcast result matters:** most console/PC LAN games discover peers with UDP
broadcast/multicast. A SoftEther (or ZeroTier) **L2** overlay carries that; a plain **L3** VPN like
WireGuard does **not** (it would need an extra L2 bridging component). This is the single most
important capability for LAN Play and it works.

## 2. What this does NOT prove (honest limits)

- **Not a game test.** No game ran. It proves the transport mechanics (UDP + broadcast + an L2
  segment), which are necessary but not sufficient for multiplayer. Real gameplay is only proven
  when two actual game instances exchange state (see §5).
- **No WAN emulation here.** The container kernel lacks `sch_netem`, so Germany↔US latency/loss was
  not emulated in-container. Re-run with `WAN=1 sudo bash run-proof.sh` on a host that has
  `sch_netem` to measure behaviour at 120 ms / 2% loss. The clean-link numbers above are a floor.
- **Different OS than the target.** This ran on Linux; the user's bridge would be Windows 11 (or a
  small Linux box). The setup differs (see §4), though SoftEther's capabilities are the same.
- **No PS5 was involved.** PS5 bridging is designed and planned (§3), not yet demonstrated.

## 3. SoftEther vs ZeroTier, and bridging an unmodified PS5

Both are established, maintained, open-source, and provide an **L2 virtual Ethernet** with
broadcast/multicast — so both can, in principle, carry LAN game discovery. Neither makes an
online-only game work; both only carry genuine LAN traffic.

| | **SoftEther VPN** | **ZeroTier** |
| --- | --- | --- |
| Layer | L2 Virtual Hub (Ethernet) | L2 virtual switch (Ethernet) |
| Broadcast/multicast | Yes (proven above) | Yes (multicast replication) |
| Self-hostable control | Yes, fully (own server) | Yes (self-hosted controller) or free hosted |
| Bridge to a physical LAN | **Local Bridge** to a NIC (documented, first-class) | Linux `brctl`/`ip link` bridge with `allowEthernetBridging` |
| OS for the bridge box | Windows / Linux / BSD | Windows / Linux / macOS / routers |
| License | Apache-2.0 | BSL 1.1 (source-available) |
| Admin rights | Yes (installs a virtual adapter) | Yes (installs a virtual adapter) |

**Bridging an unmodified PS5 (both tools, same shape):**

```
   PS5 (unmodified)                Bridge box (Windows/Linux, nearby)           Internet        Remote peer's
   firmware 12.20, offline     ┌───────────────────────────────────────┐                        bridge + device
   custom DNS 62.210.38.117    │  NIC facing PS5  ── bridged ── VPN      │  ── encrypted ──▶  (same hub/network)
        │  Ethernet            │  (SoftEther Local Bridge, or Linux      │     overlay
        └──────────────────────▶  bridge of the ZeroTier iface)         │
                               └───────────────────────────────────────┘
   The PS5 sees extra hosts on its LAN. Nothing on the console changes.
```

- The console is **never** modified: its firmware, offline account, **DNS `62.210.38.117`** and
  jailbreak are untouched. The bridge box does all the work and sees the PS5 by its MAC.
- **Scope it:** put the PS5 on a **dedicated NIC/segment** of the bridge box so only the PS5 is
  bridged, not the whole home LAN. This matches AKENO's device-scoping security rule
  (`LAN_PLAY_SECURITY.md`).
- **Do not** bridge onto the VPS or production. Run the bridge on the Windows PC or a small Linux
  box the user controls.

**Recommendation:** start with **SoftEther** for the PS5 bridge — its *Local Bridge* is a
documented, first-class feature and it is fully self-hostable with an Apache-2.0 license. Keep
ZeroTier as an alternative (simpler NAT traversal, but Linux-centric bridging and a source-available
licence to review).

## 4. Shortest concrete setup plan (reproduce on real hardware)

**Windows 11:** ready-to-run PowerShell tools automate steps A.1–A.3 below with safety gates on every
system-level change — see `docs/LAN_PLAY_WINDOWS_PROOF.md` and `docs/lanplay-proof/windows/`.

**A. PC ↔ PC first (no console):** ~30 min.
1. On two Windows 11 PCs (or two Linux boxes), install SoftEther VPN Server on one, VPN Client on
   both (or Server+Client on one and Client on the other). Create a Virtual Hub `AKENO`, two users.
2. Connect both clients; give each virtual adapter a static `10.9.0.x/24`.
3. Verify: `ping` across; run the `udp_client.py`/`udp_echo.py` from `docs/lanplay-proof/`; run the
   broadcast test. Expect 0% loss on a LAN, measurable loss/latency across the Internet.
4. Launch a PC game with a real **LAN/system-link** mode; host on one, confirm it appears in the
   other's LAN browser; play.

**B. PS5 through a Windows/Linux bridge:** add a bridge box on the PS5's LAN.
1. Wire the PS5 to a **second NIC** of the bridge box (keep the console's DNS/account as-is).
2. SoftEther: enable a **Local Bridge** from Virtual Hub `AKENO` to that NIC. (ZeroTier: bridge the
   ZeroTier interface to that NIC with `allowEthernetBridging`.)
3. The remote peer joins the same hub/network through their own bridge.
4. Host the game's LAN mode on one side; confirm discovery and join on the other.

Capture for each run: software versions, `ping`/UDP stats, packet counts, the game's discovery and
join result, screenshots, and an explicit note of what was emulated vs hardware-tested. Never report
multiplayer as working until two real game instances exchange gameplay state.

## 5. Exact game / platform pick for the hardware test

- **Primary (no accounts needed): F1 23 — PS5 ↔ PS5, 2-player LAN.** F1's LAN mode is
  community-reported to work between PS4/PS5 on one network and needs no PSN, which suits the offline
  console. **Caveat:** needs a *second* PS5 (or a remote participant who has one); with one console
  it stays **pending**. Treat as `community-reported`, not verified, until the hardware test.
- **Broadcast-discovery candidate to verify: Minecraft (Bedrock) — PC ↔ PS5.** Bedrock discovers LAN
  games via UDP broadcast (exactly what §1 proved the overlay carries). **Caveat:** Bedrock usually
  requires a Microsoft-account sign-in even for LAN and may route some setup through Xbox services,
  so it is *not* a clean "no-account" case; verify sign-in behaviour before relying on it.
- **Immediately doable now: PC ↔ PC** with any PC title that has a documented offline LAN/system-link
  mode — the fastest path to a first genuine multiplayer result while a second console is arranged.

Do not claim PS5↔PC crossplay for any title without evidence for those exact builds.

## 6. Integration gate

Per the plan: AKENO integrates a transport into `LanTransportAdapter`
(`apps/server/src/modules/lanplay/transport.ts`) **only after** the transport itself is verified on
real hardware with a real game (§4–§5). Until then LAN Play keeps reporting "Transport not
available". The control plane already issues, expires and revokes the short-lived credential such an
adapter would hand to the bridge, so wiring a verified SoftEther/ZeroTier-backed bridge later does
not change the party system.
