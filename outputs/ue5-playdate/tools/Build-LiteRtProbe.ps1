param(
    [string]$CacheDirectory='D:/PlaydateAI/litert-experiment',
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21',
    [string]$SigningKeystore='C:/Users/dodol/.android/debug.keystore'
)
$ErrorActionPreference='Stop'
$cache=[IO.Path]::GetFullPath($CacheDirectory)
if([IO.Path]::GetPathRoot($cache) -eq 'C:\') {throw 'Use D drive for experimental downloads and build caches'}
$build=Join-Path $cache 'build';$classes=Join-Path $build 'classes';$dex=Join-Path $build 'dex'
New-Item -ItemType Directory -Force -Path $build,$classes,$dex | Out-Null
$source=Join-Path $PSScriptRoot 'android-litert-probe'
$env:JAVA_HOME=$JdkDirectory
$androidJar="$SdkDirectory/platforms/android-36/android.jar";$buildTools="$SdkDirectory/build-tools/36.1.0"
@'
import hashlib,json,sys,zipfile
from pathlib import Path
cache,source=map(Path,sys.argv[1:]);lock=json.loads((source/'dependencies.json').read_text(encoding='utf-8'))
for item in lock:
    file=cache/'deps'/item['filename']
    if file.stat().st_size!=item['bytes'] or hashlib.file_digest(file.open('rb'),'sha256').hexdigest()!=item['sha256']:raise RuntimeError('Dependency checksum mismatch: '+file.name)
with zipfile.ZipFile(cache/'deps/litertlm-android-0.18.0.aar') as archive:archive.extractall(cache/'aar')
'@ | python - $cache $source
if($LASTEXITCODE) {throw 'Dependency verification failed'}
$jars=@("$cache/aar/classes.jar")+@(Get-ChildItem -LiteralPath "$cache/deps" -Filter '*.jar' | ForEach-Object {$_.FullName})
& "$JdkDirectory/bin/javac.exe" --release 8 -encoding UTF-8 -Xlint:-options -classpath (($jars+@($androidJar))-join ';') -d $classes (Join-Path $source 'ProbeActivity.java') (Join-Path $source 'LiteRtProbe.java')
if($LASTEXITCODE) {throw 'LiteRT probe Java compilation failed'}
& "$JdkDirectory/bin/jar.exe" --create --file "$build/classes.jar" -C $classes .
if($LASTEXITCODE) {throw 'Probe class assembly failed'}
& "$JdkDirectory/bin/java.exe" -cp "$cache/deps/tools/r8-9.5.23.jar" com.android.tools.r8.D8 --lib $androidJar --min-api 26 --output $dex "$build/classes.jar" @jars *> "$build/d8.log"
if($LASTEXITCODE) {throw 'Probe dex compilation failed'}
& "$buildTools/aapt2.exe" link --manifest (Join-Path $source 'AndroidManifest.xml') -I $androidJar -o "$build/raw.apk"
if($LASTEXITCODE) {throw 'Probe manifest compilation failed'}
@'
from pathlib import Path
import sys,zipfile
cache=Path(sys.argv[1]);source=Path(sys.argv[2]);root=cache/'build'
with zipfile.ZipFile(root/'raw.apk','a') as archive:
    archive.write(source/'qwen3-template.jinja','assets/qwen3-template.jinja',compress_type=zipfile.ZIP_DEFLATED)
    for file in (root/'dex').glob('*.dex'):archive.write(file,file.name,compress_type=zipfile.ZIP_DEFLATED)
    for file in (cache/'aar/jni/arm64-v8a').glob('*.so'):archive.write(file,'lib/arm64-v8a/'+file.name,compress_type=zipfile.ZIP_STORED)
'@ | python - $cache $source
if($LASTEXITCODE) {throw 'Probe APK assembly failed'}
& "$buildTools/zipalign.exe" -f -P 16 4 "$build/raw.apk" "$build/aligned.apk"
if($LASTEXITCODE) {throw 'Probe APK alignment failed'}
& "$buildTools/apksigner.bat" sign --ks $SigningKeystore --ks-pass pass:android --out "$build/playdate-litert-probe.apk" "$build/aligned.apk"
if($LASTEXITCODE) {throw 'Probe APK signing failed'}
Write-Output "$build/playdate-litert-probe.apk"
