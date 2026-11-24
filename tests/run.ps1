# Sequential coordinator: runners share production OBJ files. Never parallelize.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$GameExe, [string]$Python='python', [string]$Destination, [string]$RespawnProfile, [switch]$CampaignClock)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path "$PSScriptRoot/..").Path
$GameExe=(Resolve-Path -LiteralPath $GameExe).Path
if (!$Destination) { $Destination=Join-Path $repo ('.local/regressions/suite-'+(Get-Date -Format yyyyMMdd-HHmmss-fff)) }
if (Test-Path -LiteralPath $Destination) { throw 'Use a new destination' }
New-Item -ItemType Directory -Path $Destination | Out-Null
$out=(Resolve-Path -LiteralPath $Destination).Path
$results=[Collections.Generic.List[object]]::new()
$report=[ordered]@{started=(Get-Date).ToString('o'); status='running'; commit=(& git -C $repo rev-parse HEAD); dirty=@(& git -C $repo status --short); powershell=$PSVersionTable.PSVersion.ToString(); python=(& $Python --version); original=$GameExe; originalSha256=(Get-FileHash -LiteralPath $GameExe).Hash; cases=$results}
function Save-Report { $report | ConvertTo-Json -Depth 10 | Set-Content "$out/result.json" -Encoding utf8 }
function Run-Case([string]$Name,[string]$Script,[string[]]$Extra=@()) {
    $argsList=@('-NoProfile','-ExecutionPolicy','Bypass','-File',"$PSScriptRoot/$Script")+$Extra
    $start=Get-Date
    Write-Host "RUN $Name"
    & pwsh @argsList *> "$out/$Name.log"
    $code=$LASTEXITCODE
    $state=if(!$code){'passed'}elseif($Name -eq 'x87-pe' -and $code -eq 2){'blocked'}else{'failed'}
    $results.Add([ordered]@{name=$Name; status=$state; exitCode=$code; seconds=((Get-Date)-$start).TotalSeconds; arguments=$argsList; log="$Name.log"})
    Save-Report
    Write-Host "END $Name exit=$code"
}
Push-Location $repo
try {
    Save-Report
    Run-Case sound '../tools/build-windows.ps1' @('-GameExe',$GameExe,'-Python',$Python,'-Check')
    # Run cases sequentially because they reuse the production objects.
    foreach($name in 'poly_destroy','k50_frame') {
        Run-Case $name "$($name)_test/run.ps1" @('-GameExe',$GameExe,'-Python',$Python)
        if($name -eq 'k50_frame') { Run-Case "$name-negative" "$($name)_test/run.ps1" @('-GameExe',$GameExe,'-Python',$Python,'-NegativeControl') }
    }
    Run-Case gzip 'gzip_test/run-current.ps1' @('-GameExe',$GameExe,'-Python',$Python)
    foreach($name in 'body_contact','place_objects','vehicle_destruct','hire') {
        Run-Case $name "$($name)_test/run.ps1" @('-Destination',"$out/$name",'-Python',$Python)
        if($name -eq 'hire') { Run-Case "$name-negative" "$($name)_test/run.ps1" @('-Destination',"$out/$name-negative",'-Python',$Python,'-NegativeControl') }
    }
    foreach($name in 'render_room','rle_palette','video_palette','menu_palette','route_position','terrain_abi','grid_abi','solo_audit','ui_contract','network_address','network_connect','matherr') {
        Run-Case $name "$($name)_test/run.ps1" @('-GameExe',$GameExe,'-Python',$Python)
        $negative=@('')
        if($name -eq 'terrain_abi') { $negative=@('caller','wrapper','plant','height','arch') }
        if($name -eq 'grid_abi') { $negative=@('direct','join','join-caller','marine') }
        if($name -eq 'solo_audit') { $negative=@('speed','dust','tyre','contact-axes','licence-recipient','light-lifetime','light-distance','wheel-plane','wheel-support','body-planes') }
        if($name -eq 'ui_contract') { $negative=@('dates','map','hints') }
        if($name -eq 'network_address') { $negative=@('enum','size','write','all') }
        foreach($variant in $negative) {
            $arguments=@('-GameExe',$GameExe,'-Python',$Python,'-NegativeControl')
            if($variant) { $arguments+=$variant }
            Run-Case "$name-negative$variant" "$($name)_test/run.ps1" $arguments
        }
    }
    Run-Case x87-pe 'x87_test/run-pe.ps1' @('-GameExe',$GameExe,'-Python',$Python)
    Run-Case state 'state/run.ps1' @('-GameExe',$GameExe,'-Python',$Python)
    Run-Case call100 'audit/call100/run.ps1' @('-GameExe',$GameExe,'-Python',$Python)
    $assets = Join-Path (Split-Path $GameExe) 'ENV/mod_ad.sch'
    if (Test-Path -LiteralPath $assets) {
        $worldArgs = @('-GameExe',$GameExe)
        if ($RespawnProfile) { $worldArgs += @('-RespawnProfile',(Resolve-Path -LiteralPath $RespawnProfile).Path) }
        if ($CampaignClock) { $worldArgs += '-CampaignClock' }
        Run-Case world-event 'world_event_test/run.ps1' $worldArgs
    } else {
        $results.Add([ordered]@{name='world-event';status='skipped';reason='Missing original game assets next to -GameExe'})
    }
    if (!$RespawnProfile) { $results.Add([ordered]@{name='respawn';status='skipped';reason='Private -RespawnProfile not supplied'}) }
    if (!$CampaignClock) { $results.Add([ordered]@{name='campaign-clock';status='skipped';reason='Extended simulation requires -CampaignClock'}) }
    $report.status=if(@($results | Where-Object {$_.status -eq 'failed'}).Count){'failed'}elseif(@($results | Where-Object {$_.status -eq 'blocked'}).Count){'blocked'}else{'passed'}
} catch { $report.status='failed'; $report.error=$_.Exception.Message; throw }
finally { $report.finished=(Get-Date).ToString('o'); Save-Report; Pop-Location; Write-Host "Report: $out/result.json ($($report.status))" }
if($report.status -eq 'failed'){exit 1}
if($report.status -eq 'blocked'){exit 2}
