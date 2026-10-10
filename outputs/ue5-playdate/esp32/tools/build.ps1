param(
    [ValidateSet('build','filesystem','flash','flash-filesystem','monitor')][string]$Action='build',
    [string]$Board='m5dial',
    [string]$Port='COM9',
    [string]$ToolRoot='D:/PlaydateAI/toolchains',
    [string]$BuildRoot='D:/PlaydateAI/build/esp32'
)
$ErrorActionPreference='Stop'
$env:PLATFORMIO_CORE_DIR=Join-Path $ToolRoot 'platformio-core'
$env:PLATFORMIO_BUILD_DIR=$BuildRoot
$pio=Join-Path $ToolRoot 'esp32-pio/Scripts/pio.exe'
if (!(Test-Path -LiteralPath $pio)) { throw 'Install PlatformIO in an esp32-pio Python virtual environment first; see README.' }
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    if($Action -eq 'monitor') { & $pio device monitor --port $Port --baud 115200 }
    else {
        $arguments=@('run','-e',$Board)
        if($Action -eq 'filesystem') { $arguments+=@('-t','buildfs') }
        if($Action -eq 'flash') { $arguments+=@('-t','upload','--upload-port',$Port) }
        if($Action -eq 'flash-filesystem') { $arguments+=@('-t','uploadfs','--upload-port',$Port) }
        & $pio @arguments
    }
    if($LASTEXITCODE) { throw "PlatformIO failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
