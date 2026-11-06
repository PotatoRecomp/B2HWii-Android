param([int]$ProcId, [string]$Out)
# Capture a window's client area (PrintWindow, works behind other windows): b-shot.ps1 -ProcId PID -Out file.png
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class BShotW { [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
public struct R { public int L, T, Rr, B; } }
"@
$h = (Get-Process -Id $ProcId).MainWindowHandle
$r = New-Object BShotW+R; [BShotW]::GetClientRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ([Math]::Max($r.Rr, 1)), ([Math]::Max($r.B, 1))
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[BShotW]::PrintWindow($h, $dc, 3) | Out-Null; $g.ReleaseHdc($dc); $bmp.Save($Out)
"$($r.Rr)x$($r.B)"
