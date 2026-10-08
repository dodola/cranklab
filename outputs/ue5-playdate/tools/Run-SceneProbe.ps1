param(
    [string]$EngineDirectory='C:/Program Files/Epic Games/UE_5.8',
    [string]$OutputDirectory,
    [string]$GameDirectory,
    [int]$Width=1280,
    [int]$Height=900,
    [Nullable[float]]$VisualCrankAngle,
    [switch]$UIProbe,
    [switch]$Enlarged,
    [switch]$Immersive,
    [switch]$ImmersiveInput,
    [switch]$SettingsProbe,
    [switch]$TranslationSettings,
    [switch]$TranslationProbe,
    [switch]$TranslationLauncherEntry,
    [switch]$TranslationOriginal,
    [string]$TranslationDetailBase,
    [switch]$StageTwoProbe,
    [ValidateSet('None','Menu','Home','Lock','Flow')][string]$SystemProbe='None',
    [switch]$MobilePreview,
    [switch]$LauncherProbe,
    [switch]$LauncherFlowProbe,
    [string]$LibraryRoot
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory) { $OutputDirectory=Join-Path $root 'verification-scene/landscape' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
foreach($Evidence in @('device.png','results.json')) {Remove-Item -LiteralPath (Join-Path $OutputDirectory $Evidence) -ErrorAction SilentlyContinue}
$project=Join-Path $root 'Unreal/CrankLab.uproject'
$arguments=@(('"'+$project+'"'),'-game','-RenderOffscreen','-NoSplash','-unattended','-nosound','-windowed','-ForceRes',
    "-ResX=$Width","-ResY=$Height",('-ExecCmds="r.SetRes '+$Width+'x'+$Height+'w"'),
    ('-CrankSceneProbe="'+$OutputDirectory+'"'),'-stdout','-FullStdOutLogOutput')
if($GameDirectory) { $arguments+=('-CrankGame="'+[IO.Path]::GetFullPath($GameDirectory)+'"') }
if($UIProbe) { $arguments+='-CrankUIProbe' }
if($Enlarged) { $arguments+='-CrankEnlarged' }
if($Immersive -or $ImmersiveInput) { $arguments+='-CrankImmersive' }
if($ImmersiveInput) { $arguments+='-CrankImmersiveProbe' }
if($SettingsProbe) { $arguments+='-CrankSettingsProbe' }
if($TranslationSettings) { $arguments+='-CrankSettingsProbe'; $arguments+='-CrankTranslationSettingsProbe' }
if($TranslationProbe) { $arguments+='-CrankTranslationProbe' }
if($TranslationLauncherEntry) { $arguments+='-CrankTranslationLauncherEntry' }
if($TranslationOriginal) { $arguments+='-CrankTranslationOriginalProbe' }
if($TranslationDetailBase) { $arguments+=('-CrankTranslationDetailBase='+$TranslationDetailBase) }
if($StageTwoProbe) { $arguments+='-CrankStageTwoProbe' }
if($SystemProbe -eq 'Menu') { $arguments+='-CrankSystemMenuProbe' }
if($SystemProbe -eq 'Home') { $arguments+='-CrankHomeProbe' }
if($SystemProbe -eq 'Lock') { $arguments+='-CrankLockProbe' }
if($SystemProbe -eq 'Flow') { $arguments+='-CrankSystemFlowProbe' }
if($MobilePreview) { $arguments+='-FeatureLevelES31' }
if($LauncherProbe) { $arguments+='-CrankLauncherProbe' }
if($LauncherFlowProbe) { $arguments+='-CrankLauncherProbe'; $arguments+='-CrankLauncherFlowProbe' }
if($LibraryRoot) { $arguments+=('-CrankLibraryRoot="'+[IO.Path]::GetFullPath($LibraryRoot)+'"') }
if($null -ne $VisualCrankAngle) { $arguments+="-CrankViewAngle=$VisualCrankAngle" }
$startedUtc=[DateTime]::UtcNow
$process=Start-Process -FilePath "$EngineDirectory/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $OutputDirectory 'engine.log') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.log')
$deadline=[DateTime]::UtcNow.AddMinutes(4)
while(!$process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $process.Refresh()
}
if(!$process.HasExited) {
    # Only terminate the exact development process created above.
    Stop-Process -Id $process.Id
    throw 'Scene verification timed out; inspect engine.log'
}
if($process.ExitCode) { throw "Scene verification failed ($($process.ExitCode))" }
foreach($evidenceName in @('device.png','results.json')) {
    if((Get-Item -LiteralPath (Join-Path $OutputDirectory $evidenceName)).LastWriteTimeUtc -lt $startedUtc) { throw "Stale evidence: $evidenceName" }
}
$result=Get-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Raw | ConvertFrom-Json
if(!$result.passed) { throw 'Scene input/frame assertions failed' }
if($TranslationProbe -and !$result.translationPassed) {throw 'Translation scene verification failed'}
if($ImmersiveInput -and !$result.immersiveInputPassed) { throw 'Immersive multi-pointer/session verification failed' }
if($ImmersiveInput -and (!$result.immersiveTouchProbeRan -or !$result.immersiveTouchPassed)) { throw 'Immersive Slate dial/screen swipe touch verification failed' }
$log=Get-Content -LiteralPath (Join-Path $OutputDirectory 'engine.log') -Raw
$materialFailures=@($log -split "`n" | Where-Object { $_ -match 'Failed to compile Material' })
if($MobilePreview) {
    # Unused editor-only GBuffer visualization materials cannot compile for
    # forward mobile rendering. Every scene-material failure remains fatal.
    $materialFailures=@($materialFailures | Where-Object { $_ -notmatch '[\\/]Engine[\\/]Content[\\/]BufferVisualization[\\/]' })
}
if($materialFailures.Count -gt 0 -or $log -match 'Fatal error:') { throw 'Scene rendering failed' }
$result | ConvertTo-Json
