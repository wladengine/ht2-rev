# Use the same native x86 host and mapped Steam PE as the grid checks.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [string]$Python='python',
    [ValidateSet('none','speed','dust','tyre','contact-axes','licence-recipient','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes')][string]$NegativeControl='none'
)
$ErrorActionPreference='Stop'
& "$PSScriptRoot/../grid_abi_test/run.ps1" -GameExe $GameExe -Python $Python -Suite solo -NegativeControl $NegativeControl
