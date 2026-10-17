# AkenoLanProof.Common.ps1
#
# Shared helpers for the AKENO LAN Play SoftEther transport proof on Windows 11.
# Dot-sourced by the Setup/Run/Diagnostics/Cleanup scripts. No side effects on load.
#
# Safety rules these helpers enforce (see docs/LAN_PLAY_WINDOWS_PROOF.md):
#   * Nothing is installed automatically. If SoftEther is missing we stop and print
#     where to download it.
#   * Every system-level change (creating a virtual network adapter, assigning an IP,
#     adding a firewall rule) goes through Confirm-SystemChange and is skipped unless
#     the operator explicitly approves (interactively, or with -Yes).
#   * We never alter routing and never bridge a physical adapter in the PC<->PC proof.
#   * Credentials are generated randomly, never hardcoded.

$ErrorActionPreference = 'Stop'

# Where we remember what this proof created, so Cleanup can undo exactly that and no more.
$script:StateDir  = Join-Path $env:USERPROFILE '.akeno-lan-proof'
$script:StatePath = Join-Path $script:StateDir 'state.json'

function Write-Info    { param([string]$m) Write-Host "[akeno] $m" -ForegroundColor Cyan }
function Write-Ok      { param([string]$m) Write-Host "[ ok ] $m" -ForegroundColor Green }
function Write-WarnMsg  { param([string]$m) Write-Host "[warn] $m" -ForegroundColor Yellow }
function Write-ErrMsg   { param([string]$m) Write-Host "[fail] $m" -ForegroundColor Red }

# True only when running elevated. SoftEther NIC creation and netsh/New-NetIPAddress need it.
function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Admin {
    if (-not (Test-IsAdmin)) {
        Write-ErrMsg "This step needs an elevated PowerShell. Right-click PowerShell and 'Run as administrator', then re-run."
        throw "Administrator rights required."
    }
}

# Gate for ANY system-level change. Returns $true only on explicit approval.
# -Yes on the calling script approves non-interactively (for people who have already read
# what each step does); without it, the operator must type 'yes'.
function Confirm-SystemChange {
    param(
        [Parameter(Mandatory)][string]$Action,   # one line: what will change
        [string]$Detail = '',                     # optional extra context
        [switch]$Yes                              # pre-approval from the calling script
    )
    Write-Host ''
    Write-Host "SYSTEM CHANGE: $Action" -ForegroundColor Magenta
    if ($Detail) { Write-Host "              $Detail" -ForegroundColor DarkGray }
    if ($Yes) { Write-WarnMsg "Approved via -Yes."; return $true }
    $ans = Read-Host "Proceed with this change? Type 'yes' to allow, anything else to skip"
    if ($ans -eq 'yes') { return $true }
    Write-WarnMsg "Skipped (not approved)."
    return $false
}

# Locate vpncmd.exe without assuming an install path. Checks PATH then the usual
# SoftEther install folders. Returns $null if not found (caller prints download help).
function Find-VpnCmd {
    $fromPath = Get-Command vpncmd.exe -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }
    $candidates = @(
        "$env:ProgramFiles\SoftEther VPN Client\vpncmd.exe",
        "$env:ProgramFiles\SoftEther VPN Server\vpncmd.exe",
        "${env:ProgramFiles(x86)}\SoftEther VPN Client\vpncmd.exe",
        "${env:ProgramFiles(x86)}\SoftEther VPN Server\vpncmd.exe"
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    return $null
}

function Get-VpnCmdOrStop {
    $p = Find-VpnCmd
    if (-not $p) {
        Write-ErrMsg "SoftEther VPN was not found."
        Write-Host  "  Download it yourself from the official site (nothing is installed for you):" -ForegroundColor DarkGray
        Write-Host  "    https://www.softether-download.com/en.aspx" -ForegroundColor DarkGray
        Write-Host  "  Install 'SoftEther VPN Server' AND 'SoftEther VPN Client' on the HOST machine," -ForegroundColor DarkGray
        Write-Host  "  and 'SoftEther VPN Client' on the JOIN machine, then re-run this script." -ForegroundColor DarkGray
        throw "vpncmd.exe not found."
    }
    return $p
}

# Run a vpncmd command. SoftEther returns a non-zero exit code when an object already
# exists; -AllowExisting treats that as success so re-runs are idempotent.
function Invoke-VpnCmd {
    param(
        [Parameter(Mandatory)][string]$VpnCmd,
        [Parameter(Mandatory)][string[]]$Args,
        [switch]$AllowExisting
    )
    $out = & $VpnCmd @Args 2>&1
    $code = $LASTEXITCODE
    $text = ($out | Out-String)
    if ($code -ne 0) {
        $existing = $text -match 'already exists'
        if ($AllowExisting -and $existing) { return $text }
        Write-ErrMsg "vpncmd failed (exit $code): $($Args -join ' ')"
        Write-Host $text -ForegroundColor DarkGray
        throw "vpncmd error."
    }
    return $text
}

# A random alphanumeric secret (default 24 chars) from a CSPRNG. Never hardcoded.
function New-RandomSecret {
    param([int]$Length = 24)
    $alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'
    $bytes = New-Object 'System.Byte[]' $Length
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    -join ($bytes | ForEach-Object { $alphabet[ $_ % $alphabet.Length ] })
}

function Save-ProofState {
    param([Parameter(Mandatory)][hashtable]$State)
    if (-not (Test-Path $script:StateDir)) {
        New-Item -ItemType Directory -Path $script:StateDir -Force | Out-Null
    }
    $State | ConvertTo-Json -Depth 5 | Set-Content -Path $script:StatePath -Encoding UTF8
    Write-Ok "Saved proof state to $script:StatePath"
}

function Get-ProofState {
    if (-not (Test-Path $script:StatePath)) { return $null }
    try { return (Get-Content -Path $script:StatePath -Raw | ConvertFrom-Json) }
    catch { return $null }
}

function Get-ProofStatePath { return $script:StatePath }

# Find a Python launcher (python / python3 / py). The proof helpers need Python 3.
function Find-Python {
    foreach ($name in @('python', 'python3')) {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c) {
            try {
                $v = & $c.Source --version 2>&1
                if ($v -match 'Python 3') { return @{ Exe = $c.Source; Args = @() } }
            } catch { }
        }
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { return @{ Exe = $py.Source; Args = @('-3') } }
    return $null
}

# Assign the virtual-LAN static IP to the SoftEther adapter. System-level change -> gated.
# Uses New-NetIPAddress on the virtual adapter only; it never touches routing or any
# physical adapter.
function Set-ProofVlanIp {
    param(
        [Parameter(Mandatory)][string]$NicIf,
        [Parameter(Mandatory)][string]$VlanIp,
        [int]$PrefixLength = 24,
        [switch]$Yes
    )
    if (-not (Confirm-SystemChange -Action "Assign $VlanIp/$PrefixLength to the virtual adapter '$NicIf'." `
            -Detail "Only this virtual adapter is changed. Routing and physical adapters are untouched." -Yes:$Yes)) {
        Write-WarnMsg "Virtual-LAN IP not assigned. You can set it later in Network Connections or re-run with -Yes."
        return
    }
    # Wait for the adapter to appear (SoftEther registers it a moment after NicCreate).
    $deadline = (Get-Date).AddSeconds(20)
    while (-not (Get-NetAdapter -InterfaceAlias $NicIf -ErrorAction SilentlyContinue)) {
        if ((Get-Date) -gt $deadline) { Write-ErrMsg "Virtual adapter '$NicIf' did not appear. Check SoftEther."; return }
        Start-Sleep -Milliseconds 500
    }
    # Remove any stale proof address on this adapter, then set the requested one.
    Get-NetIPAddress -InterfaceAlias $NicIf -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like '10.9.0.*' } |
        Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue
    New-NetIPAddress -InterfaceAlias $NicIf -IPAddress $VlanIp -PrefixLength $PrefixLength -ErrorAction Stop | Out-Null
    Write-Ok "Assigned $VlanIp/$PrefixLength to '$NicIf'."
}

# Add a scoped inbound-allow firewall rule for the proof ports ON THE VIRTUAL ADAPTER ONLY.
# System-level change -> gated. Returns the rule name (recorded so Cleanup removes exactly it).
function Add-ProofFirewallRule {
    param(
        [Parameter(Mandatory)][string]$NicIf,
        [Parameter(Mandatory)][int[]]$Ports,
        [switch]$Yes
    )
    $ruleName = "AKENO LAN Proof (UDP $($Ports -join ','))"
    if (-not (Confirm-SystemChange -Action "Add a Windows Firewall rule '$ruleName'." `
            -Detail "Allows inbound UDP $($Ports -join ',') on the virtual adapter '$NicIf' only. Removed by Cleanup." -Yes:$Yes)) {
        Write-WarnMsg "Firewall rule not added. If a test shows loss or no discovery, Windows Firewall is the usual cause."
        return $null
    }
    Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue
    # Scope the rule to the virtual adapter via its InterfaceAlias so the physical LAN is unaffected.
    New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol UDP `
        -LocalPort $Ports -InterfaceAlias $NicIf -Profile Any -ErrorAction Stop | Out-Null
    Write-Ok "Added firewall rule '$ruleName' (virtual adapter only)."
    return $ruleName
}
