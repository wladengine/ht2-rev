[CmdletBinding()]
param([Parameter(Mandatory)][string]$GameExe, [string]$RespawnProfile, [switch]$CampaignClock)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameExe).Path
$source=Split-Path $game
$repo=(Resolve-Path "$PSScriptRoot/../..").Path
if ((Get-FileHash $game).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD' -or
    (Get-FileHash "$source/ENV/mod_ad.sch").Hash -ne 'C29F13351E779BE3C9DEF0FE447F6B42D58881100EB10E55EEB6B53265819EEA') { throw 'Expected the Russian Steam 8.1 fixture and its ENV directory.' }
$out=New-Item -ItemType Directory -Path "$repo/.local/regressions/world-event-$([guid]::NewGuid().ToString('N').Substring(0,8))"
. "$repo/tools/windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain
$vc=$tc.VcDir
$sdk="$($tc.SdkRoot)/Include/$($tc.SdkVersion)"
$includes=@("/I$vc/include","/I$sdk/um","/I$sdk/shared","/I$sdk/ucrt")
$cl="$vc/bin/HostX86/x86/cl.exe"
$worker="$PSScriptRoot/world_event.cpp"
$fleetPasses = @('fleet-hire','fleet-main','fleet-empty','fleet-merge','fleet-mixed','fleet-main-mixed','fleet-main-mixed-depots','fleet-salary','call100','print-list','ini-numbers','road-heading','mixer-failure','vector-copy-failure','fill-failure','provider-copy-failure','temp-buffer-failure')
$fleetPasses += @('coordinate-bounds','message-return','elapsed-time','scalar-comparisons','diagnostic-numbers','vehicle-state','geometry','weather-terrain','player-copy','engine-torque','triangle-arithmetic','body-frame','numeric-exceptions')
$fleetPasses += @('fleet-deadline','fleet-vehicle-settings','campaign-state')
if ($CampaignClock) { $fleetPasses += @('clock-a','clock-b') }
if ($RespawnProfile) {
    $profileCopy = (Resolve-Path -LiteralPath $RespawnProfile).Path
    if ((Get-FileHash -LiteralPath $profileCopy).Hash -ne '215D8F01620019FDFB04444FA6ADFE346631CD3D6B6D38C09C330077E793C080') {
        throw 'The respawn comparison uses the specified private regression profile.'
    }
    $fleetPasses += 'respawn'
}
Push-Location "$repo/.local/build"
try {
    & "$repo/tools/build-windows.ps1" -GameExe $game -LogPath "$out/build.log" *> "$out/build-console.txt"
    if ($LASTEXITCODE) { throw "Game build failed, see $out/build.log" }
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($LASTEXITCODE -or $link.Count -ne 1) { throw 'Game link command unavailable' }
    foreach ($side in 'stock','canonical') {
        $run=(New-Item -ItemType Directory -Path "$out/$side").FullName
        if ($RespawnProfile) { Copy-Item -LiteralPath $profileCopy -Destination "$run/respawn.pl1" }
        # Keep timestamps used by the game's vehicle data cache. Profiles and recordings are unnecessary.
        foreach ($item in Get-ChildItem -LiteralPath $source) {
            if ($item.PSIsContainer) {
                if ($item.Name -eq 'ENV') { Copy-Item -LiteralPath $item.FullName -Destination $run -Recurse }
            } elseif ($item.Extension -in '.sch','.ini','.dll','.cnf','.tech','.bne','.sft','.fon' -or $item.Name -eq 'DISK1') {
                Copy-Item -LiteralPath $item.FullName -Destination $run
            }
        }
        Copy-Item -LiteralPath "$PSScriptRoot/event-expression.sch" -Destination $run
        Copy-Item -LiteralPath "$PSScriptRoot/print-list.sch" -Destination $run
        if ($side -eq 'stock') {
            @('LIBRARY aiqa81','EXPORTS','Ht2Ach_Attach=WorldEvent') | Set-Content -Encoding ascii "$run/worker.def"
            $libs=@("/LIBPATH:$vc/lib/x86","/LIBPATH:$($tc.SdkRoot)/Lib/$($tc.SdkVersion)/ucrt/x86","/LIBPATH:$($tc.SdkRoot)/Lib/$($tc.SdkVersion)/um/x86")
            & $cl /nologo /LD /MT /EHs /EHc- /O2 /arch:IA32 /DSTOCK_WORLD /D_CRT_SECURE_NO_WARNINGS @includes "/Fo$run/worker.obj" $worker /link @libs "/IMPLIB:$run/worker.lib" "/DEF:$run/worker.def" "/OUT:$run/aiqa81.dll"
            if ($LASTEXITCODE) { throw 'Stock worker compile failed' }
            & $tc.Python "$repo/tests/ai_company_runs/prepare_host.py" $game "$run/event.exe"
        } else {
            & $cl /nologo /c /MT /EHs /EHc- /O2 /arch:IA32 /D_CRT_SECURE_NO_WARNINGS @includes "/Fo$run/worker.obj" $worker
            if ($LASTEXITCODE) { throw 'Canonical worker compile failed' }
            $cmd=$link[0].Trim().Replace('king.res',"`"$run/worker.obj`" king.res").Replace('/ENTRY:WinMainCRTStartup','/ENTRY:mainCRTStartup').Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/OUT:king.exe',"/OUT:`"$run/event.exe`"").Replace('/MAP:king.map',"/MAP:`"$run/event.map`"")
            $tokens=@([regex]::Matches($cmd,'(?:[^\s"]|"[^"]*")+') | ForEach-Object { $_.Value.Replace('"','') })
            $linkArgs=$tokens[1..($tokens.Count-1)]
            & $tokens[0] @linkArgs > "$run/link.log"
        }
        if ($LASTEXITCODE) { throw "Failed to prepare $side host" }
        foreach ($pass in $fleetPasses + @('controls-ai','token-math','road-script','vector','supercargo','box','allocator','fuel','roadside','finish','pickup','strong','rear','offset','center')) {
            $arguments=@{}
            if ($pass -ne 'center') { $arguments.ArgumentList="--$pass-pass" }
            $process=Start-Process -WindowStyle Hidden -FilePath "$run/event.exe" -WorkingDirectory $run -PassThru @arguments
            if (!$process.WaitForExit(90000)) { $process.Kill(); throw "$side $pass timed out in $run" }
            if ($process.ExitCode -ne 0 -or !(Test-Path "$run/event-result.txt") -or (Get-Content "$run/event-result.txt" -Encoding Ascii -Tail 1) -ne 'PASS') {
                throw "$side $pass failed ($($process.ExitCode)) in $run"
            }
            if ($pass -ne 'center') { Rename-Item -LiteralPath "$run/event-result.txt" -NewName "$pass-result.txt" }
        }
    }
    if ((Get-FileHash "$out/stock/event-result.txt").Hash -ne (Get-FileHash "$out/canonical/event-result.txt").Hash) { throw "World event mismatch, see $out" }
    if ((Get-FileHash "$out/stock/offset-result.txt").Hash -ne (Get-FileHash "$out/canonical/offset-result.txt").Hash) { throw "Offset approach mismatch, see $out" }
    if ((Get-FileHash "$out/stock/rear-result.txt").Hash -ne (Get-FileHash "$out/canonical/rear-result.txt").Hash) { throw "Rear impact mismatch, see $out" }
    if ((Get-FileHash "$out/stock/strong-result.txt").Hash -ne (Get-FileHash "$out/canonical/strong-result.txt").Hash) { throw "Strong rear impact mismatch, see $out" }
    if ((Get-FileHash "$out/stock/pickup-result.txt").Hash -ne (Get-FileHash "$out/canonical/pickup-result.txt").Hash) { throw "Pickup streak mismatch, see $out" }
    if ((Get-FileHash "$out/stock/finish-result.txt").Hash -ne (Get-FileHash "$out/canonical/finish-result.txt").Hash) { throw "Ring finish mismatch, see $out" }
    foreach ($pass in $fleetPasses + @('controls-ai','token-math','road-script','vector','supercargo','box','allocator','fuel','roadside')) {
        if ((Get-FileHash "$out/stock/$pass-result.txt").Hash -ne (Get-FileHash "$out/canonical/$pass-result.txt").Hash) { throw "$pass mismatch, see $out" }
    }
    $files=@($game,"$out/stock/ENV/mod_ad.sch","$out/canonical/ENV/mod_ad.sch","$out/stock/event-expression.sch",
             "$out/canonical/event-expression.sch","$out/stock/event.exe","$out/stock/aiqa81.dll",
             "$out/canonical/event.exe","$out/canonical/event.map","$out/stock/event-result.txt","$out/canonical/event-result.txt",
             "$out/stock/offset-result.txt","$out/canonical/offset-result.txt","$out/stock/rear-result.txt","$out/canonical/rear-result.txt",
             "$out/stock/strong-result.txt","$out/canonical/strong-result.txt","$out/stock/pickup-result.txt","$out/canonical/pickup-result.txt",
             "$out/stock/finish-result.txt","$out/canonical/finish-result.txt",
             "$out/stock/allocator-result.txt","$out/canonical/allocator-result.txt",
             "$out/stock/road-script-result.txt","$out/canonical/road-script-result.txt",
             "$out/stock/token-math-result.txt","$out/canonical/token-math-result.txt",
             "$out/stock/controls-ai-result.txt","$out/canonical/controls-ai-result.txt",
             "$out/stock/vector-result.txt","$out/canonical/vector-result.txt",
             "$out/stock/supercargo-result.txt","$out/canonical/supercargo-result.txt","$out/stock/box-result.txt","$out/canonical/box-result.txt",
             "$out/stock/fuel-result.txt","$out/canonical/fuel-result.txt","$out/stock/roadside-result.txt","$out/canonical/roadside-result.txt")
    foreach ($pass in $fleetPasses) { $files += @("$out/stock/$pass-result.txt","$out/canonical/$pass-result.txt") }
    Get-FileHash -LiteralPath $files | Select-Object Path,Hash | ConvertTo-Json | Set-Content "$out/hashes.json"
    Get-Content "$out/canonical/event-result.txt"
    "PASS $out"
} finally { Pop-Location }
