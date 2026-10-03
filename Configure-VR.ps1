param([string]$SettingsPath = "$env:USERPROFILE\Documents\dying light 2\out\settings\video.scr", [switch]$ResolutionOnly)
$ErrorActionPreference = 'Stop'
if (Get-Process DyingLightGame_x64_rwdi -ErrorAction SilentlyContinue) { throw 'Close the game before configuring VR.' }
$report = & (Join-Path $PSScriptRoot 'bin\XrRuntimeChecks.exe') 2>&1
if ($LASTEXITCODE -ne 0) { throw "Activate your OpenXR headset first. Runtime query failed: $report" }
$views = [regex]::Matches(($report -join "`n"), 'eye=([01]) recommended=(\d+)x(\d+) max=(\d+)x(\d+)')
if ($views.Count -ne 2 -or $views[0].Groups[1].Value -eq $views[1].Groups[1].Value) { throw 'Expected recommendations for both headset eyes.' }
$width = ($views | ForEach-Object { [int]$_.Groups[2].Value } | Measure-Object -Maximum).Maximum
$height = ($views | ForEach-Object { [int]$_.Groups[3].Value } | Measure-Object -Maximum).Maximum
# Both eyes use equally sized textures. No arbitrary GPU-memory budget: the
# D3D12 texture dimension limit and both runtime view limits are authoritative.
$maxWidth = [Math]::Min(16384, ($views | ForEach-Object { [int]$_.Groups[4].Value } | Measure-Object -Minimum).Minimum)
$maxHeight = [Math]::Min(16384, ($views | ForEach-Object { [int]$_.Groups[5].Value } | Measure-Object -Minimum).Minimum)
if ($width -lt 1 -or $height -lt 1 -or $width -gt $maxWidth -or $height -gt $maxHeight) {
 throw "Headset recommendation ${width}x${height} exceeds the D3D12/runtime limit ${maxWidth}x${maxHeight}."
}
$text = Get-Content -LiteralPath $SettingsPath -Raw
$values = [ordered]@{WindowMode='0'; Resolution="$width,$height"}
foreach ($name in $values.Keys) {
 $pattern = '(?m)^' + [regex]::Escape($name) + '\([^\r\n]*\)'
 if ([regex]::Matches($text,$pattern).Count -ne 1) { throw "Expected exactly one $name setting." }
 $text = [regex]::Replace($text,$pattern,($name+'('+$values[$name]+')'))
}
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'diagnostics') | Out-Null
$backup = Join-Path $PSScriptRoot ('diagnostics\video-before-vr-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.scr')
Copy-Item -LiteralPath $SettingsPath -Destination $backup
[IO.File]::WriteAllText($SettingsPath,$text,[Text.UTF8Encoding]::new($false))
$report | Set-Content (Join-Path $PSScriptRoot 'diagnostics\runtime-resolution.txt')
Write-Output "Configured $width x $height output and windowed mode. Backup: $backup"
Write-Output 'DLSS/upscaling, render scale, AA and other graphics preferences preserved.'
