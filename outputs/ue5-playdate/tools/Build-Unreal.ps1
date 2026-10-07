param(
    [ValidateSet('Win64','Android')][string]$Platform='Win64',
    [ValidateSet('arm64','x64')][string]$Architecture,
    [string]$EngineDirectory='C:/Program Files/Epic Games/UE_5.8',
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$project=Join-Path $projectRoot 'Unreal/CrankLab.uproject'
$target=if($Platform -eq 'Win64') {'CrankLabEditor'} else {'CrankLab'}
if($Platform -eq 'Android') {
    $env:ANDROID_HOME=$SdkDirectory; $env:ANDROID_SDK_ROOT=$SdkDirectory
    $env:NDKROOT="$SdkDirectory/ndk/27.2.12479018"; $env:JAVA_HOME=$JdkDirectory
}
$arguments=@($target,$Platform,'Development',"-Project=$project",'-WaitMutex','-NoHotReload')
if($Platform -eq 'Android' -and $Architecture) { $arguments+="-Architecture=$Architecture" }
& "$EngineDirectory/Engine/Build/BatchFiles/Build.bat" @arguments
if($LASTEXITCODE) { throw 'Unreal compilation/packaging failed' }
