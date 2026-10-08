param(
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/android-haptics-probe'))
$classes=Join-Path $buildRoot 'classes'; $dex=Join-Path $buildRoot 'dex'
New-Item -ItemType Directory -Force -Path $buildRoot,$classes,$dex | Out-Null
$env:JAVA_HOME=$JdkDirectory
$androidJar="$SdkDirectory/platforms/android-36/android.jar"
$buildTools="$SdkDirectory/build-tools/36.1.0"
$sources=@((Join-Path $PSScriptRoot 'android-haptics/HapticsActivity.java'),(Join-Path $PSScriptRoot 'android-haptics/HapticsProbe.java'),
    (Join-Path $projectRoot 'Unreal/Plugins/CrankRuntime/Source/CrankRuntime/Android/Java/com/cranklab/CrankHaptics.java'))
& "$JdkDirectory/bin/javac.exe" --release 8 -Xlint:-options -classpath $androidJar -d $classes @sources
if($LASTEXITCODE) {throw 'Haptic probe Java compilation failed'}
& "$JdkDirectory/bin/jar.exe" --create --file "$buildRoot/classes.jar" -C $classes .
if($LASTEXITCODE) {throw 'Haptic probe class assembly failed'}
& "$buildTools/d8.bat" --lib $androidJar --min-api 26 --output $dex "$buildRoot/classes.jar"
if($LASTEXITCODE) {throw 'Haptic probe dex compilation failed'}
& "$buildTools/aapt2.exe" link --manifest (Join-Path $PSScriptRoot 'android-haptics/AndroidManifest.xml') -I $androidJar -o "$buildRoot/raw.apk"
if($LASTEXITCODE) {throw 'Haptic probe manifest compilation failed'}
@'
from pathlib import Path
import sys, zipfile
root=Path(sys.argv[1])
with zipfile.ZipFile(root/'raw.apk','a') as apk: apk.write(root/'dex/classes.dex','classes.dex')
'@ | python - $buildRoot
if($LASTEXITCODE) {throw 'Haptic probe APK assembly failed'}
& "$buildTools/zipalign.exe" -f 4 "$buildRoot/raw.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) {throw 'Haptic probe alignment failed'}
$key=Join-Path $buildRoot 'debug.keystore'
if(!(Test-Path -LiteralPath $key)) {
    & "$JdkDirectory/bin/keytool.exe" -genkeypair -keystore $key -storepass android -keypass android -alias androiddebugkey -dname 'CN=Playdate haptic tests' -keyalg RSA -keysize 2048 -validity 3650
    if($LASTEXITCODE) {throw 'Haptic probe key generation failed'}
}
& "$buildTools/apksigner.bat" sign --ks $key --ks-pass pass:android --out "$buildRoot/playdate-haptics-probe.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) {throw 'Haptic probe signing failed'}
Write-Output "$buildRoot/playdate-haptics-probe.apk"
