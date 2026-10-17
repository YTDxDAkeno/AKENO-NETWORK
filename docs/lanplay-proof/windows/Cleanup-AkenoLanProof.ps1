<#
.SYNOPSIS
    Undo everything the AKENO LAN Play SoftEther transport proof created on this machine.

.DESCRIPTION
    Reverses, in order and only what was recorded in the proof state file:
      * disconnects and deletes the SoftEther account,
      * removes the virtual-LAN IP from the virtual adapter,
      * deletes the SoftEther virtual network adapter (removes the virtual NIC),
      * removes the proof firewall rule (if one was added),
      * on a Host, deletes the 'AKENO' Virtual Hub and its user,
      * deletes the saved proof state file.

    It does NOT uninstall SoftEther, change routing, or touch any physical adapter. Each
    destructive step is shown and skipped unless you approve it (type 'yes', or pass -Yes).

.PARAMETER Yes
    Pre-approve the removals. Use only after reading what each step does.

.PARAMETER KeepServer
    (Host) leave the Virtual Hub and user in place (only undo the client-side adapter/account).

.EXAMPLE
    .\Cleanup-AkenoLanProof.ps1
#>
[CmdletBinding()]
param([switch]$Yes, [switch]$KeepServer)

. (Join-Path $PSScriptRoot 'AkenoLanProof.Common.ps1')
$ErrorActionPreference = 'Continue'

Write-Info "AKENO LAN Play - cleanup"
Assert-Admin
$vpncmd = Find-VpnCmd
$state  = Get-ProofState

$nicName = if ($state -and $state.nicName) { $state.nicName } else { 'akeno' }
$nicIf   = if ($state -and $state.nicIf)   { $state.nicIf }   else { 'VPN Client Adapter - akeno' }
$acc     = if ($state -and $state.account) { $state.account } else { 'akeno-proof' }
$hub     = if ($state -and $state.hub)     { $state.hub }     else { 'AKENO' }
$userName = if ($state -and $state.userName) { $state.userName } else { 'player1' }

if (-not $state) { Write-WarnMsg "No recorded proof state; cleaning up default names where present." }

# 1) Disconnect + delete the SoftEther account
if ($vpncmd) {
    if (Confirm-SystemChange -Action "Disconnect and delete SoftEther account '$acc'." -Yes:$Yes) {
        & $vpncmd /CLIENT localhost /CMD AccountDisconnect $acc 2>&1 | Out-Null
        & $vpncmd /CLIENT localhost /CMD AccountDelete $acc 2>&1 | Out-Null
        Write-Ok "Account '$acc' removed."
    }
}

# 2) Remove the virtual-LAN IP from the adapter
if (Get-NetAdapter -InterfaceAlias $nicIf -ErrorAction SilentlyContinue) {
    if (Confirm-SystemChange -Action "Remove the 10.9.0.x address from virtual adapter '$nicIf'." -Yes:$Yes) {
        Get-NetIPAddress -InterfaceAlias $nicIf -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -like '10.9.0.*' } |
            Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue
        Write-Ok "Virtual-LAN IP removed."
    }
}

# 3) Delete the virtual network adapter (removes the virtual NIC)
if ($vpncmd) {
    if (Confirm-SystemChange -Action "Delete SoftEther virtual network adapter '$nicName'." `
            -Detail "Removes the virtual NIC this proof added." -Yes:$Yes) {
        & $vpncmd /CLIENT localhost /CMD NicDelete $nicName 2>&1 | Out-Null
        Write-Ok "Virtual adapter '$nicName' deleted."
    }
}

# 4) Remove the proof firewall rule
if ($state -and $state.firewallRule) {
    $fw = Get-NetFirewallRule -DisplayName $state.firewallRule -ErrorAction SilentlyContinue
    if ($fw) {
        if (Confirm-SystemChange -Action "Remove firewall rule '$($state.firewallRule)'." -Yes:$Yes) {
            Remove-NetFirewallRule -DisplayName $state.firewallRule -ErrorAction SilentlyContinue
            Write-Ok "Firewall rule removed."
        }
    }
}

# 5) Host only: delete the hub and user
if ($vpncmd -and $state -and $state.createdServer -and -not $KeepServer) {
    if (Confirm-SystemChange -Action "Delete Virtual Hub '$hub' and user '$userName' from the local SoftEther server." -Yes:$Yes) {
        & $vpncmd /SERVER localhost /PASSWORD: /ADMINHUB:$hub /CMD UserDelete $userName 2>&1 | Out-Null
        & $vpncmd /SERVER localhost /PASSWORD: /CMD HubDelete $hub 2>&1 | Out-Null
        Write-Ok "Hub '$hub' and user '$userName' removed."
    }
}

# 6) Remove the saved state file
$sp = Get-ProofStatePath
if (Test-Path $sp) {
    Remove-Item $sp -Force -ErrorAction SilentlyContinue
    Write-Ok "Removed proof state file."
}

Write-Host ''
Write-Ok "Cleanup finished. SoftEther itself was left installed; uninstall it from Settings > Apps if you no longer want it."
Write-Host "Nothing in your routing table or on any physical network adapter was changed by this proof." -ForegroundColor DarkGray
