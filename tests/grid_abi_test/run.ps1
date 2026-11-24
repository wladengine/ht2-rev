# Grid thiscall regression against production and independently supplied original.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python',
    [ValidateSet('grid','solo')][string]$Suite='grid',
    [ValidateSet('none','direct','join','join-caller','marine','speed','dust','tyre','contact-axes','licence-recipient','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes')][string]$NegativeControl='none'
)
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $GameExe).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD') { throw 'Original fixture addresses require the verified Steam EXE hash' }
if (($Suite -eq 'grid' -and $NegativeControl -in 'speed','dust','tyre','contact-axes','licence-recipient','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes') -or
    ($Suite -eq 'solo' -and $NegativeControl -notin 'none','speed','dust','tyre','contact-axes','licence-recipient','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes')) { throw 'Control does not apply to this suite' }
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot\..\..").Path
$GameExe=(Resolve-Path -LiteralPath $GameExe).Path
$suiteName=if ($Suite -eq 'solo') {'solo-audit'} else {'grid-abi'}
$out="$repo\.local\regressions\$suiteName-$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $out | Out-Null
& "$repo\tools\build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out\build.log"
Push-Location "$repo/.local/build"
try {
    $makeArgs=$tc.MakeArgs
    $commands=& $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    if ($LASTEXITCODE) { throw 'Failed to generate link command' }
    $link=@($commands | Where-Object { $_ -match 'link.exe"' })
    if ($link.Count -ne 1) { throw 'Expected exactly one game link command' }
    & "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /Oy- /arch:IA32 /D_CRT_SECURE_NO_WARNINGS /EHsc "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\grid_abi_test.obj" "$PSScriptRoot\grid_abi_test.cpp" 2>&1 | Tee-Object "$out\compile.log"
    if ($LASTEXITCODE) { throw "Harness compile failed: $LASTEXITCODE" }
    $link=$link[0].Trim().Replace('king.res', "`"$out\grid_abi_test.obj`"").Replace('/SUBSYSTEM:WINDOWS,5.01','/SUBSYSTEM:CONSOLE').Replace('/ENTRY:WinMainCRTStartup','/ENTRY:GridEntry').Replace('/OUT:king.exe', "/OUT:`"$out\grid_abi.exe`"").Replace('/MAP:king.map', "/MAP:`"$out\grid_abi.map`"")
    $link += ' /BASE:0x20000000 /FIXED:NO /DYNAMICBASE /LARGEADDRESSAWARE'
    if ($NegativeControl -ne 'none') {
        $unit='grid_support'
        $area='terrain'
        if ($NegativeControl -in 'speed','light-lifetime','light-distance') { $area='truck'; $unit='effects' }
        if ($NegativeControl -eq 'wheel-plane') { $area='truck'; $unit='world' }
        if ($NegativeControl -eq 'wheel-support') { $area='truck'; $unit='vehicle' }
        if ($NegativeControl -eq 'body-planes') { $area='truck'; $unit='moving_bodies' }
        if ($NegativeControl -eq 'dust') { $area='truck'; $unit='world_objects' }
        if ($NegativeControl -eq 'licence-recipient') { $area='ai'; $unit='ai_world' }
        if ($NegativeControl -in 'tyre','contact-axes') { $area='truck'; $unit='vehicle_physics' }
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
    function Invoke-Comparison([string]$label, [string[]]$modeArgs) {
        for ($attempt=1; $attempt -le 40; $attempt++) {
            $lines=@(& "$out\grid_abi.exe" $GameExe @modeArgs 2>&1)
            $code=$LASTEXITCODE
            $lines | Set-Content "$out\$label-attempt-$attempt.log"
            if ($code -ne 2 -or !($lines -match '^original address occupied:')) { break }
        }
        $lines | Set-Content "$out\$label.log"
        return @{output=$lines; exit=$code}
    }
    if ($Suite -eq 'solo') {
        if ($NegativeControl -eq 'none') {
            $rival=Invoke-Comparison 'rival-campaign' @('rival-campaign')
            $rival.output | Write-Output
            if ($rival.exit -ne 0) { throw 'Rival campaign comparison failed' }
        }
        $company=Invoke-Comparison 'company' @('company')
        $company.output | Write-Output
        $oldRecipient=$NegativeControl -eq 'licence-recipient'
        $companyExit=if ($oldRecipient) {3} else {0}
        $awardErrors=if ($oldRecipient) {10} else {0}
        if ($company.exit -ne $companyExit -or
            'grid placement and countdown original=1 cases=432 full=198 mismatches=0' -notin $company.output -or
            'grid placement and countdown original=0 cases=432 full=198 mismatches=0' -notin $company.output -or
            'ring grid driver selection cases=64 mismatches=0' -notin $company.output -or
            'ring grid recruitment original=1 cases=192 attempted=24 mismatches=0' -notin $company.output -or
            'ring grid recruitment original=0 cases=192 attempted=24 mismatches=0' -notin $company.output -or
            'ring driver filters original=1 cases=192 mismatches=0' -notin $company.output -or
            'ring driver filters original=0 cases=192 mismatches=0' -notin $company.output -or
            'station purchases cases=180 paid=58 refused=58 mismatches=0' -notin $company.output -or
            'vehicle dealer quotes cases=648 offers=144 mismatches=0' -notin $company.output -or
            'first warehouse assignment original=1 cases=192 dispatches=126 mismatches=0' -notin $company.output -or
            'first warehouse assignment original=0 cases=192 dispatches=126 mismatches=0' -notin $company.output -or
            'campaign ring start original=1 cases=36 mismatches=0' -notin $company.output -or
            'campaign ring start original=0 cases=36 mismatches=0' -notin $company.output -or
            'mafia settlement original=1 cases=54 forfeits=18 bankruptcies=19 mismatches=0' -notin $company.output -or
            'mafia settlement original=0 cases=54 forfeits=18 bankruptcies=19 mismatches=0' -notin $company.output -or
            'company dissolution original=1 cases=20 mismatches=0' -notin $company.output -or
            'company dissolution original=0 cases=20 mismatches=0' -notin $company.output -or
            'ring checkpoints and payout original=1 cases=128 mismatches=0' -notin $company.output -or
            'ring checkpoints and payout original=0 cases=128 mismatches=0' -notin $company.output -or
            'traffic fines original=1 cases=128 mismatches=0' -notin $company.output -or
            'traffic fines original=0 cases=128 mismatches=0' -notin $company.output -or
            'delivery rank and projections original=1 cases=960 mismatches=0' -notin $company.output -or
            'delivery rank and projections original=0 cases=960 mismatches=0' -notin $company.output -or
            'campaign market share original=1 cases=48 mismatches=0' -notin $company.output -or
            'campaign market share original=0 cases=48 mismatches=0' -notin $company.output -or
            'fuel and roadside service cases=720 fills=80 repairs=48 mismatches=0' -notin $company.output -or
            'police quotes and mafia offers cases=6912 charges=260 refusals=1818 mismatches=0' -notin $company.output -or
            'mafia dispatch and release original=1 cases=64 mismatches=0' -notin $company.output -or
            'mafia dispatch and release original=0 cases=64 mismatches=0' -notin $company.output -or
            'mafia cargo and ambushes original=1 cases=300 mismatches=0' -notin $company.output -or
            'mafia cargo and ambushes original=0 cases=300 mismatches=0' -notin $company.output -or
            "ordinary delivery licences original=0 cases=120 mismatches=$awardErrors" -notin $company.output -or
            'ordinary delivery licences original=1 cases=120 mismatches=0' -notin $company.output -or
            'delivery to licence original=1 cases=16 mismatches=0' -notin $company.output -or
            ('delivery to licence original=0 cases=16 mismatches=' + $(if ($oldRecipient) {4} else {0})) -notin $company.output -or
            'AI first hire original=1 cases=48 schedulerHires=6 mismatches=0' -notin $company.output -or
            (!$oldRecipient -and 'AI first hire original=0 cases=48 schedulerHires=6 mismatches=0' -notin $company.output) -or
            'campaign competitor starts original=0 cases=16 mismatches=0' -notin $company.output -or
            'campaign competitor starts original=1 cases=16 mismatches=0' -notin $company.output) {
            throw "Company comparison failed: $out"
        }
        $gameplay=Invoke-Comparison 'gameplay' @('gameplay')
        $gameplay.output | Write-Output
        $expectedSpeedErrors=if ($NegativeControl -eq 'speed') {3} else {0}
        $expectedCode=if ($NegativeControl -in 'speed','dust','tyre','contact-axes','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes') {3} else {0}
        $tyreErrors=if ($NegativeControl -eq 'tyre') {8} else {0}
        $dustErrors=if ($NegativeControl -eq 'dust') {48} else {0}
        $rigErrors=if ($NegativeControl -eq 'contact-axes') {28} else {0}
        $priorityErrors=if ($NegativeControl -eq 'light-distance') {9} else {0}
        $lightPass=if ($NegativeControl -eq 'light-lifetime') {0} else {1}
        $planeErrors=if ($NegativeControl -eq 'wheel-plane') {10} else {0}
        $wheelErrors=switch ($NegativeControl) { 'wheel-plane' {60} 'wheel-support' {16} default {0} }
        $stormErrors=switch ($NegativeControl) { 'wheel-plane' {128} 'wheel-support' {59} default {0} }
        $hullResult=if ($NegativeControl -eq 'body-planes') {'mismatches=48 falseHits=40 missed=8'} else {'mismatches=0 falseHits=0 missed=0'}
        $requiredLines=@(
            'world script literals original=1 cases=180 mismatches=0'
            'world script literals original=0 cases=180 mismatches=0'
            'wheel damage and transmission cases=720 mismatches=0'
            'water contact cases=432 submerged=144 deepCabs=18 mismatches=0'
            'fire frames cases=192 nearCulled=32 expired=48 mismatches=0'
            "truck hull planes=6 cases=256 referenceHits=62 referenceErrors=0 $hullResult"
            "wheel plane intersections cases=12 mismatches=$planeErrors"
            "auxiliary wheel travel cases=96 mismatches=$wheelErrors"
            "Storm slope frames=144 mismatches=$stormErrors geometryErrors=0 compressed=92 extended=137 interior=347"
            "light retirement shared=$lightPass full=$lightPass"
            'traffic distance boundaries=60 mismatches=0'
            'scene light admission boundaries=4 mismatches=0'
            'light priority order, release and slot reuse match=1'
            "light distance priorities=9 mismatches=$priorityErrors"
            'live lights original comparisons=18 mismatches=0'
            "loaded rig paired steps=32 mismatches=$rigErrors"
            'employee delivery cases=64 mismatches=0'
            "racer target speed cases=448 mismatches=$expectedSpeedErrors"
            'racer advance cases=224 mismatches=0'
            'outline chains cases=12 mismatches=0'
            'body frame snapshots=36 mismatches=0'
            "loaded wheel contacts cases=16 mismatches=$tyreErrors"
            'suspension road steps=18 bottomed=12 unsupported=12 mismatches=0'
            'competitor order list clear cases=32 mismatches=0'
            'hitch event snapshots=36 changed responses=24 mismatches=0'
            'chase waypoint routes cases=16 mismatches=0'
            'traffic jam groups=18 mismatches=0'
            'road search cold and cached queries=96 mismatches=0'
            'racer vehicle filter snapshots=128 mismatches=0'
            'trailer saddle load cases=48 mismatches=0'
            'marine angle samples=720 lookups=2882 mismatches=0'
            'marine transforms cases=16 mismatches=0'
            'marine material clock cases=60 mismatches=0'
            'wheel emission cases=32 mismatches=0'
            'wheel velocities cases=120 emissions=36 mismatches=0'
            'marine edge lifecycle cases=40 mismatches=0'
            'vertex transition cases=45 mismatches=0'
            'grid startup slots=64 snapshots=128 mismatches=0'
            'global startup slots=59 snapshots=118 mismatches=0'
            "dust pairs cases=108 puffs=96 mismatches=$dustErrors"
        )
        foreach ($required in $requiredLines) {
            if ($required -notin $gameplay.output) { throw "Missing verification: $required" }
        }
        if ($gameplay.exit -ne $expectedCode) { throw "Unexpected gameplay exit: $($gameplay.exit)" }
        $tech = Join-Path (Split-Path -Parent (Resolve-Path -LiteralPath $GameExe)) 'vehicle.tech'
        if ((Test-Path -LiteralPath $tech) -and (Get-FileHash -LiteralPath $tech).Hash -eq '65DD8CC29AD141E389D0C516A8765055F201CCEEBBDAA86082D48AB5DF4E1EDC') {
            $zil = Invoke-Comparison 'bridge-zil' @('bridge-hull',$tech)
            $zil.output | Write-Output
            $oldHull = $NegativeControl -eq 'body-planes'
            $zilCounts = if ($oldHull) {'mismatches=56 falseHits=52 missed=4'} else {'mismatches=0 falseHits=0 missed=0'}
            $zilExit = if ($oldHull) {3} else {0}
            if ($zil.exit -ne $zilExit -or "truck hull planes=13 cases=256 referenceHits=32 referenceErrors=0 $zilCounts" -notin $zil.output) {
                throw "ZIL collision hull comparison failed: $out"
            }
        }
        $testExit=$gameplay.exit
    } else {
        [string[]]$modeArgs=@(if ($NegativeControl -eq 'marine') { 'old-marine' })
        $grid=Invoke-Comparison 'run' $modeArgs
        $testOutput=$grid.output
        $testExit=$grid.exit
        $testOutput | Write-Output
        foreach ($line in @('priority/wedge pairs=88 mismatches=0','real split cases=24 mismatches=0','merge 2 cells cases=4 mismatches=0','merge 4 cells cases=4 mismatches=0','grid sequence snapshots=56 mismatches=0','shared edge camera snapshots=16 mismatches=0','nonempty split cases=40 mismatches=0')) {
            if ($line -notin $testOutput) { throw "Missing verification: $line" }
        }
        $wrongMesh=if ($NegativeControl -eq 'marine') {'[1-4]'} else {'0'}
        if (!($testOutput -match "^marine inner edge cases=6 wrong mesh=$wrongMesh$")) { throw 'Missing marine join verification' }
        $cases=@($testOutput | Where-Object { $_ -match '^case [0-9]+ ' })
        if ($cases.Count -ne 36) { throw "Incomplete regression: $testExit (see $out)" }
        for ($i=0; $i -lt 36; $i++) {
            $badThis=0
            if ($NegativeControl -eq 'direct' -and (($i -ge 12 -and $i -le 15) -or $i -ge 28)) { $badThis=1 }
            if ($NegativeControl -eq 'join' -and ($i -le 3 -or ($i -ge 24 -and $i -le 31))) { $badThis=1 }
            if ($NegativeControl -eq 'join-caller' -and $i -ge 24 -and $i -le 31) { $badThis=1 }
            $verdict=if ($badThis) {'FAIL'} else {'PASS'}
            $pattern="^case $i reference=1 badThis=$badThis args=1 stack=1 state=1 same=1 splits=[0-2] exception=00000000 fault=00000000 trace=\S+ $verdict`$"
            if ($cases[$i] -notmatch $pattern) { throw "Unexpected case result: $($cases[$i]) (see $out)" }
        }
        $expectedExit=if ($NegativeControl -in 'none','marine') {0} else {1}
        if ($testExit -ne $expectedExit) { throw "Unexpected exit: $testExit" }
    }
    $sources=@('src/terrain/grid_topology.cpp','src/terrain/terrain_grid.cpp','src/marine/marine_core.cpp','src/terrain/grid_support.cpp','tests/grid_abi_test/grid_abi_test.cpp','tests/grid_abi_test/merge_cases.h','tests/grid_abi_test/sequence_cases.h','tests/solo_audit_test/body_cases.h','tests/solo_audit_test/route_cases.h','tests/solo_audit_test/cases.h','tests/solo_audit_test/run.ps1','tests/solo_audit_test/gameplay_cases.h','tests/solo_audit_test/marine_math_cases.h','tests/solo_audit_test/wheel_cases.h','tests/solo_audit_test/edge_pair_cases.h','tests/solo_audit_test/startup_cases.h','tests/solo_audit_test/licence_cases.h','tests/solo_audit_test/loaded_rig.h','tests/solo_audit_test/employee_delivery.h','tests/solo_audit_test/company_world.h','tests/solo_audit_test/company_endings.h','tests/solo_audit_test/market_share.h','tests/solo_audit_test/rival_campaign.h','tests/solo_audit_test/ring_race.h','tests/solo_audit_test/delivery_rank.h','tests/solo_audit_test/roadside_service.h','tests/solo_audit_test/police_fines.h','tests/solo_audit_test/bribe_offers.h','tests/solo_audit_test/mafia_settlement.h','tests/solo_audit_test/ring_start.h','tests/solo_audit_test/ring_grid.h','tests/solo_audit_test/ring_countdown.h','tests/solo_audit_test/rank_projection_replay.h','tests/solo_audit_test/service_shop.h','tests/solo_audit_test/vehicle_dealers.h','tests/solo_audit_test/first_assignment.h','tests/solo_audit_test/world_reader.h','tests/solo_audit_test/vehicle_damage.h','tests/solo_audit_test/water_contact.h','tests/solo_audit_test/fire_frames.h','src/truck/vehicle_setup.cpp','src/script/values.cpp','src/script/native_values.cpp','src/truck/helicopter.cpp','src/truck/game_shell.cpp','tests/solo_audit_test/outline_chain.h','tests/solo_audit_test/light_lifetime.h','tests/solo_audit_test/bukhta_cases.h','tests/solo_audit_test/auxiliary_wheels.h','tests/solo_audit_test/bridge_contacts.h','src/truck/moving_bodies.cpp','src/truck/world.cpp','src/engine/scene.cpp','src/engine/framework.cpp','src/engine/outline_sphere.h','src/truck/hitch_solver.cpp','src/ai/cargo_orders.cpp','src/ai/ai_drivers.cpp','src/truck/effects.cpp','src/truck/vehicle_physics.cpp','src/truck/world_objects.cpp','src/truck/vehicle.cpp','src/ai/ai_world.cpp','tests/grid_abi_test/mesh_join_cases.h','tests/grid_abi_test/run.ps1','tests/grid_abi_test/negative.py') | ForEach-Object { @{ path=$_; sha256=(Get-FileHash -LiteralPath "$repo/$_").Hash } }
    $companyExitCode=if ($Suite -eq 'solo') {$company.exit} else {$null}
    [ordered]@{ status='success'; kind=$suiteName; negativeControl=$NegativeControl; testExitCode=$testExit; companyExitCode=$companyExitCode; sources=$sources; gameBuild=(Get-Content "$out\build.log.json" -Raw | ConvertFrom-Json); harnessSha256=(Get-FileHash "$out\grid_abi.exe").Hash } | ConvertTo-Json -Depth 8 | Set-Content "$out\result.json" -Encoding UTF8
    "PASS $suiteName regression; evidence: $out"
} catch {
    [ordered]@{ status='error'; error=$_.Exception.Message; evidence=$out } | ConvertTo-Json | Set-Content "$out\result.json" -Encoding UTF8
    throw
} finally { Pop-Location }
