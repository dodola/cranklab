param(
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/android-touch'))
$classes=Join-Path $buildRoot 'classes'; $dex=Join-Path $buildRoot 'dex'
New-Item -ItemType Directory -Force $classes,$dex | Out-Null
$androidJar="$SdkDirectory/platforms/android-37.0/android.jar"
& "$JdkDirectory/bin/javac.exe" --release 8 -Xlint:-options -classpath $androidJar -d $classes "$PSScriptRoot/android-touch/TouchStream.java"
if($LASTEXITCODE) { throw 'Touch harness Java compilation failed' }
& "$JdkDirectory/bin/jar.exe" --create --file "$buildRoot/classes.jar" -C $classes .
if($LASTEXITCODE) { throw 'Touch harness archive creation failed' }
$env:JAVA_HOME=$JdkDirectory
& "$SdkDirectory/build-tools/36.0.0/d8.bat" --lib $androidJar --min-api 26 --output $dex "$buildRoot/classes.jar"
if($LASTEXITCODE) { throw 'Touch harness dex compilation failed' }
Write-Output "$dex/classes.dex"
