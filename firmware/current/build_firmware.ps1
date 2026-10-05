param(
  [string]$KeilPath = 'E:/Keil_develop/UV4/UV4.exe',
  [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot '02_Application/MDK-ARM/XIN_Power.uvprojx'
$buildLog = Join-Path $PSScriptRoot 'tests/keil_release.log'
$arguments = @('-r', ('"' + $project + '"'), '-j0', '-o', ('"' + $buildLog + '"'))
$process = Start-Process -FilePath $KeilPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
$buildText = Get-Content -LiteralPath $buildLog -Raw
if ($buildText -notmatch '0 Error\(s\), 0 Warning\(s\)') { throw 'Firmware build did not pass: see tests/keil_release.log' }
& $Python -X utf8 (Join-Path $PSScriptRoot 'tests/check_stack_budget.py')
if ($LASTEXITCODE -ne 0) { throw 'Linked RAM/stack budget failed; do not release the HEX' }
