param(
    [ValidateSet('arm64-v8a','x86_64')][string]$Abi = 'arm64-v8a',
    [string]$NdkDirectory = 'C:/Users/dodol/AppData/Local/Android/Sdk/ndk/27.2.12479018',
    [string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'runtime'
& python "$PSScriptRoot/embed-launcher.py"
if ($LASTEXITCODE) { throw 'Launcher adapter generation failed' }
$cmakeExe = Join-Path ([IO.Path]::GetFullPath((Join-Path $NdkDirectory '../..'))) 'cmake/3.22.1/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { $cmakeExe = 'cmake' }
if (-not $BuildDirectory) { $BuildDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot "../../work/runtime-android-$Abi")) }
& $cmakeExe -S $source -B $BuildDirectory -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$NdkDirectory/build/cmake/android.toolchain.cmake" "-DANDROID_ABI=$Abi" '-DANDROID_PLATFORM=android-26' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' "-DPD_DEPS_ROOT=$source/deps"
if ($LASTEXITCODE) { throw 'Android runtime configuration failed' }
& $cmakeExe --build $BuildDirectory --parallel 4
if ($LASTEXITCODE) { throw 'Android runtime build failed' }
$binaryDirectory = Join-Path $projectRoot "bin/Android/$Abi"
$pluginDirectory = Join-Path $projectRoot "Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Android/$Abi"
New-Item -ItemType Directory -Force $binaryDirectory,$pluginDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'libcrank-core.so') -Destination $binaryDirectory
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'crank-probe') -Destination $binaryDirectory
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'libcrank-core.so') -Destination $pluginDirectory
Write-Output $binaryDirectory
