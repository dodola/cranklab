param([string]$OutputDirectory,[string]$LibraryRoot,[switch]$Immersive,[int]$Width=1280,[int]$Height=900)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory) {$OutputDirectory=Join-Path $root 'verification-launcher/ue-flow'}
if(!$LibraryRoot) {$LibraryRoot=Join-Path $root '../../work/launcher-flow-library'}
$detail=Get-Content -LiteralPath (Join-Path $root '../../work/launcher-dino-detail.json') -Raw | ConvertFrom-Json
$detail.price=$detail.price.current
$gameJson=$detail | ConvertTo-Json -Depth 30 -Compress
$literal=$gameJson | ConvertTo-Json -Compress
$previous=$env:PD_EVAL
try {
    $env:PD_EVAL="if screenManager then local fixture=json.decode($literal);local old=playdate.update;local once=true;playdate.update=function(...) local result=old(...);if once and screenManager.getCurrentName()=='gameList' and not screenManager.isTransitioning() then once=false;screenManager.switch('gameDetail',{game=fixture},'left') end;return result end end"
    & "$PSScriptRoot/Run-SceneProbe.ps1" -LauncherFlowProbe -OutputDirectory $OutputDirectory -LibraryRoot $LibraryRoot -Immersive:$Immersive -Width $Width -Height $Height
} finally { $env:PD_EVAL=$previous }
