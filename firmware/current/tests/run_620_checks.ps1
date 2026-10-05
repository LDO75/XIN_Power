$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '../02_Application')).Path
$testExe = Join-Path $PSScriptRoot 'test_620.exe'
$sources = @((Join-Path $PSScriptRoot 'test_620.c'), "$appRoot/Core/SRC/calibration.c", "$appRoot/Core/SRC/power_control.c", "$appRoot/Core/SRC/mcp4725.c")
& gcc -std=c11 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources -lm -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host test failed' }
$sources[0] = Join-Path $PSScriptRoot 'test_trim_623.c'
$trimExe = Join-Path $PSScriptRoot 'test_trim_623.exe'
& gcc -std=c11 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources -lm -o $trimExe
if ($LASTEXITCODE -ne 0) { throw 'PI compilation failed' }
& $trimExe
if ($LASTEXITCODE -ne 0) { throw 'PI regression failed' }
foreach ($check in @('test_uart_recovery', 'test_settings_620')) {
  $testExe = Join-Path $PSScriptRoot "$check.exe"
  $sources = @((Join-Path $PSScriptRoot "$check.c"))
  $sources += "$appRoot/Core/SRC/wire_protocol.c"
  if ($check -eq 'test_settings_620') { $sources += "$appRoot/Core/SRC/user_settings.c" }
  & gcc -std=c11 -ffunction-sections -fdata-sections -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources '-Wl,--gc-sections' -o $testExe
  if ($LASTEXITCODE -ne 0) { throw "$check compilation failed" }
  & $testExe
  if ($LASTEXITCODE -ne 0) { throw "$check failed" }
}
$testExe = Join-Path $PSScriptRoot 'test_monitor_500.exe'
$sources = @((Join-Path $PSScriptRoot 'test_monitor_500.c'), "$appRoot/Core/SRC/ina226.c", "$appRoot/Core/SRC/power_monitor.c")
& gcc -std=c11 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources -o $testExe
if ($LASTEXITCODE -ne 0) { throw '500Hz monitor compilation failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw '500Hz monitor regression failed' }
$testExe = Join-Path $PSScriptRoot 'test_lcd_yield.exe'
$sources = @((Join-Path $PSScriptRoot 'test_lcd_yield.c'), "$appRoot/Core/SRC/lcd.c")
& gcc -std=c11 -ffunction-sections -fdata-sections -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources '-Wl,--gc-sections' -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'LCD yield compilation failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'LCD yield regression failed' }

foreach ($check in @('test_console_620','test_ui_620')) {
  $testExe = Join-Path $PSScriptRoot "$check.exe"
  $sources = @((Join-Path $PSScriptRoot "$check.c"))
  if ($check -eq 'test_console_620') {
    $sources += @("$appRoot/Core/SRC/wire_protocol.c", "$appRoot/Core/SRC/calibration.c", "$appRoot/Core/SRC/power_control.c", "$appRoot/Core/SRC/mcp4725.c")
  } else { $sources += @("$appRoot/Core/SRC/lcd.c", "$appRoot/Core/SRC/ui_layout.c") }
  & gcc -std=c11 -ffunction-sections -fdata-sections -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -DPY32F403xD -DUSE_HAL_DRIVER "-I$appRoot/Core/INC" "-I$appRoot/Drivers/PY32F403_HAL_Driver/Inc" "-I$appRoot/Drivers/CMSIS/Include" "-I$appRoot/Drivers/CMSIS/Device/PY32F403/Include" @sources '-Wl,--gc-sections' -o $testExe
  if ($LASTEXITCODE -ne 0) { throw "$check compilation failed" }
  Push-Location $PSScriptRoot
  try {
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "$check failed" }
  } finally { Pop-Location }
}
