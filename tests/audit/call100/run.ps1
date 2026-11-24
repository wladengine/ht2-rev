[CmdletBinding()]
param([Parameter(Mandatory)][string]$GameExe, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
$game = (Resolve-Path -LiteralPath $GameExe).Path
$out = (New-Item -ItemType Directory -Path "$repo/.local/regressions/call100-$([guid]::NewGuid().ToString('N').Substring(0,8))").FullName
. "$repo/tools/windows-toolchain.ps1"
& "$repo/tools/build-windows.ps1" -GameExe $game -Python $Python -LogPath "$out/build.log" *> "$out/console.txt"
Push-Location "$repo/.local/build"
try {
    $tc = Resolve-WindowsToolchain -Python $Python
    $makeArgs = $tc.MakeArgs
    $cmds = & $tc.NMake /nologo /f ..\..\Makefile /n /A @makeArgs king.exe
    $link = @($cmds | Where-Object { $_ -match 'link.exe"' })
    if ($LASTEXITCODE -or $link.Count -ne 1) { throw "Cannot identify the production link command" }
    # Preserve ordinary objects, entry point, imports, numerical flags and resources.
    # Keep definitions which the ordinary linker folds together or discards.
    $cmd = $link[0].Trim().Replace('/OUT:king.exe', "/OPT:NOREF /OPT:NOICF /OUT:`"$out/audit.exe`"").Replace('/MAP:king.map', "/MAP:`"$out/audit.map`"")
    $tokens = @([regex]::Matches($cmd, '(?:[^\s"]|"[^"]*")+') | ForEach-Object { $_.Value.Replace('"','') })
    $linkArgs = $tokens[1..($tokens.Count-1)]
    & $tokens[0] @linkArgs > "$out/link.log"
    if ($LASTEXITCODE) { throw "Audit link failed in $out" }
    $common = @('--original',$game,'--candidate',"$out/audit.exe",'--map',"$out/audit.map")
    & $Python "$PSScriptRoot/compare.py" @common --out "$out/run.json" > "$out/comparison.txt"
    if ($LASTEXITCODE) { throw "Comparison failed in $out" }
    & $Python "$PSScriptRoot/compare.py" @common --negative-control --out "$out/control.json" > "$out/control.txt"
    if ($LASTEXITCODE) { throw "Negative control failed in $out" }
    $contracts = (Resolve-Path "$PSScriptRoot/../contracts").Path
    foreach ($fixture in Get-ChildItem -LiteralPath $contracts -Filter '*.json') {
        & $Python "$contracts/declarative.py" @common --fixture $fixture.FullName --out "$out/$($fixture.Name)"
        if ($LASTEXITCODE) { throw "State contract failed for $($fixture.Name)" }
    }
    foreach ($mode in 'direct','control') {
        $extra = @()
        if ($mode -eq 'control') { $extra += '--control' }
        & $Python "$contracts/contracts.py" @common @extra --out "$out/contracts-$mode.json" > "$out/contracts-$mode.log"
        if ($LASTEXITCODE) { throw "Calling-contract comparison failed ($mode)" }
    }
    foreach ($group in 'direct','three-sites','physics-effects') {
        $extra = @()
        if ($group -ne 'direct') { $extra += "--$group" }
        & $Python "$contracts/call_frontier.py" @common @extra --out "$out/$group.json" > "$out/$group.log"
        if ($LASTEXITCODE) { throw "Direct-call comparison failed ($group)" }
    }
    & $Python "$contracts/scalars.py" @common --targets "$contracts/scalar-targets.tsv" --out "$out/scalars.tsv"
    if ($LASTEXITCODE) { throw "Scalar comparison failed" }
    $rows = @(Import-Csv -LiteralPath "$out/scalars.tsv" -Delimiter ([char]9))
    if (!$rows.Count -or @($rows | Where-Object { $_.stock_expected -ne '+++' -or $_.canonical_expected -ne '+++' }).Count) {
        throw "Scalar fixture mismatch, see $out/scalars.tsv"
    }
    "PASS $out"
} finally { Pop-Location }
