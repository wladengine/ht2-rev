[CmdletBinding()]
param([Parameter(Mandatory)][string]$GameExe, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$GameExe = (Resolve-Path -LiteralPath $GameExe).Path
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
. "$repo/tools/windows-toolchain.ps1"
$tc = Resolve-WindowsToolchain -Python $Python
$out = (New-Item -ItemType Directory -Path "$repo/.local/regressions/state-$([guid]::NewGuid().ToString('N'))").FullName
& "$repo/tools/build-windows.ps1" -GameExe $GameExe -Python $Python -LogPath "$out/build.log" | Out-Null
Push-Location "$repo/.local/build"
try {
    $flags = @('/nologo','/f','..\..\tests\state\native.mk') + $tc.MakeArgs
    & $tc.NMake @flags route-contract
    if ($LASTEXITCODE) { throw 'Route ABI contract failed.' }
    foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cpp') {
        $dir = (New-Item -ItemType Directory -Path "$out/$($file.BaseName)").FullName
        & $tc.NMake @flags "CHECK_DIR=$dir" "CHECK_SOURCE=$($file.FullName)" native-check
        if ($LASTEXITCODE) { throw "Native state regression failed for $($file.Name)." }
    }
    foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.json') {
        & $tc.Python "$PSScriptRoot/compare-x86.py" $GameExe king.exe king.map $file.FullName > "$out/$($file.Name)"
        if ($LASTEXITCODE) { throw "Paired state regression failed for $($file.Name)." }
    }
} finally { Pop-Location }
"PASS state regressions. Results in $out"
