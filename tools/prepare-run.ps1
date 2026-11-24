[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDir,
    [Parameter(Mandatory)][string]$Destination,
    [string]$RebuiltExe,
    [switch]$SkipIntro
)
$ErrorActionPreference = 'Stop'
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$Destination = [IO.Path]::GetFullPath($Destination)
if ($Destination.StartsWith($GameDir.TrimEnd('\') + '\', 'OrdinalIgnoreCase')) { throw 'Destination must be outside the source game directory.' }
if (Test-Path -LiteralPath $Destination) { throw "Destination must be new; refusing to overwrite $Destination" }
if (!(Test-Path -LiteralPath "$GameDir\king.exe")) { throw 'GameDir must contain king.exe' }
if ($RebuiltExe) { $RebuiltExe = (Resolve-Path -LiteralPath $RebuiltExe).Path }
New-Item -ItemType Directory -Path $Destination | Out-Null
# Copies, never hardlinks: saves/configuration in the installation stay untouched.
& robocopy $GameDir $Destination /E /COPY:DAT /DCOPY:DAT /XJ /XD screenshots plugins 'Mod Template' sessions saves-before saves-after dumps decoded /XF *.pl1 *.pl2 *.sav *.log *.dmp *.asi telemetry.bin fatal.bin exit.json run-inputs.json steam_autocloud.vdf 'Workshop*' HT2GraphicsConfig.exe /R:0 /W:0 /NFL /NDL /NP "/LOG:$Destination\copy.log"
if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }
if ($RebuiltExe) { Copy-Item -LiteralPath $RebuiltExe -Destination "$Destination\king.exe" }
if ($SkipIntro) {
    # Only the fresh test copy: preserve the video for easy restoration.
    $intro = Join-Path $Destination 'Kotr.avi'
    if (!(Test-Path -LiteralPath $intro)) { throw 'SkipIntro requires Kotr.avi in the copied game.' }
    Rename-Item -LiteralPath $intro -NewName 'Kotr.avi.disabled'
}
$inputs = Get-ChildItem -LiteralPath $Destination -File | Where-Object { $_.Extension -in '.exe','.dll','.ini' } | ForEach-Object {
    [pscustomobject]@{ name=$_.Name; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
}
[ordered]@{ created=(Get-Date -Format o); source=$GameDir; rebuiltExe=$RebuiltExe; workingDirectory=$Destination; arguments=@(); sourceExeSha256=(Get-FileHash -LiteralPath "$GameDir\king.exe").Hash; profile='Copied installation settings, fresh save profile'; skipIntro=[bool]$SkipIntro; inputs=$inputs } | ConvertTo-Json -Depth 4 | Set-Content "$Destination\run-inputs.json" -Encoding UTF8
"Prepared $Destination; launch king.exe with this working directory."
