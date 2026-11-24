[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [Parameter(Mandatory)][string]$Output,
    [string]$VcDir,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.26100.0',
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../../tools/windows-toolchain.ps1"
$toolchain = Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
$game = (Resolve-Path -LiteralPath $GameExe).Path
if ((Get-FileHash -LiteralPath $game).Hash -ne '31FBD1DBBE7706183F09A3688482791255DB2BFB557C02675374B96796CE2EBD') {
    throw 'The benchmark requires the verified Steam 8.1 executable.'
}
$outDir = (New-Item -ItemType Directory -Path $Output).FullName
$vc = $toolchain.VcDir
$sdk = $toolchain.SdkRoot
$ver = $toolchain.SdkVersion
$definition = Join-Path $outDir 'worker.def'
@('LIBRARY aiqa81', 'EXPORTS', 'Ht2Ach_Attach=CompanyRun') | Set-Content -Encoding ascii -LiteralPath $definition
$compilerArgs = @(
    '/nologo', '/LD', '/MT', '/O2', '/EHsc', '/arch:IA32', '/D_CRT_SECURE_NO_WARNINGS',
    "/I$vc/include", "/I$sdk/Include/$ver/um", "/I$sdk/Include/$ver/shared", "/I$sdk/Include/$ver/ucrt",
    "/Fo$outDir/worker.obj", "$PSScriptRoot/stock_worker.cpp", '/link',
    "/LIBPATH:$vc/lib/x86", "/LIBPATH:$sdk/Lib/$ver/ucrt/x86", "/LIBPATH:$sdk/Lib/$ver/um/x86",
    "/DEF:$definition", "/IMPLIB:$outDir/worker.lib", "/OUT:$outDir/aiqa81.dll"
)
& "$vc/bin/HostX86/x86/cl.exe" @compilerArgs
if ($LASTEXITCODE) { throw "Worker compilation failed ($LASTEXITCODE)." }
& $toolchain.Python "$PSScriptRoot/prepare_host.py" $game "$outDir/stock-campaign.exe"
if ($LASTEXITCODE) { throw "Host preparation failed ($LASTEXITCODE)." }
Get-FileHash -LiteralPath "$outDir/stock-campaign.exe","$outDir/aiqa81.dll"
