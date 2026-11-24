# Network address endpoint regression against production and independently supplied original.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python',
    [ValidateSet('none','enum','size','write','all')][string]$NegativeControl='none'
)
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $GameExe).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD') { throw 'Original fixture addresses require the verified Steam EXE hash' }
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
$out="$repo\.local\regressions\network-address-$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $out | Out-Null
& "$repo\tools\build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out\build.log"
Push-Location "$repo/.local/build"
try {
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    if ($LASTEXITCODE) { throw 'Failed to generate link command' }
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($link.Count -ne 1) { throw 'Expected exactly one game link command' }
    & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /arch:IA32 /D_CRT_SECURE_NO_WARNINGS /EHsc "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\network_address_test.obj" "$PSScriptRoot\network_address_test.cpp" 2>&1 | Tee-Object "$out\compile.log"
    if ($LASTEXITCODE) { throw "Harness compile failed: $LASTEXITCODE" }
    $link=$link[0].Trim().Replace('king.res', "`"$out\network_address_test.obj`"").Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/ENTRY:WinMainCRTStartup','/ENTRY:TrafficEntry').Replace('/OUT:king.exe', "/OUT:`"$out\network_address.exe`"").Replace('/MAP:king.map', "/MAP:`"$out\network_address.map`"")
    $link += ' /BASE:0x20000000 /FIXED:NO /DYNAMICBASE /LARGEADDRESSAWARE'
    if ($NegativeControl -ne 'none') {
        & $tc.Python "$PSScriptRoot\negative.py" $NegativeControl "$repo\src\sound\sound_gate.cpp" "$out\old-sound_gate.cpp"
        if ($LASTEXITCODE) { throw 'Negative source generation failed' }
        & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /GS- /Zl /GR- /EHs-c- /arch:IA32 "/I$repo\src\common" "/I$repo\src\truck" "/I$repo\src\ai" "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\old-sound_gate.obj" "$out\old-sound_gate.cpp" 2>&1 | Tee-Object "$out\negative-compile.log"
        if ($LASTEXITCODE) { throw 'Negative source compilation failed' }
        $link=$link.Replace(' sound_gate.obj ', " `"$out\old-sound_gate.obj`" ")
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
        $testOutput=@(& "$out\network_address.exe" $GameExe 2>&1)
        $testExit=$LASTEXITCODE
        $testOutput | Set-Content "$out\attempt-$attempt.log"
        if ($testExit -ne 2 -or !($testOutput -match '^original address occupied:')) { break }
    }
    $testOutput | Tee-Object "$out\run.log"
    if ($NegativeControl -ne 'none') {
        $expected=switch($NegativeControl){'enum'{17};'size'{16};'write'{12};'all'{17}}
        $rejected=@($testOutput | Where-Object { $_ -match '^address .*reference=1 equal=1 abi=0 FAIL$' })
        $allFailures=@($testOutput | Where-Object { $_ -match ' FAIL$' })
        if ($testExit -ne 1 -or $rejected.Count -ne $expected -or $allFailures.Count -ne $expected) { throw "Negative control failed: $testExit; expected=$expected actual=$($rejected.Count)" }
    } elseif ($testExit) { throw "Network address regression failed: $testExit (see $out)" }
    [ordered]@{ status='success'; kind='Network address differential regression'; negativeControl=$NegativeControl; testExitCode=$testExit; gameBuild=(Get-Content "$out\build.log.json" -Raw | ConvertFrom-Json); harnessSha256=(Get-FileHash "$out\network_address.exe").Hash } | ConvertTo-Json -Depth 8 | Set-Content "$out\result.json" -Encoding UTF8
    "PASS network-address regression; evidence: $out"
} catch {
    [ordered]@{ status='error'; error=$_.Exception.Message; evidence=$out } | ConvertTo-Json | Set-Content "$out\result.json" -Encoding UTF8
    throw
} finally { Pop-Location }
