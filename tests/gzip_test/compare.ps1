param([Parameter(Mandatory)][string]$OldFile, [Parameter(Mandatory)][string]$NewFile)
$ErrorActionPreference='Stop'
$a=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $OldFile).Path)
$b=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $NewFile).Path)
if (!$a.Length -or !$b.Length) { throw 'Empty gzip comparison output' }
if ($a.Length -ne $b.Length) { throw "DIFFERENT lengths: $($a.Length) vs $($b.Length)" }
for ($i=0; $i -lt $a.Length; $i++) {
    if ($a[$i] -ne $b[$i]) { throw "DIFFERENT at byte $i" }
}
"identical packed bytes: $($a.Length) (baseline provenance must be assessed separately)"
