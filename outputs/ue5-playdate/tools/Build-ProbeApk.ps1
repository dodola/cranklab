param(
    [ValidateSet('arm64-v8a','x86_64')][string]$Abi='x86_64',
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21',
    [string]$FixtureDirectory,
    [string]$NativeFixturesDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/android-native-probe'))
if(-not $FixtureDirectory) {
    $FixtureDirectory=[IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/runtime-fixture.pdx'))
    & python (Join-Path $PSScriptRoot 'make-fixture.py') --output $FixtureDirectory
    if($LASTEXITCODE) { throw 'Test fixture generation failed' }
}
$env:JAVA_HOME=$JdkDirectory
& (Join-Path $PSScriptRoot 'Build-AndroidRuntime.ps1') -Abi $Abi -NdkDirectory "$SdkDirectory/ndk/27.2.12479018"
$nativeRoot=Join-Path $projectRoot "bin/Android/$Abi"
$classes=Join-Path $buildRoot 'classes'; $dex=Join-Path $buildRoot 'dex'
New-Item -ItemType Directory -Force $buildRoot,$classes,$dex | Out-Null
$target=if($Abi -eq 'arm64-v8a') {'aarch64-linux-android26'} else {'x86_64-linux-android26'}
& "$SdkDirectory/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" "--target=$target" -shared -fPIC -O2 "-I$projectRoot/runtime/include" (Join-Path $PSScriptRoot 'android-probe-jni.c') "-L$nativeRoot" '-l:libcrank-core.so' '-Wl,-soname,libcrank-probe-jni.so' '-Wl,-z,max-page-size=16384' '-Wl,--no-undefined' -o (Join-Path $buildRoot 'libcrank-probe-jni.so')
if($LASTEXITCODE) { throw 'JNI test adapter compilation failed' }
$androidJar="$SdkDirectory/platforms/android-36/android.jar"
& "$JdkDirectory/bin/javac.exe" --release 8 -Xlint:-options -classpath $androidJar -d $classes (Join-Path $PSScriptRoot 'android-probe/Probe.java') (Join-Path $projectRoot 'Unreal/Plugins/CrankRuntime/Source/CrankRuntime/Android/Java/com/cranklab/CrankHttp.java')
if($LASTEXITCODE) { throw 'Java test adapter compilation failed' }
& "$JdkDirectory/bin/jar.exe" --create --file "$buildRoot/classes.jar" -C $classes .
if($LASTEXITCODE) { throw 'Class archive creation failed' }
$buildTools="$SdkDirectory/build-tools/36.0.0"
& "$buildTools/d8.bat" --lib $androidJar --min-api 26 --output $dex "$buildRoot/classes.jar"
if($LASTEXITCODE) { throw 'Dex compilation failed' }
& "$buildTools/aapt2.exe" link --manifest (Join-Path $PSScriptRoot 'android-probe/AndroidManifest.xml') -I $androidJar -o "$buildRoot/raw.apk"
if($LASTEXITCODE) { throw 'Test package manifest compilation failed' }
@'
from pathlib import Path
import json, sys, zipfile
build,project,abi,fixture=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3],Path(sys.argv[4])
with zipfile.ZipFile(build/'raw.apk','a') as apk:
    apk.write(build/'dex/classes.dex','classes.dex')
    apk.write(build/'libcrank-probe-jni.so',f'lib/{abi}/libcrank-probe-jni.so')
    apk.write(project/f'bin/Android/{abi}/libcrank-core.so',f'lib/{abi}/libcrank-core.so')
    games={'fixture':fixture}
    inventory=json.loads((project/'game-inventory.json').read_text(encoding='utf-8-sig'))
    for key,item in zip(('music-box','mahjong','pdgames'),inventory['games']):
        games[key]=Path(item['root'])
    if len(sys.argv)>5 and sys.argv[5]:
        native=Path(sys.argv[5])
        for key,name in {'native-hello':'Hello-World','native-life':'Life','native-sprites':'Sprite-Game','native-abi':'Core-ABI','native-mixed':'Mixed-ABI','native-audio':'Audio-C'}.items():
            games[key]=native/name/(name+'.pdx')
    for key,root in games.items():
        for file in root.rglob('*'):
            if file.is_file(): apk.write(file,f'assets/games/{key}/{file.relative_to(root).as_posix()}',compress_type=zipfile.ZIP_DEFLATED)
'@ | python - $buildRoot $projectRoot $Abi $FixtureDirectory $NativeFixturesDirectory
if($LASTEXITCODE) { throw 'Test package asset assembly failed' }
& "$buildTools/zipalign.exe" -P 16 -f 4 "$buildRoot/raw.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) { throw 'Package alignment failed' }
$key=Join-Path $buildRoot 'debug.keystore'
if(-not (Test-Path -LiteralPath $key)) {
    & "$JdkDirectory/bin/keytool.exe" -genkeypair -keystore $key -storepass android -keypass android -alias androiddebugkey -dname 'CN=CrankLab local tests' -keyalg RSA -keysize 2048 -validity 3650
    if($LASTEXITCODE) { throw 'Local test signing key creation failed' }
}
& "$buildTools/apksigner.bat" sign --ks $key --ks-pass pass:android --out "$buildRoot/crank-native-probe.apk" "$buildRoot/aligned.apk"
if($LASTEXITCODE) { throw 'Test package signing failed' }
Write-Output "$buildRoot/crank-native-probe.apk"
