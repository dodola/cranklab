param([string]$CacheDirectory='D:/PlaydateAI/online-translation',[string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',[string]$JdkDirectory='C:/Program Files/Java/jdk-21')
$ErrorActionPreference='Stop'
$cache=[IO.Path]::GetFullPath($CacheDirectory)
if([IO.Path]::GetPathRoot($cache) -eq 'C:\'){throw 'Use D drive for new build caches'}
$build=Join-Path $cache 'android-probe';$classes=Join-Path $build 'classes';$dex=Join-Path $build 'dex'
New-Item -ItemType Directory -Force $classes,$dex | Out-Null
$source=Join-Path $PSScriptRoot 'android-online-probe';$androidJar="$SdkDirectory/platforms/android-36/android.jar";$bt="$SdkDirectory/build-tools/36.1.0"
$env:JAVA_HOME=$JdkDirectory
& "$JdkDirectory/bin/javac.exe" --release 8 -Xlint:-options -encoding UTF-8 -classpath $androidJar -d $classes (Join-Path $source 'OnlineProbe.java') (Join-Path $source 'ProbeActivity.java')
if($LASTEXITCODE){throw 'Probe Java compilation failed'}
& "$JdkDirectory/bin/jar.exe" --create --file "$build/classes.jar" -C $classes .
if($LASTEXITCODE){throw 'Probe Java assembly failed'}
& "$bt/d8.bat" --lib $androidJar --min-api 26 --output $dex "$build/classes.jar"
if($LASTEXITCODE){throw 'Probe dex compilation failed'}
& "$bt/aapt2.exe" link --manifest (Join-Path $source 'AndroidManifest.xml') -I $androidJar -o "$build/raw.apk"
if($LASTEXITCODE){throw 'Probe manifest compilation failed'}
& "$JdkDirectory/bin/jar.exe" --update --file "$build/raw.apk" -C $dex classes.dex
if($LASTEXITCODE){throw 'Probe APK assembly failed'}
& "$bt/zipalign.exe" -f 4 "$build/raw.apk" "$build/aligned.apk"
if($LASTEXITCODE){throw 'Probe APK alignment failed'}
& "$bt/apksigner.bat" sign --ks C:/Users/dodol/.android/debug.keystore --ks-pass pass:android --out "$build/online-translation-probe.apk" "$build/aligned.apk"
if($LASTEXITCODE){throw 'Probe APK signing failed'}
Write-Output "$build/online-translation-probe.apk"
