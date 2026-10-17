# AKENO LAN Play — Windows 11 transport-proof tools

Ready-to-run PowerShell tools that build a real Layer-2 virtual LAN between two Windows 11 PCs with
the open-source **SoftEther VPN** (via its documented `vpncmd`) and measure genuine traffic across
it. **No packet-capture/injection/forwarding engine is implemented here** — SoftEther carries the
traffic; these scripts orchestrate it and measure the result.

**Full beginner walkthrough:** [`docs/LAN_PLAY_WINDOWS_PROOF.md`](../../LAN_PLAY_WINDOWS_PROOF.md)
(install, configure, host/IPs, firewall, launch, success, undo; the PC game test; the PS5 bridge
research).

| Script | Purpose |
| --- | --- |
| `Setup-AkenoLanProof.ps1` | Set up a machine (`-Role Host` or `-Role Join`): hub, user (random password), virtual adapter, connect. |
| `Run-TransportTest.ps1` | Run the UDP + broadcast tests (`-Mode Listen` / `-Mode Send`) and save results. |
| `Test-Diagnostics.ps1` | Read-only health check. |
| `Cleanup-AkenoLanProof.ps1` | Undo everything the setup created. |
| `AkenoLanProof.Common.ps1` | Shared helpers (dot-sourced; not run directly). |

Quick start (elevated PowerShell on each PC):

```powershell
# Host
.\Setup-AkenoLanProof.ps1 -Role Host -OpenFirewall
# Join (use the details the Host printed)
.\Setup-AkenoLanProof.ps1 -Role Join -ServerIp <HOST-IP> -UserName player1 -Password <PRINTED> -OpenFirewall
# Test: Listen on 10.9.0.2 first, then Send on 10.9.0.1
.\Run-TransportTest.ps1 -Mode Listen
.\Run-TransportTest.ps1 -Mode Send -PeerIp 10.9.0.2
# Check / undo
.\Test-Diagnostics.ps1 -PeerIp 10.9.0.2
.\Cleanup-AkenoLanProof.ps1
```

**Safety:** nothing is installed for you; every system-level change (virtual adapter, IP, firewall
rule) is shown as `SYSTEM CHANGE:` and requires you to type `yes` (or pass `-Yes`). Routing is never
changed and no physical adapter is bridged in this PC↔PC proof. Credentials are generated, never
hardcoded. Requires Administrator (SoftEther adapter + IP need it) and Python 3 on the machine that
runs the transport test.

This is a **transport** proof, not a game test, and it is **not** wired into production AKENO. The
author verified the underlying harness on Linux; the Windows/game/PS5 runs are performed by you.
