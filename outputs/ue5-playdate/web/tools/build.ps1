param(
    [Parameter(Mandatory=$true)][string]$EmsdkRoot,
    [string]$BuildDirectory = "D:/PlaydateAI/build/web-wasm"
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/..").Path
$sdk = (Resolve-Path -LiteralPath $EmsdkRoot).Path
$env:EMSDK = $sdk
$env:EM_CONFIG = Join-Path $sdk '.emscripten'
$nodeFile = Get-ChildItem -LiteralPath (Join-Path $sdk 'node') -Filter node.exe -Recurse | Select-Object -First 1
$env:EMSDK_NODE = $nodeFile.FullName
$env:PATH = "$sdk/upstream/emscripten;$sdk/upstream/bin;$(Split-Path $nodeFile.FullName);$env:PATH"
& cmake -S $project -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$sdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
if ($LASTEXITCODE -ne 0) { throw 'Web CMake configuration failed' }
& cmake --build $BuildDirectory --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'WebAssembly compilation failed' }
