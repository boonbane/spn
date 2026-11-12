# common.ps1 — shared setup run before every variant's recipes.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Write-Host "== common: base setup =="

$Root = 'C:\toolchains'
New-Item -ItemType Directory -Force -Path $Root | Out-Null

# the golden carries a user config from an older spn, which a current one rejects
Remove-Item -Recurse -Force "$env:APPDATA\spn" -ErrorAction SilentlyContinue

$exclusions = @($Root, $env:TEMP, "$env:LOCALAPPDATA\spn", "$env:APPDATA\spn")
foreach ($path in $exclusions) {
  try { Add-MpPreference -ExclusionPath $path -ErrorAction Stop }
  catch { Write-Host "  (skipped Defender exclusion for ${path}: $($_.Exception.Message))" }
}

try {
  Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
    -Name 'LongPathsEnabled' -Value 1 -Type DWord -ErrorAction Stop
} catch { Write-Host "  (skipped LongPathsEnabled: $($_.Exception.Message))" }

Write-Host "== common: done =="
