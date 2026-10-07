param([string]$EngineDirectory='C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
python "$PSScriptRoot/manifest-fixture.py"
if($LASTEXITCODE) { throw 'Fixture manifest creation failed' }
python "$PSScriptRoot/export-device.py"
if($LASTEXITCODE) { throw 'Device conversion failed' }
& "$EngineDirectory/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$root/Unreal/CrankLab.uproject" `
    -run=pythonscript "-script=$PSScriptRoot/create-ue-assets.py" -unattended -nullrhi -nosound -NoSplash
if($LASTEXITCODE) { throw 'UE asset creation failed' }
