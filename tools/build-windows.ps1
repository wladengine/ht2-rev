[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameExe,
    [switch]$Clean,
    [switch]$Check,
    [string]$VcDir,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.26100.0',
    [string]$Python = 'python',
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\windows-toolchain.ps1"
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$GameExe = (Resolve-Path -LiteralPath $GameExe).Path
$configuration = 'build'
$buildDir = "$repo\.local\$configuration"
if ($GameExe.StartsWith("$repo\.local\build\", 'OrdinalIgnoreCase')) { throw 'GameExe must be an original game EXE, not the build output.' }
$tc = Resolve-WindowsToolchain -VcDir $VcDir -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Python $Python
if (!$LogPath) { $LogPath = "$repo\.local\logs\$configuration-$(Get-Date -Format yyyyMMdd-HHmmss-fff).log" }
$LogPath = [IO.Path]::GetFullPath($LogPath)
New-Item -ItemType Directory -Force (Split-Path $LogPath) | Out-Null
$manifestPath = "$LogPath.json"
$configPath = "$buildDir\build-config.json"
New-Item -ItemType Directory -Force $buildDir | Out-Null
$config = [ordered]@{ toolchain=$tc; makefile=(Get-FileHash "$repo\Makefile").Hash }
$configJson = $config | ConvertTo-Json -Depth 5
Push-Location $buildDir
try {
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE) { throw 'Cannot identify source commit' }
    $dirty = @(& git status --porcelain)
    $manifest = [ordered]@{ status='running'; commit=$commit; dirty=$dirty; started=(Get-Date -Format o); toolchain=$tc; gameExe=$GameExe; gameSha256=(Get-FileHash -LiteralPath $GameExe).Hash; clean=[bool]$Clean; log=$LogPath }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8
    $makeArgs = $tc.MakeArgs + "GAME_EXE=$GameExe"
    $previous = if (Test-Path $configPath) { Get-Content $configPath -Raw } else { '' }
    if ($Clean -or $previous.Trim() -ne $configJson.Trim()) {
        "Cleaning known build artifacts (resources included)." | Tee-Object -FilePath $LogPath
        & $tc.NMake /nologo /f ..\..\Makefile @makeArgs distclean 2>&1 | Tee-Object -FilePath $LogPath -Append
        if ($LASTEXITCODE) { throw "nmake distclean failed: $LASTEXITCODE" }
    }
    $configJson | Set-Content $configPath -Encoding UTF8
    $target = if ($Check) { 'check' } else { 'build' }
    & $tc.NMake /nologo /f ..\..\Makefile @makeArgs $target 2>&1 | Tee-Object -FilePath $LogPath -Append
    if ($LASTEXITCODE) { throw "nmake build failed: $LASTEXITCODE (see $LogPath)" }
    $manifest.target = $target
    $manifest.status = 'success'
    $manifest.exeSha256 = (Get-FileHash king.exe).Hash
    $manifest.resource = Get-Content king.res.json -Raw | ConvertFrom-Json
    $manifest.finished = Get-Date -Format o
    $manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8
    "Built $buildDir\king.exe; SHA256=$($manifest.exeSha256)"
} catch {
    if ($manifest) {
        $manifest.status='error'; $manifest.error=$_.Exception.Message
        $manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8
    }
    throw
} finally { Pop-Location }
