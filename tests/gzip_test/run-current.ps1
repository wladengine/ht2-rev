# Self round-trip check only. This is NOT an independent original-game oracle.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [ValidateRange(1,100000)][int]$Cases=300,
    [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
$out="$repo\.local\regressions\gzip-$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $out | Out-Null
& "$repo\tools\build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out\build.log"
Push-Location "$repo/.local/build"
try {
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    if ($LASTEXITCODE) { throw 'Failed to generate link command' }
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($link.Count -ne 1) { throw 'Expected exactly one game link command' }
    & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /arch:IA32 /D_CRT_SECURE_NO_WARNINGS "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\gzip_test.obj" "$PSScriptRoot\gzip_test.cpp" 2>&1 | Tee-Object "$out\compile.log"
    if ($LASTEXITCODE) { throw "Harness compile failed: $LASTEXITCODE" }
    $link=$link[0].Trim().Replace('king.res', "`"$out\gzip_test.obj`"").Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/ENTRY:WinMainCRTStartup','/ENTRY:mainCRTStartup').Replace('/OUT:king.exe', "/OUT:`"$out\gzip.exe`"").Replace('/MAP:king.map', "/MAP:`"$out\gzip.map`"")
    # Relocatable test executable; production game flags remain unchanged.
    $link += ' /BASE:0x20000000 /FIXED:NO /DYNAMICBASE:NO'
    $link | Set-Content "$out\link-command.txt"
    # Parse NMAKE's quoted tokens and invoke link directly, without a second shell.
    $tokens=@([regex]::Matches($link, '(?:[^\s"]|"[^"]*")+') | ForEach-Object { $_.Value.Replace('"','') })
    $linkArgs=$tokens[1..($tokens.Count-1)]
    & $tokens[0] @linkArgs 2>&1 | Tee-Object "$out\link.log"
    if ($LASTEXITCODE) { throw "Harness link failed: $LASTEXITCODE" }
    & "$out\gzip.exe" $Cases "$out\packed.bin" 2>&1 | Tee-Object "$out\run.log"
    if ($LASTEXITCODE) { throw "Round-trip failed: $LASTEXITCODE (see $out)" }
    if (!(Test-Path "$out\packed.bin") -or (Get-Item "$out\packed.bin").Length -eq 0) { throw 'Harness produced no packed output' }
    [ordered]@{ status='success'; kind='self round-trip, not original equivalence'; cases=$Cases; gameBuild=(Get-Content "$out\build.log.json" -Raw | ConvertFrom-Json); harnessSha256=(Get-FileHash "$out\gzip.exe").Hash; packedSha256=(Get-FileHash "$out\packed.bin").Hash } | ConvertTo-Json -Depth 8 | Set-Content "$out\result.json" -Encoding UTF8
    "PASS self round-trip; evidence: $out"
} finally { Pop-Location }
