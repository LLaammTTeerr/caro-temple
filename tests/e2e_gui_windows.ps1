# End-to-end test of the GUI version on Windows: real keyboard/mouse input through SendInput
# (the same OS path a physical keyboard/mouse uses), assertions through the CARO_TRACE log.
# Usage: pwsh tests/e2e_gui_windows.ps1 <dir containing caro.exe>
param([string]$Dir)
$ErrorActionPreference = "Stop"
$scale = [double]($env:XINJ_SCALE ?? "1")

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Inj {
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint data, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct U { [FieldOffset(0)] public KEYBDINPUT k; [FieldOffset(0)] public MOUSEINPUT m; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public U u; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    public static void Key(ushort vk, bool ext) {
        var a = new INPUT[2];
        ushort sc = (ushort)MapVirtualKey(vk, 0);
        uint f = ext ? 1u : 0u;
        a[0].type = 1; a[0].u.k.wVk = vk; a[0].u.k.wScan = sc; a[0].u.k.dwFlags = f;
        a[1].type = 1; a[1].u.k.wVk = vk; a[1].u.k.wScan = sc; a[1].u.k.dwFlags = f | 2u;
        SendInput(1, new[] { a[0] }, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(60);
        SendInput(1, new[] { a[1] }, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Click() {
        var a = new INPUT[1];
        a[0].type = 0; a[0].u.m.dwFlags = 2; SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(60);
        a[0].u.m.dwFlags = 4; SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@

$VK = @{ Enter = 0x0D; Escape = 0x1B; Space = 0x20; Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28; Home = 0x24 }
$EXT = @{ Left = $true; Up = $true; Right = $true; Down = $true; Home = $true }
function Wait([int]$ms) { Start-Sleep -Milliseconds ([int]($ms * $scale)) }
function Keys([string[]]$names) { foreach ($n in $names) { [Inj]::Key([uint16]$VK[$n], [bool]$EXT[$n]); Wait 250 } }
function Fail([string]$msg) { Write-Host "E2E GUI (Windows) FAIL: $msg"; Get-Content $script:trace -ErrorAction SilentlyContinue; exit 1 }
function Want([string]$line, [string]$what) {
    for ($i = 0; $i -lt 20; $i++) {
        if ((Get-Content $script:trace -ErrorAction SilentlyContinue) -contains $line) { Write-Host "ok: $what"; return }
        Wait 250
    }
    Fail "$what (no '$line' in trace)"
}
function LastScene { (Get-Content $script:trace | Where-Object { $_ -like "scene *" } | Select-Object -Last 1) }
function Start-Caro([string]$name, [string]$cargs) {
    $script:trace = Join-Path $env:RUNNER_TEMP "trace-$name.txt"
    Remove-Item $script:trace -ErrorAction SilentlyContinue
    $env:CARO_TRACE = $script:trace
    $env:CARO_DATA_DIR = Join-Path $env:RUNNER_TEMP "data-$name"
    $env:CARO_AFK_IDLE = "600"
    $sp = @{ FilePath = (Join-Path $Dir "caro.exe"); WorkingDirectory = $Dir; PassThru = $true }
    if ($cargs) { $sp.ArgumentList = $cargs }
    $script:proc = Start-Process @sp
    $script:hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 120 -and $script:hwnd -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 250
        if ($script:proc.HasExited) { Fail "caro.exe exited with code $($script:proc.ExitCode) before showing a window" }
        $script:hwnd = $script:proc.MainWindowHandle; $script:proc.Refresh()
        if ($script:hwnd -eq [IntPtr]::Zero) { $script:hwnd = [Inj]::FindWindow($null, "Caro Temple") }
    }
    if ($script:hwnd -eq [IntPtr]::Zero) { Fail "window not found after 30 s" }
    Write-Host "window found"
    # wait for the first frame (trace) and let the window settle before typing
    for ($i = 0; $i -lt 60; $i++) { if ((Get-Content $script:trace -ErrorAction SilentlyContinue) -contains "scene 0") { break }; Start-Sleep -Milliseconds 250 }
    Wait 1500
    [void][Inj]::SetForegroundWindow($script:hwnd)
    Wait 500
}

Write-Host "== menus: Enter selects, Esc goes back"
Start-Caro "menus" ""
Keys @("Enter"); Want "scene 1" "Enter on Play opens New Game"
Keys @("Escape"); Wait 800
if ((LastScene) -ne "scene 0") { Fail "Esc did not go back to Home" }; Write-Host "ok: Esc goes back to Home"
Stop-Process -Id $script:proc.Id -Force; Wait 800

Write-Host "== match: arrows move, Space places, mouse click places, Esc pause/close, Home"
Start-Caro "match" ""
Keys @("Enter"); Wait 700; Keys @("Up", "Up", "Enter"); Wait 700; Keys @("Up", "Up", "Enter"); Wait 1200
Want "scene 5" "keyboard navigates Setup and Profile into a match"
Keys @("Space"); Want "moves 1" "keyboard placed a stone"; Want "moves 2" "computer replied"
# mouse: board is 576x576 at (352,80) in the game's 1280x720 virtual screen, which is
# letterboxed into the client area exactly like gui/main.c does; click cell (row 8, col 1)
$rc = New-Object Inj+RECT; [void][Inj]::GetClientRect($script:hwnd, [ref]$rc)
$cw = $rc.R - $rc.L; $ch = $rc.B - $rc.T
$k = [Math]::Min($cw / 1280.0, $ch / 720.0); $ox = ($cw - 1280 * $k) / 2; $oy = ($ch - 720 * $k) / 2
Write-Host "client area ${cw}x${ch}, scale $k"
$p = New-Object Inj+POINT; $p.X = [int]($ox + (352 + 1.5 * 57.6) * $k); $p.Y = [int]($oy + (80 + 8.5 * 57.6) * $k)
[void][Inj]::ClientToScreen($script:hwnd, [ref]$p); [void][Inj]::SetCursorPos($p.X, $p.Y); Wait 300; [Inj]::Click()
Want "moves 3" "mouse click placed a stone"; Want "moves 4" "computer replied again"
Keys @("Escape"); Want "overlay 1" "Esc opens Pause"
Keys @("Escape"); Wait 600
if ((Get-Content $script:trace | Select-Object -Last 3) -notcontains "overlay 0") { Fail "Esc did not close Pause" }
Write-Host "ok: Esc closes Pause"
Keys @("Home"); Wait 1200
if ((LastScene) -ne "scene 0") { Fail "Home key did not go to the main screen" }; Write-Host "ok: Home key goes to the main screen"
Stop-Process -Id $script:proc.Id -Force
Write-Host "E2E GUI (Windows): all checks passed"
