# vs.ps1 — install Visual Studio Build Tools with the C++ (MSVC) workload.
# Args: the VS year, "2022" or "2026"; -Minimal installs the workload at the
# installer's defaults and nothing else, which leaves out ATL and the ARM64 tools.
param([string]$Year = '2022', [switch]$Minimal)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# aka.ms channel names changed at major 18: release/pre -> stable/insiders
$channels = @{
  '2022' = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
  '2026' = 'https://aka.ms/vs/18/stable/vs_BuildTools.exe'
}
$url = $channels[$Year]
if (-not $url) { throw "unknown VS year: $Year" }

Write-Host "== Visual Studio $Year Build Tools =="
$exe = Join-Path $env:TEMP "vs_BuildTools_$Year.exe"
Invoke-WebRequest -Uri $url -OutFile $exe -UseBasicParsing

# aka.ms answers an unknown channel with a redirect to bing, so a wrong URL lands
# an HTML page here and Start-Process only says "corrupted and unreadable".
$mz = [System.IO.File]::ReadAllBytes($exe)[0..1]
if ($mz[0] -ne 0x4D -or $mz[1] -ne 0x5A) {
  throw "vs_BuildTools for $Year is not a PE image; $url did not serve an installer"
}

$vsArgs = @(
  '--quiet', '--wait', '--norestart', '--nocache',
  '--add', 'Microsoft.VisualStudio.Workload.VCTools',
  '--includeRecommended'
)
if (-not $Minimal) {
  $vsArgs += @(
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.ARM64',
    '--add', 'Microsoft.VisualStudio.Component.VC.ATL',
    '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621'
  )
}
$proc = Start-Process -FilePath $exe -ArgumentList $vsArgs -Wait -PassThru
Remove-Item -Force $exe
if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
  throw "vs_BuildTools exited with $($proc.ExitCode)"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw "no Visual Studio install with the x64 C++ tools after setup" }
Write-Host $install
if (-not $Minimal -and -not (Get-ChildItem "$install\VC\Tools\MSVC\*\atlmfc\include\atlbase.h" -ErrorAction SilentlyContinue)) {
  throw "ATL was requested but atlbase.h is not under $install"
}
Write-Host "== Visual Studio $Year done =="
