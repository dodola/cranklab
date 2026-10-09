param(
    [string]$CacheDirectory='D:/PlaydateAI/hymt2-gpu-experiment',
    [ValidateSet('opencl','vulkan')][string]$Backend='opencl',
    [switch]$Generic,
    [string]$SdkDirectory='C:/Users/dodol/AppData/Local/Android/Sdk',
    [string]$JdkDirectory='C:/Program Files/Java/jdk-21',
    [string]$SigningKeystore='C:/Users/dodol/.android/debug.keystore',
    [string]$Glslc
)
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
$cache=[IO.Path]::GetFullPath($CacheDirectory)
if([IO.Path]::GetPathRoot($cache) -eq 'C:\'){throw 'Experimental caches must use D drive'}
$variant=$Backend+$(if($Generic){'-generic'}else{''})
$source=Join-Path $cache "source-$variant";$build=Join-Path $cache "build-$variant"
New-Item -ItemType Directory -Force $build | Out-Null
$prepare=@((Join-Path $PSScriptRoot 'Prepare-HyMtGpuProbe.py'),'--cache',$cache,'--backend',$Backend)
if($Generic){$prepare+='--generic'}
& python @prepare
if($LASTEXITCODE){throw 'Probe preparation failed'}
$ndk="$SdkDirectory/ndk/27.2.12479018"
$cmake="$SdkDirectory/cmake/3.22.1/bin/cmake.exe"
$options=@('-S',$source,'-B',$build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$SdkDirectory/cmake/3.22.1/bin/ninja.exe",'-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake",'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_static',"-DPython3_EXECUTABLE=$((Get-Command python.exe).Source)")
if($Backend -eq 'opencl'){
    $options+=@("-DOpenCL_INCLUDE_DIR=$cache/deps/OpenCL-Headers","-DOpenCL_LIBRARY=$ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26/libdl.so",'-DGGML_OPENCL_EMBED_KERNELS=ON',"-DGGML_OPENCL_USE_ADRENO_KERNELS=$($(if($Generic){'OFF'}else{'ON'}))")
}else{
    if(!$Glslc){throw 'Specify host glslc for Vulkan shader compilation'}
    $options+=@("-DVulkan_GLSLC_EXECUTABLE=$Glslc",'-DGGML_VULKAN_COOPMAT=OFF','-DGGML_VULKAN_COOPMAT2=OFF')
}
& $cmake @options *> "$build/configure.log"
if($LASTEXITCODE){Get-Content "$build/configure.log" -Tail 24;throw 'Native GPU probe configure failed'}
& $cmake --build $build --target crank-translate speedprobe -j 6 *> "$build/build.log"
if($LASTEXITCODE){Get-Content "$build/build.log" -Tail 36;throw 'Native GPU probe build failed'}
$classes=Join-Path $build 'classes';$dex=Join-Path $build 'dex'
New-Item -ItemType Directory -Force $classes,$dex | Out-Null
$javaSource=Join-Path $PSScriptRoot 'android-hymt-gpu-probe'
$env:JAVA_HOME=$JdkDirectory
$androidJar="$SdkDirectory/platforms/android-36/android.jar";$buildTools="$SdkDirectory/build-tools/36.1.0"
& "$JdkDirectory/bin/javac.exe" --release 8 -encoding UTF-8 -classpath $androidJar -d $classes (Join-Path $javaSource 'GpuProbe.java') (Join-Path $javaSource 'ProbeActivity.java')
if($LASTEXITCODE){throw 'Probe Java compilation failed'}
& "$JdkDirectory/bin/jar.exe" --create --file "$build/classes.jar" -C $classes .
if($LASTEXITCODE){throw 'Probe Java assembly failed'}
& "$buildTools/d8.bat" --lib $androidJar --min-api 26 --output $dex "$build/classes.jar"
if($LASTEXITCODE){throw 'Probe dex compilation failed'}
& "$buildTools/aapt2.exe" link --manifest (Join-Path $javaSource 'AndroidManifest.xml') -I $androidJar -o "$build/raw.apk"
if($LASTEXITCODE){throw 'Probe manifest compilation failed'}
@'
from pathlib import Path
import sys,zipfile
root=Path(sys.argv[1])
with zipfile.ZipFile(root/'raw.apk','a') as apk:
    apk.write(root/'dex/classes.dex','classes.dex',compress_type=zipfile.ZIP_DEFLATED)
    for name in ('libcrank-translate.so','libspeedprobe.so'):
        file=list(root.rglob(name))
        if len(file)!=1:raise RuntimeError('Missing or duplicate native library: '+name)
        apk.write(file[0],'lib/arm64-v8a/'+name,compress_type=zipfile.ZIP_STORED)
'@ | python - $build
if($LASTEXITCODE){throw 'Probe APK assembly failed'}
& "$buildTools/zipalign.exe" -f -P 16 4 "$build/raw.apk" "$build/aligned.apk"
if($LASTEXITCODE){throw 'Probe alignment failed'}
& "$buildTools/apksigner.bat" sign --ks $SigningKeystore --ks-pass pass:android --out "$build/playdate-hymt-$variant-probe.apk" "$build/aligned.apk"
if($LASTEXITCODE){throw 'Probe signing failed'}
Write-Output "$build/playdate-hymt-$variant-probe.apk"
