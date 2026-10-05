$ErrorActionPreference = 'Stop'
$expected = '26b0bc8aff796a220c3d64fe7aa41e574d7a40b57a84bfd3dafa50d0bbba67cf'
$outputPath = Join-Path $PSScriptRoot 'XIN_Power_Studio_V6.2.3.exe'
if (Test-Path -LiteralPath $outputPath) {
    if ((Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected) {
        Write-Output 'The EXE already exists and its SHA256 is correct.'
        exit 0
    }
    throw 'An EXE with a different SHA256 already exists. Move it before merging.'
}
$partPaths = @(1, 2 | ForEach-Object { Join-Path $PSScriptRoot "XIN_Power_Studio_V6.2.3.part$_" })
foreach ($partPath in $partPaths) { if (-not (Test-Path -LiteralPath $partPath)) { throw "Missing file: $partPath" } }
$tempPath = Join-Path $PSScriptRoot ('merge-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    $writer = [System.IO.File]::Open($tempPath, [System.IO.FileMode]::CreateNew)
    try {
        foreach ($partPath in $partPaths) {
            $reader = [System.IO.File]::OpenRead($partPath)
            try { $reader.CopyTo($writer) } finally { $reader.Dispose() }
        }
    } finally { $writer.Dispose() }
    if ((Get-FileHash -LiteralPath $tempPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'SHA256 mismatch. Download both parts again.' }
    Move-Item -LiteralPath $tempPath -Destination $outputPath
    Write-Output "Created and verified: $outputPath"
} finally {
    if (Test-Path -LiteralPath $tempPath) { Remove-Item -LiteralPath $tempPath }
}
