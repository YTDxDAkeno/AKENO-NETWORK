<#
.SYNOPSIS
    Run the AKENO LAN Play transport test over the SoftEther virtual LAN and collect results.

.DESCRIPTION
    Drives the parameterized Python helpers in docs/lanplay-proof/ to measure, over the
    virtual LAN built by Setup-AkenoLanProof.ps1:
      * bidirectional UDP delivery (datagrams echoed, loss, latency, jitter), and
      * UDP broadcast discovery (what LAN games use to find each other).

    This is a TRANSPORT test, not a game test. A pass proves the overlay carries UDP and
    broadcast; it does not prove any game works (that is the separate game test in the doc).

    Run the ECHO/LISTENER side first on one machine (-Mode Listen), then the CLIENT side on
    the other (-Mode Send). Results are written to a timestamped folder and printed.

.PARAMETER Mode
    Listen = start the UDP echo server and the broadcast listener on this machine (do this
             on the machine whose virtual-LAN IP is -PeerIp as seen from the other side).
    Send   = send UDP to the peer's echo server and send one broadcast beacon.

.PARAMETER PeerIp
    (Send) the other machine's virtual-LAN IP (e.g. 10.9.0.2 if you are 10.9.0.1).

.PARAMETER Broadcast
    (Send) the virtual LAN's broadcast address. Default 10.9.0.255 for 10.9.0.0/24.

.PARAMETER Count
    (Send) number of UDP datagrams (default 500).

.PARAMETER OutDir
    Where to write results. Default: .\results\<timestamp> next to this script.

.EXAMPLE
    # On the machine at 10.9.0.2 (the echo/listen side):
    .\Run-TransportTest.ps1 -Mode Listen

    # On the machine at 10.9.0.1 (the client/send side):
    .\Run-TransportTest.ps1 -Mode Send -PeerIp 10.9.0.2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Listen','Send')][string]$Mode,
    [string]$PeerIp,
    [string]$Broadcast = '10.9.0.255',
    [int]$Count = 500,
    [string]$OutDir
)

. (Join-Path $PSScriptRoot 'AkenoLanProof.Common.ps1')
$ErrorActionPreference = 'Stop'

$helpers = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path   # docs/lanplay-proof/
$py = Find-Python
if (-not $py) {
    Write-ErrMsg "Python 3 was not found. Install it from https://www.python.org/downloads/ (tick 'Add python.exe to PATH'), then re-run."
    throw "Python 3 required."
}
$pyExe = $py.Exe; $pyPre = $py.Args

function Invoke-Helper {
    param([string]$Script, [string[]]$HelperArgs)
    $full = @($pyPre + @((Join-Path $helpers $Script)) + $HelperArgs)
    & $pyExe @full
}

if ($Mode -eq 'Listen') {
    Write-Info "Transport test (Listen side). Start this BEFORE the Send side."
    $udpPort = 40000; $bcPort = 45000
    Write-Info "Starting UDP echo server on 0.0.0.0:$udpPort (background)..."
    $echo = Start-Process -FilePath $pyExe -PassThru -WindowStyle Hidden `
        -ArgumentList ($pyPre + @((Join-Path $helpers 'udp_echo.py'), '--bind','0.0.0.0','--port',"$udpPort"))
    Start-Sleep -Seconds 1
    Write-Ok "Echo server running (PID $($echo.Id))."
    Write-Info "Now run the Send side on the other machine. Waiting for one broadcast beacon (60s)..."
    # Broadcast listener blocks until a beacon arrives or it times out; use a generous window
    # so the operator has time to start the Send side.
    Invoke-Helper -Script 'bcast_listener.py' -HelperArgs @('--port',"$bcPort",'--timeout','60')
    $bcExit = $LASTEXITCODE
    Write-Info "Stopping echo server..."
    Stop-Process -Id $echo.Id -ErrorAction SilentlyContinue
    if ($bcExit -eq 0) { Write-Ok "Broadcast discovery: RECEIVED." } else { Write-WarnMsg "Broadcast discovery: NOT received (check firewall / that Send ran in time)." }
    Write-Ok "Listen side done. The measured numbers are printed on the Send side."
    return
}

# -------- Send side: measure and record --------
if (-not $PeerIp) { throw "-PeerIp is required for -Mode Send (the other machine's virtual-LAN IP)." }
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot ("results\" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
Write-Info "Transport test (Send side) -> peer $PeerIp. Results: $OutDir"

$state = Get-ProofState
$selfIp = if ($state -and $state.vlanIp) { $state.vlanIp } else { '0.0.0.0' }

# 1) Reachability
Write-Info "Ping $PeerIp (4 packets)..."
$ping = Test-Connection -ComputerName $PeerIp -Count 4 -ErrorAction SilentlyContinue
$pingOk = [bool]$ping
$ping | Out-String | Tee-Object -FilePath (Join-Path $OutDir 'ping.txt') | Out-Null

# 2) Bidirectional UDP latency/loss (peer must be running -Mode Listen)
Write-Info "Bidirectional UDP: $Count datagrams to $PeerIp:40000 (echo expected)..."
$udpLine = Invoke-Helper -Script 'udp_client.py' -HelperArgs @('--dst',$PeerIp,'--port','40000','--bind',$selfIp,'--count',"$Count")
$udpLine | Tee-Object -FilePath (Join-Path $OutDir 'udp.txt')

# 3) Broadcast discovery beacon (peer must be running the listener from -Mode Listen)
Write-Info "Sending one broadcast beacon to $Broadcast:45000..."
$bcOut = Invoke-Helper -Script 'bcast_send.py' -HelperArgs @('--bind',$selfIp,'--bcast',$Broadcast,'--port','45000','--message',"AKENO-LAN-DISCOVERY from=$selfIp")
$bcOut | Tee-Object -FilePath (Join-Path $OutDir 'broadcast.txt')

# Parse the UDP summary line for the report.
$m = [regex]::Match([string]$udpLine, 'sent=(\d+) echoed=(\d+) loss=([\d.]+)% rtt_avg=([\d.]+)ms p50=([\d.]+)ms p95=([\d.]+)ms jitter=([\d.]+)ms')
$summary = [ordered]@{
    timestamp   = (Get-Date).ToString('o')
    selfVlanIp  = $selfIp
    peerVlanIp  = $PeerIp
    pingReachable = $pingOk
    udp = if ($m.Success) { [ordered]@{
        sent=[int]$m.Groups[1].Value; echoed=[int]$m.Groups[2].Value; lossPct=[double]$m.Groups[3].Value
        rttAvgMs=[double]$m.Groups[4].Value; p50Ms=[double]$m.Groups[5].Value
        p95Ms=[double]$m.Groups[6].Value; jitterMs=[double]$m.Groups[7].Value } } else { $null }
    broadcastSent = ([string]$bcOut -match 'beacon sent')
    note = "Transport test only. Not a game test. Broadcast RECEIVED/NOT is reported on the Listen machine."
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $OutDir 'summary.json') -Encoding UTF8

Write-Host ''
Write-Host "==================== TRANSPORT TEST RESULTS ====================" -ForegroundColor Green
Write-Host ("  Ping reachable : {0}" -f $pingOk)
if ($m.Success) {
    Write-Host ("  UDP            : {0}/{1} echoed, loss {2}%, rtt avg {3} ms, p95 {4} ms, jitter {5} ms" -f `
        $m.Groups[2].Value,$m.Groups[1].Value,$m.Groups[3].Value,$m.Groups[4].Value,$m.Groups[6].Value,$m.Groups[7].Value)
} else { Write-WarnMsg "  UDP            : no summary parsed (did the peer run -Mode Listen first?)" }
Write-Host  "  Broadcast      : sent; see the Listen machine for RECEIVED/NOT"
Write-Host "  Saved          : $OutDir"
Write-Host "===============================================================" -ForegroundColor Green
Write-Host "  Interpreting success:" -ForegroundColor DarkGray
Write-Host "    * loss 0% on a wired LAN; a few % and higher latency is normal over the Internet." -ForegroundColor DarkGray
Write-Host "    * broadcast RECEIVED on the Listen machine = LAN-game discovery can cross the overlay." -ForegroundColor DarkGray
Write-Host "    * This proves TRANSPORT only. Game multiplayer is proven separately (see the doc)." -ForegroundColor DarkGray
