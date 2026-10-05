param([string]$Python = 'python', [string]$Destination = '')
$ErrorActionPreference = 'Stop'
$candidate = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$Destination) {
  $workspace = (Resolve-Path (Join-Path $candidate '../..')).Path
  $Destination = Join-Path $workspace 'outputs/V6.2.3_Gated_PI'
}
& $Python -X utf8 (Join-Path $PSScriptRoot 'package_623.py') $Destination
if ($LASTEXITCODE -ne 0) { throw 'Release checks or packaging failed' }
