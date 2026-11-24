# Independent PE alternative to the optional, undistributed inline-ASM fixtures.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [ValidateRange(1,100000)][int]$Cases=3000,
    [ValidateRange(0,1000000)][int]$FirstSeed=0,
    [ValidateSet('both','pc53','pc24')][string]$Precision='both',
    [string]$Destination,[string]$VcDir,[string]$SdkRoot,
    [string]$SdkVersion='10.0.26100.0',[string]$Python='python'
)
$ErrorActionPreference='Stop'
$GameExe=(Resolve-Path -LiteralPath $GameExe).Path
if((Get-FileHash -LiteralPath $GameExe).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD'){throw 'PE addresses require the verified Steam EXE'}
. "$PSScriptRoot/../../tools/windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$repo=(Resolve-Path "$PSScriptRoot/../..").Path
if(!$Destination){$Destination=Join-Path $repo ('.local/regressions/x87-pe-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))}
if(Test-Path -LiteralPath $Destination){throw 'Use a new destination'}
New-Item -ItemType Directory -Path $Destination | Out-Null
$out=(Resolve-Path -LiteralPath $Destination).Path
$rows=[Collections.Generic.List[object]]::new()
$result=[ordered]@{status='preparing';original=$GameExe;originalSha256=(Get-FileHash $GameExe).Hash;cases=$Cases;firstSeed=$FirstSeed;precision=$Precision;tests=$rows;started=(Get-Date).ToString('o')}
function Save-Result {$result | ConvertTo-Json -Depth 10 | Set-Content "$out/result.json" -Encoding utf8}
try {
    & "$repo/tools/build-windows.ps1" -GameExe $GameExe -VcDir $tc.VcDir -SdkRoot $tc.SdkRoot -SdkVersion $tc.SdkVersion -Python $tc.Python -LogPath "$out/build.log"
    $result.gameBuild=Get-Content "$out/build.log.json" -Raw | ConvertFrom-Json
    $result.sourceHashes=@('hitch_test.cpp','copter_test.cpp','small_test.cpp','generate_pe.py','extract.py','pe_reference.h','run-pe.ps1' | ForEach-Object {@{name=$_;sha256=(Get-FileHash "$PSScriptRoot/$_").Hash}})
    $result.runtimeSourceHashes=@('src/truck/world.cpp','src/engine/crt.cpp','src/common/x87.h' | ForEach-Object {@{path=$_;sha256=(Get-FileHash "$repo/$_").Hash}})
    $result.productionObjectHashes=@('hitch_solver.obj','copter_step.obj','x87.obj' | ForEach-Object {@{name=$_;sha256=(Get-FileHash "$repo/.local/build/$_").Hash}})
    & $tc.Python "$PSScriptRoot/generate_pe.py" $out
    if($LASTEXITCODE){throw 'PE adapter generation failed'}
    & $tc.Python "$PSScriptRoot/extract.py" "$out/small_gen.cpp" "$repo/src/ai/ai_containers.cpp:RopeIt_At,Rope_Compare" "$repo/src/ai/ai_drivers.cpp:Player_FromId"
    if($LASTEXITCODE){throw 'Small function extraction failed'}
    Copy-Item "$PSScriptRoot/pe_reference.h" $out
    $inc=@("/I$($tc.VcDir)/include","/I$($tc.SdkRoot)/Include/$($tc.SdkVersion)/um","/I$($tc.SdkRoot)/Include/$($tc.SdkVersion)/shared","/I$($tc.SdkRoot)/Include/$($tc.SdkVersion)/ucrt")
    & "$($tc.VcDir)/bin/HostX86/x86/cl.exe" /nologo /c /O2 /GS- /Zl /GR- /EHs-c- /arch:IA32 @inc "/I$repo/src/common" "/Fo$out/matherr-support.obj" "$out/matherr-support.cpp" 2>&1 | Tee-Object "$out/matherr-compile.log"
    if($LASTEXITCODE){throw 'Game CRT bridge compilation failed'}
    foreach($part in 'hitch','copter','small') {
        & "$($tc.VcDir)/bin/HostX86/x86/cl.exe" /nologo /c /O2 /arch:IA32 /D_CRT_SECURE_NO_WARNINGS /EHsc @inc "/Fo$out/$part-pe.obj" "$out/$part-pe.cpp" 2>&1 | Tee-Object "$out/$part-compile.log"
        if($LASTEXITCODE){throw "$part fixture compile failed"}
        $objects=@("$out/$part-pe.obj")
        if($part -ne 'small') {
            $source=if($part -eq 'hitch'){'hitch_solver'}else{'copter_step'}
            Copy-Item "$repo/.local/build/$source.obj","$repo/.local/build/x87.obj" $out -Force
            $objects+=@("$out/$source.obj","$out/x87.obj","$out/matherr-support.obj")
        }
        & "$($tc.VcDir)/bin/HostX86/x86/link.exe" /nologo @objects "/OUT:$out/$part-pe.exe" /SUBSYSTEM:CONSOLE /ENTRY:PeEntry /BASE:0x20000000 /DYNAMICBASE /LARGEADDRESSAWARE /FIXED:NO "/LIBPATH:$($tc.VcDir)/lib/x86" "/LIBPATH:$($tc.SdkRoot)/Lib/$($tc.SdkVersion)/um/x86" "/LIBPATH:$($tc.SdkRoot)/Lib/$($tc.SdkVersion)/ucrt/x86" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib 2>&1 | Tee-Object "$out/$part-link.log"
        if($LASTEXITCODE){throw "$part fixture link failed"}
        # Retry only before the independent reference can be mapped. Comparisons,
        # exceptions and crashes are never retried or treated as successful tests.
        for($attempt=1;$attempt -le 40;$attempt++) {
            $lines=@(& "$out/$part-pe.exe" $Cases $GameExe $FirstSeed $Precision 2>&1)
            $testExit=$LASTEXITCODE
            $lines | Set-Content "$out/$part-attempt-$attempt.log"
            if($testExit -ne 2 -or !($lines -match '^original address occupied:')){break}
        }
        $lines | Tee-Object "$out/$part-run.log"
        $state=if(!$testExit){'passed'}elseif($testExit -eq 2 -and ($lines -match '^original address occupied:')){'blocked'}else{'failed'}
        $rows.Add([ordered]@{name=$part;status=$state;exitCode=$testExit;harnessSha256=(Get-FileHash "$out/$part-pe.exe").Hash;log="$part-run.log"})
        Save-Result
    }
    $result.status=if(@($rows|Where-Object status -eq failed).Count){'failed'}elseif(@($rows|Where-Object status -eq blocked).Count){'blocked'}else{'passed'}
    $result.finished=(Get-Date).ToString('o');Save-Result
    "PE x87 result: $($result.status); $out/result.json"
    if($result.status -eq 'failed'){exit 1}
    if($result.status -eq 'blocked'){exit 2}
} catch {
    $result.status='error';$result.error=$_.Exception.Message;Save-Result;throw
}
