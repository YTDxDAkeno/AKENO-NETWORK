<#
.SYNOPSIS
    Diagnose the AKENO LAN Play SoftEther virtual-LAN connection on this machine (read-only).

.DESCRIPTION
    Checks, and prints a clear PASS/WARN for each: SoftEther present, the client service
    running, the virtual adapter present with its virtual-LAN IP, the SoftEther account
    connected to the hub, the proof firewall rule (if any), and optionally reachability to
    the peer. Changes nothing on the system.

.PARAMETER PeerIp
    Optional: the other machine's virtual-LAN IP, to test reachability (ping) as well.

.EXAMPLE
    .\Test-Diagnostics.ps1 -PeerIp 10.9.0.2
#>
[CmdletBinding()]
param([string]$PeerIp)

. (Join-Path $PSScriptRoot 'AkenoLanProof.Common.ps1')
$ErrorActionPreference = 'Continue'

$pass = 0; $warn = 0
function Check { param([string]$Name,[bool]$Ok,[string]$Detail='')
    if ($Ok) { Write-Host ("[PASS] {0}" -f $Name) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("[WARN] {0}" -f $Name) -ForegroundColor Yellow; $script:warn++ }
    if ($Detail) { Write-Host ("       {0}" -f $Detail) -ForegroundColor DarkGray }
}

Write-Info "AKENO LAN Play - connection diagnostics (read-only)"
$state = Get-ProofState
if ($state) { Write-Info "Using recorded proof state ($(Get-ProofStatePath))." }
else { Write-WarnMsg "No recorded proof state; checking with defaults. Run Setup first if nothing passes." }

$nicIf = if ($state -and $state.nicIf) { $state.nicIf } else { 'VPN Client Adapter - akeno' }
$hub   = if ($state -and $state.hub)   { $state.hub }   else { 'AKENO' }
$acc   = if ($state -and $state.account) { $state.account } else { 'akeno-proof' }

# 1) SoftEther present
$vpncmd = Find-VpnCmd
Check "SoftEther vpncmd found" ([bool]$vpncmd) $vpncmd

# 2) Client service running
$svc = Get-Service SEVPNCLIENT -ErrorAction SilentlyContinue
Check "SoftEther VPN Client service running" ([bool]($svc -and $svc.Status -eq 'Running')) `
    ("status: " + ($(if ($svc) { $svc.Status } else { 'not installed' })))

# 3) Virtual adapter present + up
$nic = Get-NetAdapter -InterfaceAlias $nicIf -ErrorAction SilentlyContinue
Check "Virtual adapter '$nicIf' present" ([bool]$nic) ($(if ($nic) { "status: $($nic.Status)" } else { "not found" }))

# 4) Virtual-LAN IP assigned
$vip = Get-NetIPAddress -InterfaceAlias $nicIf -AddressFamily IPv4 -ErrorAction SilentlyContinue |
       Where-Object { $_.IPAddress -like '10.9.0.*' } | Select-Object -First 1
Check "Virtual-LAN IP assigned (10.9.0.x)" ([bool]$vip) ($(if ($vip) { "$($vip.IPAddress)/$($vip.PrefixLength)" } else { "none on this adapter" }))

# 5) SoftEther account connected
if ($vpncmd) {
    $acctList = & $vpncmd /CLIENT localhost /CMD AccountList 2>&1 | Out-String
    $connected = $acctList -match [regex]::Escape($acc) -and ($acctList -match 'Connected')
    Check "SoftEther account '$acc' connected to hub '$hub'" $connected
    if (-not $connected) { Write-Host ($acctList.Trim()) -ForegroundColor DarkGray }
} else { Check "SoftEther account connected" $false "vpncmd not available" }

# 6) Firewall rule (informational)
if ($state -and $state.firewallRule) {
    $fw = Get-NetFirewallRule -DisplayName $state.firewallRule -ErrorAction SilentlyContinue
    Check "Proof firewall rule present ('$($state.firewallRule)')" ([bool]$fw)
} else {
    Write-Host "[info] No proof firewall rule recorded. If UDP/broadcast fails, Windows Firewall is the usual cause;" -ForegroundColor DarkGray
    Write-Host "       re-run Setup with -OpenFirewall, or allow UDP 40000/45000 on the virtual adapter." -ForegroundColor DarkGray
}

# 7) Optional reachability
if ($PeerIp) {
    $ok = [bool](Test-Connection -ComputerName $PeerIp -Count 3 -Quiet -ErrorAction SilentlyContinue)
    Check "Ping peer $PeerIp over the virtual LAN" $ok `
        ($(if (-not $ok) { "No reply. Check the peer is connected and its firewall allows ICMP/UDP on the virtual adapter." } else { "" }))
}

Write-Host ''
Write-Host ("Summary: $pass passed, $warn need attention.") -ForegroundColor $(if ($warn -eq 0) { 'Green' } else { 'Yellow' })
if ($warn -gt 0) { Write-Host "Fix the WARN items above, then run Run-TransportTest.ps1." -ForegroundColor DarkGray }
