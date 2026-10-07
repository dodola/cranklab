param(
    [Parameter(Mandatory = $true)][string[]]$GamePaths,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$reports = foreach ($gamePath in $GamePaths) {
    $root = (Resolve-Path -LiteralPath $gamePath).Path
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Expected a PDX directory: $root" }
    $metadata = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $root 'pdxinfo')) {
        if ($line -match '^([^=]+)=(.*)$') { $metadata[$Matches[1]] = $Matches[2] }
    }
    $files = @(Get-ChildItem -LiteralPath $root -File -Recurse)
    $native = @($files | Where-Object { $_.Name -match '^pdex\.' } | ForEach-Object {
        $bytes = [IO.File]::ReadAllBytes($_.FullName)
        $format = 'unknown'
        $architecture = 'unknown'
        if ($bytes.Length -ge 4) {
            $magic = [BitConverter]::ToString($bytes, 0, 4)
            if ($magic -eq 'CF-FA-ED-FE' -and $bytes.Length -ge 8) {
                $format = 'Mach-O 64-bit'
                $cpu = [BitConverter]::ToUInt32($bytes, 4)
                if ($cpu -eq 0x0100000C) { $architecture = 'ARM64' }
                elseif ($cpu -eq 0x01000007) { $architecture = 'x86_64' }
            } elseif ($magic.StartsWith('4D-5A')) {
                $format = 'PE'
            } elseif ($magic -eq '7F-45-4C-46') {
                $format = 'ELF'
            }
        }
        [ordered]@{ path = $_.FullName.Substring($root.Length + 1); bytes = $_.Length; format = $format; architecture = $architecture }
    })
    $luaContainers = @($files | Where-Object { $_.Extension -eq '.pdz' } | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($root.Length + 1); bytes = $_.Length }
    })
    $classification = if ($native.Count -gt 0) { 'native-or-hybrid' }
        elseif (Test-Path -LiteralPath (Join-Path $root 'main.pdz')) { 'lua-only-package' }
        else { 'unknown' }
    [ordered]@{
        root = $root
        metadata = $metadata
        classification = $classification
        files = $files.Count
        bytes = ($files | Measure-Object Length -Sum).Sum
        nativeFiles = $native
        luaContainers = $luaContainers
        extensions = @($files | Group-Object Extension | Sort-Object Name | ForEach-Object {
            [ordered]@{ extension = $_.Name; count = $_.Count }
        })
        verification = 'Directory inventory only; execution and API coverage not verified.'
    }
}
$report = [ordered]@{ schemaVersion = 1; games = @($reports) }
$outputAbsolute = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputAbsolute)) | Out-Null
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputAbsolute -Encoding utf8
Write-Output $outputAbsolute
