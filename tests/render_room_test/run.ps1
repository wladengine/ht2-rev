# Render-room endpoint regression against production and independently supplied original.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python',
    [switch]$NegativeControl
)
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $GameExe).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD') { throw 'Original fixture addresses require the verified Steam EXE hash' }
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
$out="$repo\.local\regressions\render-room-$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $out | Out-Null
& "$repo\tools\build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out\build.log"
Push-Location "$repo/.local/build"
try {
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    if ($LASTEXITCODE) { throw 'Failed to generate link command' }
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($link.Count -ne 1) { throw 'Expected exactly one game link command' }
    & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /arch:IA32 /D_CRT_SECURE_NO_WARNINGS /EHsc "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\render_room_test.obj" "$PSScriptRoot\render_room_test.cpp" 2>&1 | Tee-Object "$out\compile.log"
    if ($LASTEXITCODE) { throw "Harness compile failed: $LASTEXITCODE" }
    $link=$link[0].Trim().Replace('king.res', "`"$out\render_room_test.obj`"").Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/ENTRY:WinMainCRTStartup','/ENTRY:TrafficEntry').Replace('/OUT:king.exe', "/OUT:`"$out\render_room.exe`"").Replace('/MAP:king.map', "/MAP:`"$out\render_room.map`"")
    $link += ' /BASE:0x20000000 /FIXED:NO /DYNAMICBASE /LARGEADDRESSAWARE'
    if ($NegativeControl) {
        & $tc.Python "$PSScriptRoot\negative.py" "$repo\src\truck\object_ids.cpp" "$out\old-object-ids.cpp"
        if ($LASTEXITCODE) { throw 'Negative source generation failed' }
        & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /GS- /Zl /GR- /EHs-c- /arch:IA32 "/I$repo\src\common" "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\old-object-ids.obj" "$out\old-object-ids.cpp" 2>&1 | Tee-Object "$out\negative-compile.log"
        if ($LASTEXITCODE) { throw 'Negative source compilation failed' }
        $link=$link.Replace(' object_ids.obj ', " `"$out\old-object-ids.obj`" ")
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
        $testOutput=@(& "$out\render_room.exe" $GameExe 2>&1)
        $testExit=$LASTEXITCODE
        $testOutput | Set-Content "$out\attempt-$attempt.log"
        if ($testExit -ne 2 -or !($testOutput -match '^original address occupied:')) { break }
    }
    $testOutput | Tee-Object "$out\run.log"
    if ($NegativeControl) {
        $rejected=@($testOutput | Where-Object { $_ -match '^case [0-2] reference=1 target=0 other_state=1 .* FAIL$' })
        $passed=@($testOutput | Where-Object { $_ -match '^case [34] reference=1 target=1 other_state=1 .* PASS$' })
        if ($testExit -ne 1 -or $rejected.Count -ne 3 -or $passed.Count -ne 2) { throw "Negative control did not reproduce three endpoint-only failures: $testExit" }
    } elseif ($testExit) { throw "Render-room regression failed: $testExit (see $out)" }
    [ordered]@{ status='success'; kind='render-room endpoint regression'; negativeControl=[bool]$NegativeControl; testExitCode=$testExit; gameBuild=(Get-Content "$out\build.log.json" -Raw | ConvertFrom-Json); harnessSha256=(Get-FileHash "$out\render_room.exe").Hash } | ConvertTo-Json -Depth 8 | Set-Content "$out\result.json" -Encoding UTF8
    "PASS render-room regression; evidence: $out"
} catch {
    [ordered]@{ status='error'; error=$_.Exception.Message; evidence=$out } | ConvertTo-Json | Set-Content "$out\result.json" -Encoding UTF8
    throw
} finally { Pop-Location }
