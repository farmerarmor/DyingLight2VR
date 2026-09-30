$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Configure-VR.ps1') -ResolutionOnly
if (-not $?) { throw 'VR configuration failed; game was not launched.' }
Start-Process 'steam://rungameid/534380' -WindowStyle Hidden

