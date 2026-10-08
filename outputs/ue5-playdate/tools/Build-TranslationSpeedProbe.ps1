# Build only. After authorizing device tests, install beside the debug Playdate
# app and run: adb shell am instrument -w -r -e profile stq1 -e repeats 2
# com.cranklab.speedprobe/com.cranklab.SpeedProbe
# Add -e sourceId <cached-game-id> -e repeats 1 to measure a real long description.
# Uninstall only com.cranklab.speedprobe when finished; retain the main app/data.
param(
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21',
    [string]$SigningKeystore='C:/Users/dodol/.android/debug.keystore',
    [string]$VariantDirectory,
    [string]$BuildDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory)}else{[IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/android-translation-performance'))}
$source=Join-Path $PSScriptRoot 'android-translation-speed'
$classes=Join-Path $buildRoot 'classes';$dex=Join-Path $buildRoot 'dex'
New-Item -ItemType Directory -Force -Path $buildRoot,$classes,$dex | Out-Null
$env:JAVA_HOME=$JdkDirectory
$androidJar="$SdkDirectory/platforms/android-36/android.jar"
$buildTools="$SdkDirectory/build-tools/36.1.0"
$ndk="$SdkDirectory/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64"
& "$ndk/bin/clang++.exe" --target=aarch64-none-linux-android26 "--sysroot=$ndk/sysroot" -O3 -DNDEBUG -std=c++17 -fPIC -static-libstdc++ -shared '-Wl,-z,max-page-size=16384' '-Wl,-soname,libspeedprobe.so' (Join-Path $source 'speedprobe.cpp') -ldl -llog -o "$buildRoot/libspeedprobe.so"
if($LASTEXITCODE) {throw 'Translation performance JNI compilation failed'}
& "$JdkDirectory/bin/javac.exe" --release 8 -Xlint:-options -classpath $androidJar -d $classes (Join-Path $source 'SpeedProbe.java')
if($LASTEXITCODE) {throw 'Translation performance Java compilation failed'}
& "$JdkDirectory/bin/jar.exe" --create --file "$buildRoot/classes.jar" -C $classes .
if($LASTEXITCODE) {throw 'Translation performance class assembly failed'}
& "$buildTools/d8.bat" --lib $androidJar --min-api 26 --output $dex "$buildRoot/classes.jar"
if($LASTEXITCODE) {throw 'Translation performance dex compilation failed'}
& "$buildTools/aapt2.exe" link --manifest (Join-Path $source 'AndroidManifest.xml') -I $androidJar -o "$buildRoot/raw.apk"
if($LASTEXITCODE) {throw 'Translation performance manifest compilation failed'}
# Write only the owned probe archive. Model files remain in the installed app.
@'
from pathlib import Path
import sys,zipfile
root=Path(sys.argv[1]);variants=Path(sys.argv[2]) if len(sys.argv)>2 and sys.argv[2] else None
with zipfile.ZipFile(root/'raw.apk','a') as apk:
    apk.write(root/'dex/classes.dex','classes.dex',compress_type=zipfile.ZIP_DEFLATED)
    apk.write(root/'libspeedprobe.so','lib/arm64-v8a/libspeedprobe.so',compress_type=zipfile.ZIP_DEFLATED)
    if variants:
        for file in variants.glob('*.so'):apk.write(file,'lib/arm64-v8a/'+file.name,compress_type=zipfile.ZIP_DEFLATED)
'@ | python - $buildRoot $VariantDirectory
if($LASTEXITCODE) {throw 'Translation performance APK assembly failed'}
& "$buildTools/zipalign.exe" -f 4 "$buildRoot/raw.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) {throw 'Translation performance APK alignment failed'}
# Instrumenting Playdate requires its signing certificate, not a new test key.
& "$buildTools/apksigner.bat" sign --ks $SigningKeystore --ks-pass pass:android --out "$buildRoot/playdate-translation-speed-probe.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) {throw 'Translation performance APK signing failed'}
Write-Output "$buildRoot/playdate-translation-speed-probe.apk"
