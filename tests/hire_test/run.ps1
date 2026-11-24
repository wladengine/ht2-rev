[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination,[string]$Python='python',[string]$Source,[switch]$NegativeControl)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -Python $Python
$out=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $out) { throw 'Use a new destination' }
New-Item -ItemType Directory -Path $out|Out-Null
$generateArgs=@("$PSScriptRoot\generate.py","$out\fixture.cpp")
if ($Source) { $generateArgs += (Resolve-Path -LiteralPath $Source).Path }
if ($NegativeControl) { $generateArgs += '--negative-control' }
& $tc.Python @generateArgs
if($LASTEXITCODE) { throw 'Generation failed' }
& "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /GS- /arch:IA32 /D_CRT_SECURE_NO_WARNINGS "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\fixture.obj" "$out\fixture.cpp" 2>&1 | Tee-Object "$out\compile.log"
if($LASTEXITCODE) { throw 'Compile failed' }
& "$($tc.VcDir)\bin\HostX86\x86\link.exe" /nologo "$out\fixture.obj" "/OUT:$out\fixture.exe" /SUBSYSTEM:CONSOLE /MACHINE:X86 "/LIBPATH:$($tc.VcDir)\lib\x86" "/LIBPATH:$($tc.SdkRoot)\Lib\$($tc.SdkVersion)\ucrt\x86" "/LIBPATH:$($tc.SdkRoot)\Lib\$($tc.SdkVersion)\um\x86" kernel32.lib libcmt.lib libvcruntime.lib libucrt.lib legacy_stdio_definitions.lib 2>&1 | Tee-Object "$out\link.log"
if($LASTEXITCODE) { throw 'Link failed' }
$lines=@(& "$out\fixture.exe" 2>&1)
$testExit=$LASTEXITCODE
$lines | Tee-Object "$out\run.log"
if ($NegativeControl) {
    $bad=@($lines | Where-Object { $_ -match '^hire-(success|refusal): hire=1 swap=0 correct_hirer=0 FAIL$' })
    $good=@($lines | Where-Object { $_ -match ' PASS$' })
    if ($testExit -ne 1 -or $bad.Count -ne 2 -or $good.Count -ne 13) { throw 'Negative control did not reproduce the argument defect' }
} elseif ($testExit) { throw 'Hire argument regression failed' }
[ordered]@{status='success';negativeControl=[bool]$NegativeControl;fixtureExitCode=$testExit;cases=15;source=(Get-Content "$out\fixture.json" -Raw | ConvertFrom-Json);fixtureSha256=(Get-FileHash "$out\fixture.exe").Hash} | ConvertTo-Json -Depth 4 | Set-Content "$out\result.json" -Encoding UTF8
"PASS hire regression: $out"
