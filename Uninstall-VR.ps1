param([string]$GameRoot)
$ErrorActionPreference = 'Stop'
if (Get-Process DyingLightGame_x64_rwdi -ErrorAction SilentlyContinue) { throw 'Close Dying Light 2 before removing the mod.' }
if (!$GameRoot) {
    $key = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 534380' -ErrorAction SilentlyContinue
    $GameRoot = $key.InstallLocation
    if (!$GameRoot) { $GameRoot = Read-Host 'Dying Light 2 game folder' }
}
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$target = Join-Path $GameRoot 'ph\work\bin\x64'
$receiptPath = Join-Path $target 'DL2VR-install.json'
$destination = Join-Path $target 'winmm.dll'
if (!(Test-Path -LiteralPath $receiptPath)) { throw 'No mod installation receipt found; no files removed.' }
$receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
if ($receipt.destination -ne $destination) { throw 'Receipt path mismatch; no files removed.' }
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $receipt.sha256) { throw 'Installed DLL has changed; no files removed.' }
    Remove-Item -LiteralPath $destination
}
Remove-Item -LiteralPath $receiptPath
Write-Output 'DyingLight2VR removed. INI and game settings preserved.'
