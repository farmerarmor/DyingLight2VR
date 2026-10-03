param([string]$GameRoot)
$ErrorActionPreference = 'Stop'
if (Get-Process DyingLightGame_x64_rwdi -ErrorAction SilentlyContinue) { throw 'Close Dying Light 2 before installing.' }
if (!$GameRoot) {
    $key = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 534380' -ErrorAction SilentlyContinue
    $GameRoot = $key.InstallLocation
    if (!$GameRoot) { $GameRoot = Read-Host 'Dying Light 2 game folder' }
}
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$target = Join-Path $GameRoot 'ph\work\bin\x64'
$baseline = Get-Content (Join-Path $PSScriptRoot 'baseline.json') -Raw | ConvertFrom-Json
foreach ($entry in $baseline) {
    $file = Join-Path $target $entry.file
    if (!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Unsupported game binary: $($entry.file). Use a mod release matching your game build."
    }
}
$source = Join-Path $PSScriptRoot 'bin\winmm.dll'
$expected = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$destination = Join-Path $target 'winmm.dll'
$receiptPath = Join-Path $target 'DL2VR-install.json'
if (Test-Path -LiteralPath $destination) {
    $existing = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($existing -ne $expected) {
        if (!(Test-Path -LiteralPath $receiptPath)) { throw 'Existing winmm.dll is not managed by this installer. It was not overwritten.' }
        $previous = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        if ($previous.destination -ne $destination -or $previous.sha256 -ne $existing) { throw 'Existing DLL differs from its mod receipt. It was not overwritten.' }
        $backup = Join-Path $PSScriptRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Copy-Item -LiteralPath $destination -Destination (Join-Path $backup 'winmm.dll')
        Copy-Item -LiteralPath $receiptPath -Destination (Join-Path $backup 'DL2VR-install.json')
    }
}
Copy-Item -LiteralPath $source -Destination $destination
if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expected) { throw 'Installed checksum mismatch.' }
$ini = Join-Path $target 'DL2VR.ini'
if (!(Test-Path -LiteralPath $ini)) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'DL2VR.ini') -Destination $ini }
@{destination=$destination;sha256=$expected;version='0.2.0';installedAt=(Get-Date).ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath $receiptPath
Write-Output 'DyingLight2VR DX12 installed. Select DirectX 12 in the game. Activate your OpenXR headset and run Launch-VR.cmd.'
Write-Output "Settings: $ini"
