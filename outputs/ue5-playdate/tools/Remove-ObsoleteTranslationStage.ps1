# Remove only inference libraries/classes in this project's generated Android
# stage: Gradle otherwise retains old UPL copies across incremental packages.
param([string]$ProjectDirectory=(Join-Path $PSScriptRoot '../Unreal'))
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath($ProjectDirectory)
$stage=[IO.Path]::GetFullPath((Join-Path $project 'Intermediate/Android'))
if(!(Test-Path -LiteralPath $stage)){return}
$files=Get-ChildItem -LiteralPath $stage -File -Recurse | Where-Object {
    $_.Name -match '^lib(ggml.*|llama|crank-translate|vendor-hash)\.so$' -or
    $_.Name -match '^CrankModelDownload(\$.*)?\.(java|class)$'
}
foreach($file in $files) {
    $resolved=[IO.Path]::GetFullPath($file.FullName)
    if(!$resolved.StartsWith($stage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Staging path escaped the project Android directory'}
    Remove-Item -LiteralPath $resolved
}
Write-Output "Removed $($files.Count) obsolete generated translation files"
