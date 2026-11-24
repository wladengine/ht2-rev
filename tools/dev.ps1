[CmdletBinding()]
param(
    [ValidateSet('doctor','build','check','test','prepare','run')][string]$Action = 'doctor',
    [string]$GameExe, [string]$GameDir, [string]$VcDir, [string]$SdkRoot,
    [string]$SdkVersion = '10.0.26100.0', [string]$Python = 'python',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$toolchain = @{ VcDir=$VcDir; SdkRoot=$SdkRoot; SdkVersion=$SdkVersion; Python=$Python }
if ($Action -eq 'doctor') {
    . "$PSScriptRoot\windows-toolchain.ps1"
    Resolve-WindowsToolchain @toolchain | Select-Object VcDir, CompilerVersion, SdkRoot, SdkVersion, Python, PythonVersion | Format-List
    return
}
if ($Action -eq 'test') {
    if (!$GameExe) { throw 'Supply -GameExe with the path to the Steam 8.1 king.exe.' }
    if ($VcDir) { $env:VCToolsInstallDir = $VcDir }
    if ($SdkRoot) { $env:WindowsSdkDir = $SdkRoot }
    $env:HT2_SDK_VERSION = $SdkVersion
    $env:HT2_PYTHON = $Python
    & "$PSScriptRoot/../tests/run.ps1" -GameExe $GameExe -Python $Python
    exit $LASTEXITCODE
}
if ($Action -in 'build','check') {
    if (!$GameExe) { throw 'Supply -GameExe with the path to your original king.exe.' }
    & "$PSScriptRoot\build-windows.ps1" @toolchain -GameExe $GameExe -Clean:$Clean -Check:($Action -eq 'check')
    return
}
if (!$GameDir) { throw 'Supply -GameDir with the game installation directory.' }
$localDir = Join-Path $PSScriptRoot '..\.local'
$config = 'build'
$exe = Join-Path $localDir "$config\king.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build the $config configuration first." }
$run = Join-Path $localDir ('runs\' + (Get-Date -Format yyyyMMdd-HHmmss) + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
& "$PSScriptRoot\prepare-run.ps1" -GameDir $GameDir -Destination $run -RebuiltExe $exe
if ($Action -eq 'prepare') { return }
$run = (Resolve-Path -LiteralPath $run).Path
$start = Get-Date -Format o
$game = Start-Process -FilePath "$run\king.exe" -WorkingDirectory $run -PassThru -Wait
[ordered]@{ started=$start; finished=(Get-Date -Format o); exitCode=$game.ExitCode } |
    ConvertTo-Json | Set-Content -LiteralPath "$run\exit.json"
if ($game.ExitCode) { throw "Game exit $($game.ExitCode). See $run" }
