# AKENO LAN Play — Windows 11 transport proof (beginner guide)

This is a **ready-to-run, Windows 11 version** of the SoftEther LAN transport proof. It builds a
real Layer-2 virtual LAN between two Windows PCs and measures genuine network traffic across it. It
uses the maintained, open-source **SoftEther VPN** through its documented command-line tool
(`vpncmd`); **it contains no packet-capture, packet-injection or packet-forwarding engine of its
own** — SoftEther carries the traffic, and the scripts only orchestrate it and measure the result.

> **What this proves and what it does not.** This is a **transport test**, not a game test. A pass
> shows the overlay carries bidirectional UDP and LAN-discovery broadcasts — the mechanics LAN
> multiplayer needs. It does **not** by itself prove any game works. A separate, clearly-labelled
> **game test** (§7) is how you confirm real multiplayer. Nothing here is integrated into production
> AKENO; LAN Play still reports "Transport not available" in the app until a transport is verified.

**Honesty note from the author (Claude):** I wrote and statically reviewed these scripts, and I
verified the underlying harness on Linux (results in `docs/LAN_PLAY_TRANSPORT_PROOF.md`). I have
**not** run them on real Windows hardware, and no PS5 was involved. The Windows and game results
below are the ones **you** will produce; everything is labelled accordingly.

---

## 0. Files in this proof

Under `docs/lanplay-proof/`:

| File | What it is |
| --- | --- |
| `windows/Setup-AkenoLanProof.ps1` | Sets up one machine (Host or Join): SoftEther hub/user/adapter, connects it. |
| `windows/Run-TransportTest.ps1` | Runs the UDP + broadcast tests and saves results. |
| `windows/Test-Diagnostics.ps1` | Read-only health check of the connection. |
| `windows/Cleanup-AkenoLanProof.ps1` | Undoes everything the setup created. |
| `windows/AkenoLanProof.Common.ps1` | Shared helpers (not run directly). |
| `udp_echo.py`, `udp_client.py` | Bidirectional-UDP latency/loss test (echo server + client). |
| `bcast_listener.py`, `bcast_send.py` | UDP broadcast discovery test. |
| `run-proof.sh` | The original Linux harness (for reference / Linux boxes). |

The PowerShell scripts **reuse the Python helpers** above, so Python 3 is required on the machine
that runs `Run-TransportTest.ps1`.

---

## 1. What to install (nothing is installed for you)

On **both** PCs:

1. **SoftEther VPN** — download from the official site: <https://www.softether-download.com/en.aspx>
   (select "SoftEther VPN (Freeware)", your component, "Windows", the latest build).
   - On the **Host** PC install **both** "SoftEther VPN Server" **and** "SoftEther VPN Client".
   - On the **Join** PC install just "SoftEther VPN Client".
   - Let the installer register and start the Windows services (default). The scripts talk to these
     services locally; they never reach out to SoftEther's network.
2. **Python 3** — <https://www.python.org/downloads/>. During install, tick **"Add python.exe to
   PATH"**. Only the machine that runs the transport test needs it (install on both to be safe).

The scripts **never download or install anything**. If SoftEther or Python is missing, the script
stops and prints the link above.

You run the scripts in **Windows PowerShell as Administrator** (SoftEther's virtual adapter and IP
assignment need admin rights). If scripts are blocked, allow them for this session only:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

---

## 2. Which machine is the Host, which IPs

- Pick one PC as the **Host**. It runs the SoftEther server (the Virtual Hub named `AKENO`) and also
  joins its own hub so it has a virtual adapter.
- The other PC is the **Join** machine.
- On the virtual LAN both machines share subnet **`10.9.0.0/24`**:
  - Host virtual-LAN IP: **`10.9.0.1`**
  - Join virtual-LAN IP: **`10.9.0.2`**
  - Broadcast address for that subnet: **`10.9.0.255`**
- You also need the **Host's real IP on your normal network** (e.g. `192.168.1.50`) so the Join
  machine can reach the server. The Host setup prints this for you. Find it manually with `ipconfig`
  (the IPv4 address of your Wi-Fi/Ethernet adapter).

The two PCs must be able to reach each other's real IP (same home network, or a reachable address
across the Internet with UDP/TCP 443 open to the Host). On the same home LAN this just works.

---

## 3. Configure SoftEther and connect (the Setup script does this)

**On the Host** (elevated PowerShell), from `docs/lanplay-proof/windows`:

```powershell
.\Setup-AkenoLanProof.ps1 -Role Host -OpenFirewall
```

It will:
- start the SoftEther services,
- create the `AKENO` Virtual Hub and a user with a **randomly generated password** (never
  hardcoded),
- **ask you to approve** creating the SoftEther virtual network adapter (this installs SoftEther's
  virtual NIC driver — the only driver the proof needs, and the Cleanup script removes it),
- **ask you to approve** assigning `10.9.0.1` to that adapter,
- with `-OpenFirewall`, **ask you to approve** a firewall rule that allows the test's UDP ports
  (40000, 45000) inbound **on the virtual adapter only**,
- print a block of details to hand to the other PC (Host IP, hub name, username, password).

**On the Join machine** (elevated PowerShell), paste the details the Host printed:

```powershell
.\Setup-AkenoLanProof.ps1 -Role Join -ServerIp <HOST-IP> -UserName player1 -Password <PRINTED-PASSWORD> -OpenFirewall
```

It asks for the same approvals and assigns `10.9.0.2`.

> **Every system-level change is gated.** Creating the virtual adapter, assigning an IP, and adding
> a firewall rule each print `SYSTEM CHANGE: …` and only proceed when you type `yes`. To pre-approve
> (after reading what each does), add `-Yes`. The scripts **never** change your routing table and
> **never** bridge a physical network adapter in this PC↔PC proof.

---

## 4. Firewall permissions

Windows Defender Firewall is the single most common reason a LAN test shows loss or "no discovery".
`-OpenFirewall` offers to add one **scoped** rule: inbound **UDP 40000 and 45000**, **on the
SoftEther virtual adapter only**, so your real network is unaffected. The rule's name is recorded so
Cleanup removes exactly it. If you skip it and a test fails, re-run Setup with `-OpenFirewall`, or add
the rule yourself in "Windows Defender Firewall with Advanced Security".

(The PC **game** test in §7 uses the game's own ports — e.g. OpenArena's UDP 27960–27963 — which the
game or you must also allow on the virtual adapter.)

---

## 5. Launch the transport test

Both machines must be set up and connected first (check with §6). Then:

1. On the machine at **`10.9.0.2`** (the echo/listen side), run:
   ```powershell
   .\Run-TransportTest.ps1 -Mode Listen
   ```
   It starts the UDP echo server and waits for a broadcast beacon (60 s).
2. On the machine at **`10.9.0.1`** (the client/send side), run:
   ```powershell
   .\Run-TransportTest.ps1 -Mode Send -PeerIp 10.9.0.2
   ```
   It pings the peer, sends 500 UDP datagrams and measures echoes, then sends one broadcast beacon.

Results are saved under `windows\results\<timestamp>\` (`ping.txt`, `udp.txt`, `broadcast.txt`,
`summary.json`) and printed on screen.

---

## 6. How to recognise success

Run the read-only diagnostics any time:

```powershell
.\Test-Diagnostics.ps1 -PeerIp 10.9.0.2
```

Every line should read **`[PASS]`**: SoftEther found, client service running, virtual adapter
present, `10.9.0.x` assigned, account connected, (firewall rule present), peer reachable.

For the transport test, success looks like:
- **Ping:** replies from `10.9.0.2`.
- **UDP:** `sent=500 echoed=500 loss=0.0%` on a wired LAN (a few percent loss and higher latency is
  normal across the Internet — that is real WAN behaviour, not a failure).
- **Broadcast:** the **Listen** machine prints `DISCOVERY RECEIVED …`. This is the key result: it
  means LAN-game discovery broadcasts cross the overlay. (A plain Layer-3 VPN like WireGuard cannot
  do this; SoftEther's L2 hub can, which is why it was chosen.)

Remember: this is the **transport** passing. Game multiplayer is proven only by §7.

---

## 7. Real multiplayer test — a specific, LAN-capable PC game

Keep this **separate** from the transport test. Verify the game and its exact version before relying
on it.

### Primary pick: OpenArena (free, no account)

- **Why:** OpenArena is free and open-source, needs **no online account**, and is built on the
  **id Tech 3 (Quake III)** engine, whose LAN server browser discovers servers by **UDP broadcast**
  on ports **27960–27963** — exactly the mechanism §5's broadcast test validates.
- **Version to use:** install the **same version on both PCs** (current release **0.8.8**). Mismatched
  versions won't see each other.
- **Download:** <https://openarena.ws/download.php>.
- **Evidence level:** `lan-documented` — the LAN browser and UDP ports are well established for the
  Quake 3 engine; the exact discovery packet format was not independently reconfirmed here, and it has
  **not** been tested over the AKENO tunnel yet. Treat a successful run as the first verification.
- **Firewall:** allow UDP **27960–27963** inbound on the **virtual adapter** on the machine hosting
  the game (same idea as §4).
- **Steps:** (1) confirm LAN works normally first — host a server on one PC and join from the other on
  your ordinary LAN; (2) then repeat with each PC using its `10.9.0.x` virtual-LAN address, hosting
  the server so the other finds it in the **Multiplayer → Local/LAN** browser; (3) play a match.
- **Success = two real game instances discover each other over the virtual LAN and play.** Record:
  both versions, who hosted, the virtual-LAN IPs, whether it appeared in the LAN browser, join, and
  stability. Only then is multiplayer "working" for this title.

### Secondary: Factorio (paid)

- Factorio has a **"Browse LAN games"** mode that lists games on the local network, and LAN works
  without port-forwarding. **Caveats to verify yourself:** all instances must run **exactly the same
  version and mods**; whether LAN needs no account is **ambiguous for 2.0** (sources conflict, and you
  can't run two simultaneous copies on one Steam account — a second non-Steam copy from factorio.com
  needs a factorio.com login). Verify in-game before relying on it.
- Sources:
  [official wiki — Multiplayer](https://wiki.factorio.com/Multiplayer),
  [FFF #139 (LAN without internet)](https://forums.factorio.com/viewtopic.php?p=163090).

### Other notes

- Any PC title with a genuine **offline LAN / system-link** mode works in principle; a matching title
  on two platforms is **not** proof the builds interoperate.
- Sources for OpenArena LAN/ports:
  [NixOS wiki — OpenArena (UDP 27960–27963 must be open for LAN)](https://wiki.nixos.org/wiki/OpenArena),
  [Quake 3 server protocol (UDP-only, port 27960, auto-increment to 4 servers)](https://mirrors.lug.mtu.edu/idgames/idstuff/quake3/docs/server.txt).

---

## 8. How to undo everything

On each machine (elevated PowerShell):

```powershell
.\Cleanup-AkenoLanProof.ps1
```

It disconnects and deletes the SoftEther account, removes the `10.9.0.x` IP, deletes the virtual
adapter, removes the proof's firewall rule, and (on the Host) deletes the `AKENO` hub and user — each
step gated by a `yes`. It does **not** uninstall SoftEther (do that from Settings → Apps if you want),
and it changes **nothing** in your routing table or on any physical adapter. Add `-Yes` to approve
all removals at once.

---

## 9. PS5 bridge stage — research only (nothing is deployed to the PS5)

This section is **design and verification steps**, not something the scripts do, and **nothing here
touches the PS5**. The goal is to let an **unmodified PS5** appear on the same SoftEther virtual LAN
as a remote peer, by putting a **bridge box** (a Windows or Linux PC) between the console and the
network — without replacing the console's DNS or breaking its Internet.

### The shape

```
  PS5 (unmodified)                  Bridge box (Windows/Linux)                Internet       Remote peer
  firmware 12.20, offline       ┌──────────────────────────────────────┐                     + their
  DNS 62.210.38.117, as-is      │  NIC-A facing the PS5                  │  ── encrypted ──▶   bridge/device
       │  Ethernet              │  NIC-B / Wi-Fi facing your real LAN    │     SoftEther        on hub AKENO
       └────────────────────────▶ SoftEther Local Bridge  ⇄  Virtual Hub │     overlay
                                └──────────────────────────────────────┘
  The PS5 sees extra hosts on "its LAN". The console is never changed.
```

### Do NOT assume a dedicated NIC preserves the PS5's Internet

Plugging the PS5 into a second NIC of the bridge box does **not** automatically keep its Internet
working. Before any bridge is enabled you must **verify the topology**, because a PS5 with no
gateway/DNS path will drop off the Internet:

1. **Addressing.** How does the PS5 currently get its IP — DHCP from your router, or static? If DHCP,
   the bridge must let the PS5 keep reaching that same DHCP server (so bridging the PS5 NIC onto the
   segment that carries the router is required, not a separate isolated NIC with nothing behind it).
2. **Gateway.** Identify the PS5's current default gateway (your router). After bridging, confirm the
   PS5 still resolves and reaches that gateway — test the console's own "Test Internet Connection".
3. **DNS.** The console's DNS (`62.210.38.117`) must keep working unchanged. The bridge must **not**
   intercept, rewrite, or replace DNS. Verify a name resolves from the console after bridging.
4. **Routing on the bridge box.** The bridge box must **forward Layer-2 frames** between the PS5 NIC
   and the hub, **not** NAT or route the PS5 behind a new gateway (that would change the console's
   addressing). Confirm the bridge is L2 (SoftEther *Local Bridge*), and that the box's own routing
   table is unchanged for its real adapter.
5. **Scope.** Bridge **only** the PS5's segment, not your whole home LAN, to match AKENO's
   device-scoping rule (`docs/LAN_PLAY_SECURITY.md`).

Only after all five are verified on paper and with the console's connection test should a Local Bridge
be enabled. If any check fails (e.g. the PS5 uses DHCP from the router but the bridge would isolate it
from that router), **stop and report** — do not proceed.

### Remaining PS5 limitations (honest)

- **Not demonstrated.** No PS5 was bridged or tested; this is a verified-on-paper design only.
- **The console is never modified:** firmware 12.20, offline account, DNS `62.210.38.117` and jailbreak
  state are all untouched. The bridge box does all the work and only sees the PS5 by its MAC address.
- **A game must have a genuine offline LAN mode** for any of this to matter; an online/PSN-only title
  will not work over a LAN overlay (see `docs/LAN_PLAY_COMPATIBILITY.md`).
- **PS5↔PS5** needs a second console or a remote participant who has one; with a single console it stays
  **pending**.
- **Never bridge onto the AKENO VPS or any production host.** Run the bridge on a PC you control.

---

## 10. What I (Claude) tested vs what you need to run

**Tested by me (on Linux, reproducible — see `docs/LAN_PLAY_TRANSPORT_PROOF.md`):**
- SoftEther L2 Virtual Hub carrying bidirectional UDP: **500/500 echoed, 0% loss**, rtt avg ~1 ms.
- UDP broadcast discovery across the L2 segment: **received**.
- Hub counters confirm the frames really traversed SoftEther.
- The parameterized Python helpers and the `run-proof.sh` harness, re-run end-to-end after
  parameterization.

**You need to run (Windows / game / PS5 — not done by me):**
- The four PowerShell scripts on two real Windows 11 PCs (§3–§6). Expected: the same UDP/broadcast
  results as above on a wired LAN; real loss/latency across the Internet.
- The PC **game** test (§7) — two OpenArena instances discovering and playing over the virtual LAN.
- The PS5 bridge (§9) — only after verifying the topology; nothing is deployed to the console.

**Troubleshooting quick table:**

| Symptom | Likely cause / fix |
| --- | --- |
| `vpncmd.exe not found` | Install SoftEther (Server+Client on Host, Client on Join); re-run. |
| Setup can't create the adapter | Run PowerShell **as Administrator**; approve the `SYSTEM CHANGE` prompt. |
| Diagnostics: account not connected | Wrong Host IP/username/password, or TCP 443 to the Host is blocked. |
| UDP loss 100% / no echo | Start the **Listen** side first; allow UDP 40000/45000 (`-OpenFirewall`). |
| Broadcast NOT received | Firewall on the Listen machine; or Send ran after the 60 s window. |
| Game instances don't see each other | Same game **version** on both; allow the game's UDP ports on the virtual adapter; host the server, don't just connect. |

---

## 11. Integration gate (unchanged)

AKENO wires a transport into `LanTransportAdapter`
(`apps/server/src/modules/lanplay/transport.ts`) **only after** the transport is verified on real
hardware with a real game. Until then LAN Play reports "Transport not available" and carries no game
traffic. These Windows tools are the reproducible proof that gets us to that gate; they are **not**
part of the production build.
