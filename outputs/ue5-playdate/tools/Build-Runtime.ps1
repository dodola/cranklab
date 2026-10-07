param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot '../../work/runtime-build-portable')) }
$source = Join-Path $projectRoot 'runtime'
& python "$PSScriptRoot/embed-launcher.py"
if ($LASTEXITCODE) { throw 'Launcher adapter generation failed' }
& cmake -S $source -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DPD_DEPS_ROOT=$source/deps"
if ($LASTEXITCODE) { throw 'Runtime configuration failed' }
& cmake --build $BuildDirectory --config Release --parallel 4
if ($LASTEXITCODE) { throw 'Runtime build failed' }
$binaryDirectory = Join-Path $projectRoot 'bin/Win64'
$pluginDirectory = Join-Path $projectRoot 'Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Win64'
New-Item -ItemType Directory -Force $binaryDirectory,$pluginDirectory | Out-Null
foreach ($file in @('crank-runtime.exe','crank-core.dll','SDL2.dll','SDL2_mixer.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory "Release/$file") -Destination $binaryDirectory
    if ($file.EndsWith('.dll')) { Copy-Item -LiteralPath (Join-Path $BuildDirectory "Release/$file") -Destination $pluginDirectory }
}
Write-Output $binaryDirectory
