# Terrain thiscall regression against production and independently supplied original.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python',
    [ValidateSet('none','caller','wrapper','plant','height','arch')][string]$NegativeControl='none'
)
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $GameExe).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD') { throw 'Original fixture addresses require the verified Steam EXE hash' }
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
$out="$repo\.local\regressions\terrain-abi-$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $out | Out-Null
& "$repo\tools\build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out\build.log"
Push-Location "$repo/.local/build"
try {
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    if ($LASTEXITCODE) { throw 'Failed to generate link command' }
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($link.Count -ne 1) { throw 'Expected exactly one game link command' }
    & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /Oy- /arch:IA32 /D_CRT_SECURE_NO_WARNINGS /EHsc "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\terrain_abi_test.obj" "$PSScriptRoot\terrain_abi_test.cpp" 2>&1 | Tee-Object "$out\compile.log"
    if ($LASTEXITCODE) { throw "Harness compile failed: $LASTEXITCODE" }
    $link=$link[0].Trim().Replace('king.res', "`"$out\terrain_abi_test.obj`"").Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/ENTRY:WinMainCRTStartup','/ENTRY:TerrainEntry').Replace('/OUT:king.exe', "/OUT:`"$out\terrain_abi.exe`"").Replace('/MAP:king.map', "/MAP:`"$out\terrain_abi.map`"")
    $link += ' /BASE:0x20000000 /FIXED:NO /DYNAMICBASE /LARGEADDRESSAWARE'
    if ($NegativeControl -ne 'none') {
        $unit=if ($NegativeControl -eq 'caller') {'helicopter'} else {'terrain'}
        $area=if ($NegativeControl -eq 'caller') {'truck'} else {'terrain'}
        if ($NegativeControl -eq 'arch') { $unit='media'; $area='engine' }
        & $tc.Python "$PSScriptRoot\negative.py" $NegativeControl "$repo\src\$area\$unit.cpp" "$out\old-$unit.cpp"
        if ($LASTEXITCODE) { throw 'Negative source generation failed' }
        & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /GS- /Zl /GR- /EHs-c- /arch:IA32 "/I$repo\src\common" "/I$repo\src\$area" "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\old-$unit.obj" "$out\old-$unit.cpp" 2>&1 | Tee-Object "$out\negative-compile.log"
        if ($LASTEXITCODE) { throw 'Negative source compilation failed' }
        $link=$link.Replace(" $unit.obj ", " `"$out\old-$unit.obj`" ")
    }
    $link | Set-Content "$out\link-command.txt"
    # Parse NMAKE's quoted tokens and invoke link directly, without a second shell.
    $tokens=@([regex]::Matches($link, '(?:[^\s"]|"[^"]*")+') | ForEach-Object { $_.Value.Replace('"','') })
    $linkArgs=$tokens[1..($tokens.Count-1)]
    & $tokens[0] @linkArgs 2>&1 | Tee-Object "$out\link.log"
    if ($LASTEXITCODE) { throw "Harness link failed: $LASTEXITCODE" }
    # ASLR can place the fixture over the original's fixed address range.
    # Retry only this explicit mapping prerequisite, never a comparison failure.
    for ($attempt=1; $attempt -le 40; $attempt++) {
        $testOutput=@(& "$out\terrain_abi.exe" $GameExe 2>&1)
        $testExit=$LASTEXITCODE
        $testOutput | Set-Content "$out\attempt-$attempt.log"
        if ($testExit -ne 2 -or !($testOutput -match '^original address occupied:')) { break }
    }
    $testOutput | Tee-Object "$out\run.log"
    $geometryFailures=if ($NegativeControl -eq 'height') {8} else {0}
    $archFailures=if ($NegativeControl -eq 'arch') {4} else {0}
    if ($testOutput -notcontains "strip geometry: cases=6 failures=$archFailures") { throw "Strip geometry comparison incomplete or different: $out" }
    if ($testOutput -notcontains "plant geometry: cases=108 failures=$geometryFailures") { throw "Plant geometry comparison incomplete or different: $out" }
    $cases=@($testOutput | Where-Object { $_ -match '^case \d+ ' })
    if ($cases.Count -ne 11) { throw "Incomplete regression: $testExit (see $out)" }
    for ($i=0; $i -lt 11; $i++) {
        $badThis=($NegativeControl -eq 'caller' -and $i -ge 4 -and $i -le 7) -or ($NegativeControl -eq 'wrapper' -and $i -le 7) -or ($NegativeControl -eq 'plant' -and $i -ge 9)
        $thisOK=if ($badThis) {0} else {1}
        $verdict=if ($badThis) {'FAIL'} else {'PASS'}
        $pattern="^case $i \S+ reference=1 this=$thisOK copies=1 stack=1 inputs=1 args=1 return=1 globals=1 same=1 exception=00000000 fault=00000000 trace=\S+ $verdict`$"
        if ($cases[$i] -notmatch $pattern) { throw "Unexpected case result: $($cases[$i]) (see $out)" }
    }
    $expectedExit=if ($NegativeControl -eq 'none') {0} else {1}
    if ($testExit -ne $expectedExit) { throw "Unexpected exit: $testExit" }
    $sources=@('src/common/terrain_view.h','src/engine/media.cpp','src/truck/helicopter.cpp','src/terrain/terrain.cpp','tests/terrain_abi_test/terrain_abi_test.cpp','tests/terrain_abi_test/run.ps1','tests/terrain_abi_test/negative.py') | ForEach-Object { @{ path=$_; sha256=(Get-FileHash -LiteralPath "$repo/$_").Hash } }
    [ordered]@{ status='success'; kind='terrain-abi endpoint regression'; negativeControl=$NegativeControl; testExitCode=$testExit; sources=$sources; gameBuild=(Get-Content "$out\build.log.json" -Raw | ConvertFrom-Json); harnessSha256=(Get-FileHash "$out\terrain_abi.exe").Hash } | ConvertTo-Json -Depth 8 | Set-Content "$out\result.json" -Encoding UTF8
    "PASS terrain-abi regression; evidence: $out"
} catch {
    [ordered]@{ status='error'; error=$_.Exception.Message; evidence=$out } | ConvertTo-Json | Set-Content "$out\result.json" -Encoding UTF8
    throw
} finally { Pop-Location }
