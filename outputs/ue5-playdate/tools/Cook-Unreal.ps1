param(
    [ValidateSet('arm64','x64')][string]$Architecture='arm64',
    [ValidateSet('ETC2','ASTC')][string]$TextureFormat='ETC2',
    [string]$EngineDirectory='C:/Program Files/Epic Games/UE_5.8',
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21',
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipCook
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$project=Join-Path $root 'Unreal/CrankLab.uproject'
if(!$OutputDirectory) { $OutputDirectory=Join-Path $root "android/$Architecture" }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$env:ANDROID_HOME=$SdkDirectory; $env:ANDROID_SDK_ROOT=$SdkDirectory
$env:NDKROOT="$SdkDirectory/ndk/27.2.12479018"; $env:JAVA_HOME=$JdkDirectory
& "$PSScriptRoot/Remove-ObsoleteTranslationStage.ps1"
& python "$PSScriptRoot/manifest-fixture.py"
if($LASTEXITCODE) { throw 'Fixture manifest creation failed' }
$arguments=@('BuildCookRun',"-project=$project",'-noP4','-unattended',
    '-platform=Android',"-clientarchitecture=$Architecture",'-clientconfig=Development',
    "-cookflavor=$TextureFormat",'-map=/Game/Maps/Device','-stage','-pak','-iostore',
    '-package','-archive',"-archivedirectory=$OutputDirectory",'-utf8output')
$arguments+=if($SkipBuild) {'-skipbuild'} else {'-build'}
$arguments+=if($SkipCook) {'-skipcook'} else {'-cook'}
& "$EngineDirectory/Engine/Build/BatchFiles/RunUAT.bat" @arguments
if($LASTEXITCODE) { throw 'Cooked Android package generation failed; inspect the UAT log' }
$apk=Get-ChildItem -LiteralPath $OutputDirectory -Filter 'CrankLab*.apk' -File -Recurse |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if(!$apk) { throw 'UAT completed without a CrankLab APK in the archive directory' }
Write-Output $apk.FullName
