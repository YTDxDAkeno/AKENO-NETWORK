<#
.SYNOPSIS
    Set up one machine for the AKENO LAN Play SoftEther transport proof (PC <-> PC).

.DESCRIPTION
    Builds a real Layer-2 virtual LAN between two Windows 11 PCs using the maintained,
    open-source SoftEther VPN through its documented vpncmd interface. It implements NO
    packet capture, injection or forwarding of its own; SoftEther carries the traffic.

    Two roles:
      -Role Host : starts the SoftEther VPN Server, creates the 'AKENO' Virtual Hub and a
                   user with a RANDOMLY GENERATED password, then (via the local VPN Client)
                   creates a virtual adapter and connects it to the hub. Prints the exact
                   details you hand to the other machine.
      -Role Join : connects this machine's SoftEther VPN Client to the host's hub using the
                   details the host printed.

    SAFETY: nothing is installed for you; every system-level change (virtual adapter,
    IP address, firewall rule) is shown and skipped unless you approve it (type 'yes',
    or pass -Yes). Routing is never changed and no physical adapter is ever bridged in
    this PC<->PC proof. Credentials are generated, never hardcoded.

.PARAMETER Role
    Host or Join.

.PARAMETER ServerIp
    (Join only) The host machine's real IP address on your normal network, as printed by
    the host's setup run (e.g. 192.168.1.50).

.PARAMETER UserName
    (Join only) The hub username the host printed (default: player1).

.PARAMETER Password
    (Join only) The generated hub password the host printed.

.PARAMETER VlanIp
    This machine's address on the virtual LAN. Defaults: Host 10.9.0.1, Join 10.9.0.2.
    Both machines must share the 10.9.0.0/24 subnet and differ in the last octet.

.PARAMETER OpenFirewall
    If set, OFFER to add a scoped Windows Firewall rule that allows the proof's UDP ports
    (40000, 45000) inbound ON THE VIRTUAL ADAPTER ONLY. Still requires your approval. The
    rule name is recorded so Cleanup removes exactly it.

.PARAMETER Yes
    Pre-approve the system-level changes this script would otherwise prompt for. Use only
    after reading what each step does.

.EXAMPLE
    # On the HOST (elevated PowerShell):
    .\Setup-AkenoLanProof.ps1 -Role Host -OpenFirewall

.EXAMPLE
    # On the JOIN machine (elevated PowerShell), using the details the host printed:
    .\Setup-AkenoLanProof.ps1 -Role Join -ServerIp 192.168.1.50 -UserName player1 -Password <printed> -OpenFirewall
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Host','Join')][string]$Role,
    [string]$ServerIp,
    [string]$UserName = 'player1',
    [string]$Password,
    [string]$VlanIp,
    [switch]$OpenFirewall,
    [switch]$Yes
)

. (Join-Path $PSScriptRoot 'AkenoLanProof.Common.ps1')

$ErrorActionPreference = 'Stop'
$HubName  = 'AKENO'
$NicName  = 'akeno'                       # SoftEther virtual adapter name -> "VPN Client Adapter - akeno"
$NicIf    = 'VPN Client Adapter - akeno'  # the Windows interface alias SoftEther assigns
$AccName  = 'akeno-proof'
$Ports    = @(40000, 45000)

Write-Info "AKENO LAN Play - SoftEther transport proof setup (role: $Role)"
Assert-Admin
$vpncmd = Get-VpnCmdOrStop
Write-Ok "Found vpncmd: $vpncmd"

if (-not $VlanIp) { $VlanIp = if ($Role -eq 'Host') { '10.9.0.1' } else { '10.9.0.2' } }

# -- Warn once about the one unavoidable driver install (the SoftEther virtual adapter) ----
Write-Host ''
Write-WarnMsg "Creating the SoftEther virtual network adapter installs SoftEther's virtual NIC"
Write-WarnMsg "driver. That is the only driver this proof needs. It is reversible with the"
Write-WarnMsg "Cleanup script. You will be asked to approve it before it happens."

# ---------------------------------------------------------------------------- HOST ----------
if ($Role -eq 'Host') {
    Write-Info "Starting the SoftEther VPN Server service (if present)..."
    Start-Service SEVPNSERVER -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    $hubPw  = New-RandomSecret -Length 28
    $userPw = New-RandomSecret -Length 24

    Write-Info "Creating Virtual Hub '$HubName' and user '$UserName' (idempotent)..."
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @(
        '/SERVER','localhost','/PASSWORD:','/CMD','HubCreate',$HubName,"/PASSWORD:$hubPw") | Out-Null
    # SecureNAT off: this is a plain L2 segment for the proof, no NAT/DHCP from the hub.
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @(
        '/SERVER','localhost','/PASSWORD:',"/ADMINHUB:$HubName",'/CMD','SecureNatDisable') | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @(
        '/SERVER','localhost','/PASSWORD:',"/ADMINHUB:$HubName",'/CMD','UserCreate',$UserName,
        '/GROUP:none','/REALNAME:none','/NOTE:none') | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -Args @(
        '/SERVER','localhost','/PASSWORD:',"/ADMINHUB:$HubName",'/CMD','UserPasswordSet',$UserName,
        "/PASSWORD:$userPw") | Out-Null
    Write-Ok "Hub and user ready."

    # The host also runs the VPN Client to get its own virtual adapter on the hub.
    Write-Info "Starting the SoftEther VPN Client service (if present)..."
    Start-Service SEVPNCLIENT -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    if (Confirm-SystemChange -Action "Create SoftEther virtual network adapter '$NicName' on this machine." `
            -Detail "Installs the SoftEther virtual NIC driver. Undo with Cleanup-AkenoLanProof.ps1." -Yes:$Yes) {
        Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @('/CLIENT','localhost','/CMD','NicCreate',$NicName) | Out-Null
        Write-Ok "Virtual adapter '$NicName' created."
    } else {
        Write-ErrMsg "Cannot continue without the virtual adapter."; return
    }

    Write-Info "Creating the client account and connecting it to the local hub..."
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @(
        '/CLIENT','localhost','/CMD','AccountCreate',$AccName,
        '/SERVER:localhost:443',"/HUB:$HubName","/USERNAME:$UserName","/NICNAME:$NicName") | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -Args @(
        '/CLIENT','localhost','/CMD','AccountPasswordSet',$AccName,"/PASSWORD:$userPw",'/TYPE:standard') | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @('/CLIENT','localhost','/CMD','AccountConnect',$AccName) | Out-Null
    Start-Sleep -Seconds 3
    Write-Ok "Connected to hub '$HubName'."

    $firewallRule = $null
    if ($OpenFirewall) { $firewallRule = Add-ProofFirewallRule -NicIf $NicIf -Ports $Ports -Yes:$Yes }
    Set-ProofVlanIp -NicIf $NicIf -VlanIp $VlanIp -Yes:$Yes

    $lanIps = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike '169.254*' -and $_.IPAddress -ne '127.0.0.1' -and
                       $_.InterfaceAlias -ne $NicIf } | Select-Object -ExpandProperty IPAddress)

    Save-ProofState -State @{
        role=$Role; hub=$HubName; nicName=$NicName; nicIf=$NicIf; account=$AccName;
        userName=$UserName; userPassword=$userPw; hubPassword=$hubPw; vlanIp=$VlanIp;
        firewallRule=$firewallRule; ports=$Ports; createdServer=$true
    }

    Write-Host ''
    Write-Host "================ HAND THESE TO THE OTHER (JOIN) MACHINE ================" -ForegroundColor Green
    Write-Host "  Host IP on your normal network : $([string]::Join(' or ', $lanIps))"
    Write-Host "  Hub name                       : $HubName"
    Write-Host "  Username                       : $UserName"
    Write-Host "  Password                       : $userPw"
    Write-Host "  This host's virtual-LAN IP     : $VlanIp"
    Write-Host "  Suggested JOIN virtual-LAN IP  : 10.9.0.2"
    Write-Host "=======================================================================" -ForegroundColor Green
    Write-Host "  On the other PC, run:" -ForegroundColor DarkGray
    Write-Host "    .\Setup-AkenoLanProof.ps1 -Role Join -ServerIp <host IP above> -UserName $UserName -Password $userPw -OpenFirewall" -ForegroundColor DarkGray
    Write-Host ''
    Write-Ok "Host setup done. Next: run Test-Diagnostics.ps1, then Run-TransportTest.ps1 once both machines are connected."
}

# ---------------------------------------------------------------------------- JOIN ----------
else {
    if (-not $ServerIp) { throw "-ServerIp is required for -Role Join (the host's IP it printed)." }
    if (-not $Password) { throw "-Password is required for -Role Join (the host printed it)." }

    Write-Info "Starting the SoftEther VPN Client service (if present)..."
    Start-Service SEVPNCLIENT -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    if (Confirm-SystemChange -Action "Create SoftEther virtual network adapter '$NicName' on this machine." `
            -Detail "Installs the SoftEther virtual NIC driver. Undo with Cleanup-AkenoLanProof.ps1." -Yes:$Yes) {
        Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @('/CLIENT','localhost','/CMD','NicCreate',$NicName) | Out-Null
        Write-Ok "Virtual adapter '$NicName' created."
    } else {
        Write-ErrMsg "Cannot continue without the virtual adapter."; return
    }

    Write-Info "Creating the client account and connecting to host $ServerIp hub '$HubName'..."
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @(
        '/CLIENT','localhost','/CMD','AccountCreate',$AccName,
        "/SERVER:${ServerIp}:443","/HUB:$HubName","/USERNAME:$UserName","/NICNAME:$NicName") | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -Args @(
        '/CLIENT','localhost','/CMD','AccountPasswordSet',$AccName,"/PASSWORD:$Password",'/TYPE:standard') | Out-Null
    Invoke-VpnCmd -VpnCmd $vpncmd -AllowExisting -Args @('/CLIENT','localhost','/CMD','AccountConnect',$AccName) | Out-Null
    Start-Sleep -Seconds 3
    Write-Ok "Connected to hub '$HubName' on $ServerIp."

    $firewallRule = $null
    if ($OpenFirewall) { $firewallRule = Add-ProofFirewallRule -NicIf $NicIf -Ports $Ports -Yes:$Yes }
    Set-ProofVlanIp -NicIf $NicIf -VlanIp $VlanIp -Yes:$Yes

    Save-ProofState -State @{
        role=$Role; hub=$HubName; nicName=$NicName; nicIf=$NicIf; account=$AccName;
        userName=$UserName; userPassword=$Password; serverIp=$ServerIp; vlanIp=$VlanIp;
        firewallRule=$firewallRule; ports=$Ports; createdServer=$false
    }
    Write-Ok "Join setup done. Next: Test-Diagnostics.ps1, then Run-TransportTest.ps1 from whichever machine you choose as the client."
}
