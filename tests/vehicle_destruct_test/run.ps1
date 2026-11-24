[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination,[string]$Python='python',[string]$VehicleSource)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\..\tools\windows-toolchain.ps1"
$tc=Resolve-WindowsToolchain -Python $Python
$out=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $out) { throw 'Use a new destination' }
New-Item -ItemType Directory -Path $out|Out-Null
$generateArgs=@("$PSScriptRoot\generate.py","$out\fixture.cpp")
if ($VehicleSource) { $generateArgs += (Resolve-Path -LiteralPath $VehicleSource).Path }
& $tc.Python @generateArgs
if($LASTEXITCODE) { throw 'Generation failed' }
& "$($tc.VcDir)\bin\HostX86\x86\cl.exe" /nologo /c /O2 /GS- /arch:IA32 /D_CRT_SECURE_NO_WARNINGS "/I$($tc.VcDir)\include" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\um" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\shared" "/I$($tc.SdkRoot)\Include\$($tc.SdkVersion)\ucrt" "/Fo$out\fixture.obj" "$out\fixture.cpp" 2>&1 | Tee-Object "$out\compile.log"
if($LASTEXITCODE) { throw 'Compile failed' }
& "$($tc.VcDir)\bin\HostX86\x86\link.exe" /nologo "$out\fixture.obj" "/OUT:$out\fixture.exe" /SUBSYSTEM:CONSOLE /MACHINE:X86 "/LIBPATH:$($tc.VcDir)\lib\x86" "/LIBPATH:$($tc.SdkRoot)\Lib\$($tc.SdkVersion)\ucrt\x86" "/LIBPATH:$($tc.SdkRoot)\Lib\$($tc.SdkVersion)\um\x86" kernel32.lib libcmt.lib libvcruntime.lib libucrt.lib legacy_stdio_definitions.lib 2>&1 | Tee-Object "$out\link.log"
if($LASTEXITCODE) { throw 'Link failed' }
& "$out\fixture.exe" 2>&1 | Tee-Object "$out\run.log"
if($LASTEXITCODE) { throw 'Vehicle resource-ID regression failed' }
