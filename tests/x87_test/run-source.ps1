param([Parameter(Mandatory)][string]$GameExe, [ValidateRange(1,100000)][int]$Cases = 3000, [Parameter(Mandatory)][string]$ReferenceDir, [string]$VcDir, [string]$SdkRoot, [string]$SdkVersion='10.0.26100.0', [string]$Python='python')
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\..\.."
$ReferenceDir = (Resolve-Path -LiteralPath $ReferenceDir).Path
foreach ($f in 'hitch_asm.cpp', 'copter_asm.cpp', 'small_asm.cpp') {
    if (-not (Test-Path "$ReferenceDir\$f")) { throw "$f is missing: the reference machine code is not in the repository" }
}
$GameExe = (Resolve-Path -LiteralPath $GameExe).Path
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc = Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$vc = $tc.VcDir; $sdk = $tc.SdkRoot; $sv = $tc.SdkVersion
$out = "$repo\.local\regressions\x87-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item $GameExe "$out\king_original.exe" -Force

& $tc.Python "$PSScriptRoot\x87_to_c.py" "$ReferenceDir\hitch_asm.cpp" L_FUN_558f20 Hitch_SolveC "$out\hitch_c.cpp" --cov
if ($LASTEXITCODE) { throw 'reference/translation generation failed' }
& $tc.Python "$PSScriptRoot\x87_to_c.py" "$ReferenceDir\hitch_asm.cpp" L_FUN_558f20 Hitch_Solve "$out\hitch_plain.cpp"
if ($LASTEXITCODE) { throw 'reference/translation generation failed' }
# the game's file is the plain translation with its own header (the first comment lines)
$a = Get-Content "$out\hitch_plain.cpp" | Where-Object { $_ -notmatch '^//' }
$b = Get-Content "$repo\src\truck\hitch_solver.cpp" | Where-Object { $_ -notmatch '^//' }
if (Compare-Object $a $b -SyncWindow 0) { throw 'src\truck\hitch_solver.cpp is not the current translation of hitch_asm.cpp' }

Push-Location $out
try {
$cl = "$vc\bin\HostX86\x86\cl.exe"
$inc = @("/I$repo\src\common", "/I$vc\include", "/I$sdk\Include\$sv\um", "/I$sdk\Include\$sv\shared", "/I$sdk\Include\$sv\ucrt")
# the game's CFLAGS for the code under test: the compiler's choices decide the rounding
$game = @('/nologo', '/c', '/O2', '/W3', '/GS-', '/Zl', '/GR-', '/EHs-c-', '/arch:IA32') + $inc
# the x87 helpers (x87.h) as the game builds them
& $cl @game /fp:fast "$repo\src\common\x87.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& $cl @game "$ReferenceDir\hitch_asm.cpp" "$out\hitch_c.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& $cl /nologo /c /O2 /W3 /EHsc /arch:IA32 /D_CRT_SECURE_NO_WARNINGS @inc "$PSScriptRoot\hitch_test.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& "$vc\bin\HostX86\x86\link.exe" /nologo hitch_test.obj hitch_asm.obj hitch_c.obj x87.obj /OUT:hitch_test.exe /SUBSYSTEM:CONSOLE /LIBPATH:"$vc\lib\x86" /LIBPATH:"$sdk\Lib\$sv\ucrt\x86" /LIBPATH:"$sdk\Lib\$sv\um\x86" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib
if ($LASTEXITCODE) { throw 'link failed' }
& .\hitch_test.exe $Cases "$out\king_original.exe"
$fail = $LASTEXITCODE

& $tc.Python "$PSScriptRoot\x87_to_c.py" "$ReferenceDir\copter_asm.cpp" L_FUN_563600 Copter_StepC "$out\copter_c.cpp" --cov
if ($LASTEXITCODE) { throw 'reference/translation generation failed' }
& $tc.Python "$PSScriptRoot\x87_to_c.py" "$ReferenceDir\copter_asm.cpp" L_FUN_563600 Copter_Step "$out\copter_plain.cpp"
if ($LASTEXITCODE) { throw 'reference/translation generation failed' }
$a = Get-Content "$out\copter_plain.cpp" | Where-Object { $_ -notmatch '^//' }
$b = Get-Content "$repo\src\truck\copter_step.cpp" | Where-Object { $_ -notmatch '^//' }
if (Compare-Object $a $b -SyncWindow 0) { throw 'src\truck\copter_step.cpp is not the current translation of copter_asm.cpp' }
& $cl @game "$ReferenceDir\copter_asm.cpp" "$out\copter_c.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& $cl /nologo /c /O2 /W3 /EHsc /arch:IA32 /D_CRT_SECURE_NO_WARNINGS @inc "$PSScriptRoot\copter_test.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& "$vc\bin\HostX86\x86\link.exe" /nologo copter_test.obj copter_asm.obj copter_c.obj x87.obj /OUT:copter_test.exe /SUBSYSTEM:CONSOLE /LIBPATH:"$vc\lib\x86" /LIBPATH:"$sdk\Lib\$sv\ucrt\x86" /LIBPATH:"$sdk\Lib\$sv\um\x86" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib
if ($LASTEXITCODE) { throw 'link failed' }
& .\copter_test.exe $Cases "$out\king_original.exe"
$fail = $fail -bor $LASTEXITCODE

& $tc.Python "$PSScriptRoot\extract.py" "$out\small_gen.cpp" "$repo\src\ai\ai_containers.cpp:RopeIt_At,Rope_Compare" "$repo\src\ai\ai_drivers.cpp:Player_FromId"
if ($LASTEXITCODE) { throw 'reference/translation generation failed' }
& $cl @game "$ReferenceDir\small_asm.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& $cl /nologo /c /O2 /W3 /EHsc /arch:IA32 /D_CRT_SECURE_NO_WARNINGS "/I$out" @inc "$PSScriptRoot\small_test.cpp"
if ($LASTEXITCODE) { throw 'compile failed' }
& "$vc\bin\HostX86\x86\link.exe" /nologo small_test.obj small_asm.obj /OUT:small_test.exe /SUBSYSTEM:CONSOLE /LIBPATH:"$vc\lib\x86" /LIBPATH:"$sdk\Lib\$sv\ucrt\x86" /LIBPATH:"$sdk\Lib\$sv\um\x86" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib
if ($LASTEXITCODE) { throw 'link failed' }
& .\small_test.exe $Cases
$fail = $fail -bor $LASTEXITCODE
if ($fail) { throw "x87 comparison failed: $fail" }
} finally { Pop-Location }
