# Runs the engine in the Azahar emulator without taking the focus, waits, and saves a screenshot of its window.
#   powershell -File tools/emu_run.ps1 [-wait 12] [-out work/emu.png]
# Azahar is expected in ..\Azahar\azahar-windows-msvc-*\ (portable: its SD card is user\sdmc there).
param([int]$wait = 12, [string]$out = "work\emu.png")
$root = Split-Path -Parent $PSScriptRoot
$az = Get-ChildItem (Join-Path (Split-Path -Parent $root) "Azahar") -Directory -Filter "azahar-windows-*" | Select-Object -First 1
$sd = Join-Path $az.FullName "user\sdmc\3ds\3dsilksong"
Copy-Item (Join-Path $root "3dsilksong.3dsx") $sd -Force
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Fg {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
}
"@
$prev = [Fg]::GetForegroundWindow()
Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath (Join-Path $az.FullName "azahar.exe") -ArgumentList "`"$sd\3dsilksong.3dsx`"" `
    -WorkingDirectory $az.FullName -PassThru
# as soon as its window exists: behind everything, and the focus back where it was
for ($i = 0; $i -lt 50 -and $p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh() }
[Fg]::SetWindowPos($p.MainWindowHandle, [IntPtr]1, 0, 0, 0, 0, 0x13) | Out-Null   # HWND_BOTTOM, no move/size/activate
[Fg]::SetForegroundWindow($prev) | Out-Null
Start-Sleep -Seconds $wait
& (Join-Path $PSScriptRoot "winshot.ps1") azahar (Join-Path $root $out)
