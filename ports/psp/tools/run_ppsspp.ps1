# PPSSPP v1.20.4/D3D11 can initialize without usable output when started
# minimized: restoring leaves only a blurred background while emulation runs.
# Start visibly so emulator validation actually includes rendered output.
[CmdletBinding()]
param(
    [string]$Emulator,
    [string]$Pbp,
    [string]$Config
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (!$Emulator) { $Emulator = Join-Path $repo 'build-tools/ppsspp/PPSSPPWindows64.exe' }
if (!$Pbp) { $Pbp = Join-Path $repo 'build-tools/ppsspp/memstick/PSP/GAME/OpenRBX/EBOOT.PBP' }
if (!$Config) { $Config = Join-Path $repo 'build-tools/ppsspp/memstick/PSP/SYSTEM/ppsspp.ini' }
foreach ($path in @($Emulator, $Pbp, $Config)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $path" }
}
# Windows file names cannot contain a double quote; quote each complete argument
# explicitly because Start-Process joins ArgumentList into one command line.
$pspProcess = Start-Process -FilePath $Emulator -WindowStyle Normal -PassThru `
    -ArgumentList @("--config=`"$Config`"", "`"$Pbp`"")
$pspProcess.Id | Set-Content -LiteralPath (Join-Path $repo 'build-psp-emulator.pid')
Write-Output "Started visible PPSSPP process $($pspProcess.Id)"
