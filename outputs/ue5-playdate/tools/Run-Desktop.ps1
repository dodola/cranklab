param([Parameter(Mandatory=$true)][string]$GamePath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$game = (Resolve-Path -LiteralPath $GamePath).Path
$saveRoot = Join-Path $projectRoot 'user-data'
New-Item -ItemType Directory -Force $saveRoot | Out-Null
$env:PD_SAVE_DIR = $saveRoot
$env:PD_STUB_LOG = '1'
& (Join-Path $projectRoot 'bin/Win64/crank-runtime.exe') --scale 3 $game
if ($LASTEXITCODE) { throw "Runtime exited with code $LASTEXITCODE" }
