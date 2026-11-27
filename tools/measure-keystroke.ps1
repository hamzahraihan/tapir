param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\Release\tapir.exe'),
    [int]$Runs = 5,
    [string[]]$Queries = @('v', 'vs', 'visua'),
    [ValidateSet('warm', 'cold', 'both')][string]$Mode = 'both',
    [int]$Warmup = 1,
    [switch]$Burst,
    [string]$ReportPath
)

$ErrorActionPreference = 'Stop'

# pwsh -File passes "v,vs,visua" as one token; direct invocation passes an array.
$Queries = @($Queries | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
if ($Queries.Count -eq 0) { throw 'no queries were provided' }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class TapirKeystrokeProbe
{
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long value);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long value);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", EntryPoint = "PostMessageW")] static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    const uint WM_HOTKEY = 0x0312;
    const uint WM_KEYDOWN = 0x0100;
    const uint WM_CHAR = 0x0102;
    const uint WM_APP_PROBE = 0x8005; // WM_APP + 5

    static long frequency;
    static TapirKeystrokeProbe() { QueryPerformanceFrequency(out frequency); }

    public static double NowMs()
    {
        long value;
        QueryPerformanceCounter(out value);
        return value * 1000.0 / frequency;
    }

    public static IntPtr FindPopup(int pid)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) =>
        {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner != (uint)pid) return true;
            var name = new StringBuilder(64);
            GetClassName(hwnd, name, name.Capacity);
            if (name.ToString() != "tapir") return true;
            found = hwnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static bool Visible(IntPtr hwnd) { return IsWindowVisible(hwnd); }
    public static void Toggle(IntPtr hwnd) { PostMessage(hwnd, WM_HOTKEY, new IntPtr(1), IntPtr.Zero); }
    public static void Key(IntPtr hwnd, int virtualKey) { PostMessage(hwnd, WM_KEYDOWN, new IntPtr(virtualKey), IntPtr.Zero); }
    public static void Char(IntPtr hwnd, char value) { PostMessage(hwnd, WM_CHAR, new IntPtr(value), IntPtr.Zero); }
    public static void Probe(IntPtr hwnd, int wParam) { PostMessage(hwnd, WM_APP_PROBE, new IntPtr(wParam), IntPtr.Zero); }
    public static void KeySync(IntPtr hwnd, int virtualKey) { SendMessage(hwnd, WM_KEYDOWN, new IntPtr(virtualKey), IntPtr.Zero); }
}
'@

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $count = $sorted.Count
    if ($count -eq 0) { return 0.0 }
    if ($count % 2 -eq 1) { return [double]$sorted[[int][math]::Floor($count / 2)] }
    return ([double]$sorted[$count / 2 - 1] + [double]$sorted[$count / 2]) / 2.0
}

function Format-Range([double[]]$values) {
    $median = Get-Median $values
    $min = ($values | Measure-Object -Minimum).Minimum
    $max = ($values | Measure-Object -Maximum).Maximum
    return ('median {0:N2}, range {1:N2}-{2:N2}, n={3}' -f $median, $min, $max, $values.Count)
}

function Format-Cell([double[]]$values) {
    $median = Get-Median $values
    $min = ($values | Measure-Object -Minimum).Minimum
    $max = ($values | Measure-Object -Maximum).Maximum
    return ('{0:N2} [{1:N2}-{2:N2}]' -f $median, $min, $max)
}

function Wait-Until([scriptblock]$condition, [int]$timeoutMs) {
    $start = [TapirKeystrokeProbe]::NowMs()
    while ([TapirKeystrokeProbe]::NowMs() - $start -lt $timeoutMs) {
        if (& $condition) { return $true }
        [void][System.Threading.Thread]::Yield()
    }
    return $false
}

function Read-Status([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { $lines = [System.IO.File]::ReadAllLines($path) } catch { return $null }
    $map = @{}
    foreach ($line in $lines) {
        $index = $line.IndexOf('=')
        if ($index -gt 0) { $map[$line.Substring(0, $index).Trim()] = $line.Substring($index + 1).Trim() }
    }
    return $map
}

function Request-Status($session, [int]$timeoutMs = 15000) {
    $path = $session.StatusPath
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    [TapirKeystrokeProbe]::Probe($session.Hwnd, 3)
    $ok = Wait-Until { Test-Path -LiteralPath $path } $timeoutMs
    if (-not $ok) { return $null }
    return Read-Status $path
}

function Wait-IndexReady($session, [int]$timeoutMs = 60000) {
    $start = [TapirKeystrokeProbe]::NowMs()
    $last = $null
    while ([TapirKeystrokeProbe]::NowMs() - $start -lt $timeoutMs) {
        $status = Request-Status $session 3000
        if ($null -ne $status -and $status.ContainsKey('indexReady') -and $status['indexReady'] -eq '1' -and
            $status.ContainsKey('indexEntries') -and [int]$status['indexEntries'] -gt 0) {
            return $status
        }
        if ($null -ne $status) { $last = $status }
        [void][System.Threading.Thread]::Yield()
    }
    $detail = if ($null -ne $last) { ($last.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ' } else { 'no status file was written' }
    throw "timed out after ${timeoutMs} ms waiting for indexReady=1 and indexEntries>0; last status: $detail"
}

function Parse-Tsv([string]$path) {
    $lines = @([System.IO.File]::ReadAllLines($path) | Where-Object { $_.Trim().Length -gt 0 })
    if ($lines.Count -lt 2) { throw "probe TSV has no data rows: $path" }
    $header = $lines[0].Split("`t")
    $required = @('queryLen', 'publishedQueryLen', 'entries', 'results', 'ioCalls', 'inputUs', 'matchDoneUs', 'sortDoneUs', 'uiDoneUs', 'valid')
    foreach ($name in $required) {
        if ($header -notcontains $name) { throw "probe TSV is missing column '$name': $path" }
    }
    $rows = @()
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $columns = $lines[$i].Split("`t")
        if ($columns.Count -ne $header.Count) {
            throw "probe TSV row $i has $($columns.Count) columns, expected $($header.Count): $path"
        }
        $record = [ordered]@{}
        for ($j = 0; $j -lt $header.Count; $j++) { $record[$header[$j]] = $columns[$j] }
        $rows += [pscustomobject]$record
    }
    return $rows
}

function Dump-Probe($session, [int]$expectedRows, [int]$timeoutMs = 5000) {
    $path = $session.ProbeOut
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    [TapirKeystrokeProbe]::Probe($session.Hwnd, 2)
    $ok = Wait-Until {
        if (-not (Test-Path -LiteralPath $path)) { return $false }
        try { $lines = @([System.IO.File]::ReadAllLines($path)) } catch { return $false }
        return ($lines.Count -ge ($expectedRows + 1))
    } $timeoutMs
    if (-not $ok) { throw "probe TSV did not contain $expectedRows data rows within ${timeoutMs} ms: $path" }
    return Parse-Tsv $path
}

function New-Config([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    @"
[general]
version = 1
hotkey = "Alt+Space"
animations = false
start_with_windows = false

[window]
monitor = "primary"
position = "upper-center"
width = 680
compact = true
max_visible_rows = 8
details_pane = false

[theme]
backdrop = "none"
backdrop_opacity = 1.0
font = "Segoe UI Variable"
preset = "catppuccin"
mode = "dark"
background = "#2b2b2b"
foreground = "#e6e6e6"
accent = "#8ab4f8"
muted = "#9aa0a6"
surface = "#333333"
selection = "#8ab4f8"

[history]
max_entries = 500
max_entry_bytes = 1048576
ignore_processes = []

[index]
sources = ["appsfolder", "startmenu"]

[privacy]
encrypt_history = false
"@ | Set-Content -LiteralPath (Join-Path $dir 'config.toml') -Encoding utf8
}

function Start-App([string]$exePath, [string]$configDir, [string]$probeOut) {
    $previousConfig = $env:TAPIR_CONFIG_DIR
    $previousProbe = $env:TAPIR_PROBE_OUT
    $env:TAPIR_CONFIG_DIR = $configDir
    $env:TAPIR_PROBE_OUT = $probeOut
    try { $process = Start-Process -FilePath $exePath -PassThru }
    finally {
        if ($null -eq $previousConfig) { Remove-Item Env:TAPIR_CONFIG_DIR -ErrorAction SilentlyContinue }
        else { $env:TAPIR_CONFIG_DIR = $previousConfig }
        if ($null -eq $previousProbe) { Remove-Item Env:TAPIR_PROBE_OUT -ErrorAction SilentlyContinue }
        else { $env:TAPIR_PROBE_OUT = $previousProbe }
    }
    $deadline = [TapirKeystrokeProbe]::NowMs() + 5000
    $hwnd = [IntPtr]::Zero
    while ([TapirKeystrokeProbe]::NowMs() -lt $deadline) {
        $hwnd = [TapirKeystrokeProbe]::FindPopup($process.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
        [void][System.Threading.Thread]::Yield()
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        $detail = if ($process.HasExited) { "exited with code $($process.ExitCode)" } else { 'still running' }
        throw "popup window was not created for PID $($process.Id) ($detail); config $configDir"
    }
    return @{ Process = $process; Hwnd = $hwnd; ConfigDir = $configDir; ProbeOut = $probeOut; StatusPath = "$probeOut.status" }
}

function Stop-App($session) {
    if ($null -eq $session) { return }
    if (-not $session.Process.HasExited) {
        $session.Process.CloseMainWindow() | Out-Null
        $session.Process.WaitForExit(2000) | Out-Null
        if (-not $session.Process.HasExited) {
            Stop-Process -Id $session.Process.Id -Force
            $session.Process.WaitForExit(2000) | Out-Null
        }
    }
    $session.Process.Dispose()
}

function Type-Query($session, [string]$text) {
    foreach ($character in $text.ToCharArray()) {
        # The app's message loop calls TranslateMessage, which synthesizes a
        # second WM_CHAR from a posted WM_KEYDOWN and inserts the character
        # twice. Post the character message directly, as measure.ps1 does.
        [TapirKeystrokeProbe]::Char($session.Hwnd, $character)
    }
}

function Clear-Query($session, [int]$count) {
    if ($count -le 0) { return }
    # SendMessage dispatches straight to the window proc, so TranslateMessage
    # never synthesizes a WM_CHAR for VK_ESCAPE. A posted WM_KEYDOWN would add
    # a bogus zeroed probe sample after the measured keystrokes.
    [TapirKeystrokeProbe]::KeySync($session.Hwnd, 0x1B)
}

if (-not (Test-Path -LiteralPath $Exe)) { throw "executable not found: $Exe" }
$existing = @(Get-Process -Name tapir -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) { throw "stop the running tapir process before measuring (PID $($existing[0].Id))" }

$root = Join-Path $env:TEMP ('tapir-keystroke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null

$originalConfigDir = $env:TAPIR_CONFIG_DIR
$originalProbeOut = $env:TAPIR_PROBE_OUT

$modes = if ($Mode -eq 'both') { @('warm', 'cold') } else { @($Mode) }
$samples = @{}
$indexEntries = @{}
$burstResults = @()
$session = $null
$s4Failed = $false
$lines = [System.Collections.Generic.List[string]]::new()

try {
    $lines.Add('# Keystroke probe measurements')
    $lines.Add(('Measured on {0:yyyy-MM-dd HH:mm:ss} with tools/measure-keystroke.ps1. The harness drives the real popup with posted window messages and reads the app-side probe through WM_APP+5.' -f (Get-Date)))
    $lines.Add(('Exe: {0}; runs per mode: {1}; warmup passes: {2}; queries: {3}; modes: {4}; order alternates forward/reverse (ABBA-style) across runs.' -f (Resolve-Path -LiteralPath $Exe).Path, $Runs, $Warmup, ($Queries -join ','), ($modes -join ',')))
    $lines.Add('')

    foreach ($measurementMode in $modes) {
        $samples[$measurementMode] = @{}
        for ($run = 0; $run -lt $Runs; $run++) {
            $fixture = Join-Path $root ("$measurementMode-run$run")
            New-Item -ItemType Directory -Path $fixture -Force | Out-Null
            New-Config $fixture
            $probeOut = Join-Path $fixture 'probe.tsv'
            if ($measurementMode -eq 'cold') {
                $cache = Join-Path $fixture 'index.cache'
                if (Test-Path -LiteralPath $cache) { Remove-Item -LiteralPath $cache -Force }
            }

            $session = Start-App $Exe $fixture $probeOut
            try {
                [void](Wait-Until { -not [TapirKeystrokeProbe]::Visible($session.Hwnd) } 1000)
                [TapirKeystrokeProbe]::Toggle($session.Hwnd)
                if (-not (Wait-Until { [TapirKeystrokeProbe]::Visible($session.Hwnd) } 2000)) {
                    throw 'popup did not become visible after WM_HOTKEY'
                }

                [TapirKeystrokeProbe]::Probe($session.Hwnd, 1)
                $ready = Wait-IndexReady $session 60000
                $indexEntries[$measurementMode] = [int]$ready['indexEntries']
                $currentLength = 0

                if ($Burst) {
                    Clear-Query $session $currentLength
                    $currentLength = 0
                    [TapirKeystrokeProbe]::Probe($session.Hwnd, 1)
                    foreach ($character in @('q', 'a', 'b')) {
                        [TapirKeystrokeProbe]::Char($session.Hwnd, [char]$character)
                    }
                    $currentLength = 3
                    $status = Request-Status $session 2000
                    if ($null -eq $status) { throw 'probe status was not written after the burst sequence' }
                    $published = if ($status.ContainsKey('publishedQueryLen')) { $status['publishedQueryLen'] } else { 'unknown' }
                    $rows = Dump-Probe $session 3 5000
                    $burstResults += [pscustomobject]@{
                        Mode = $measurementMode
                        Run = $run
                        PublishedQueryLen = $published
                        DumpedSets = $rows.Count
                    }
                    continue
                }

                $order = if (($run % 4) -eq 1 -or ($run % 4) -eq 2) { @($Queries)[($Queries.Count - 1)..0] } else { @($Queries) }
                foreach ($query in $order) {
                    Clear-Query $session $currentLength
                    $currentLength = 0
                    [TapirKeystrokeProbe]::Probe($session.Hwnd, 1)

                    if ($measurementMode -eq 'warm') {
                        for ($pass = 0; $pass -lt $Warmup; $pass++) {
                            Type-Query $session $query
                            $currentLength = $query.Length
                            Clear-Query $session $currentLength
                            $currentLength = 0
                        }
                        [TapirKeystrokeProbe]::Probe($session.Hwnd, 4)
                    }

                    Type-Query $session $query
                    $currentLength = $query.Length
                    $status = Request-Status $session 2000
                    if ($null -eq $status) {
                        $why = if ($session.Process.HasExited) { "app exited with code $($session.Process.ExitCode)" } else { 'app still running' }
                        throw "probe status was not written after query '$query' ($why)"
                    }
                    $rows = Dump-Probe $session $query.Length 5000
                    $lengthKey = $query.Length
                    if (-not $samples[$measurementMode].ContainsKey($lengthKey)) { $samples[$measurementMode][$lengthKey] = @() }
                    foreach ($row in $rows) {
                        if ($row.valid -ne '1') { throw "invalid probe sample for query '$query': $($row | Out-String)" }
                        if ([int]$row.entries -le 0 -or [int]$row.publishedQueryLen -le 0) { continue }
                        $samples[$measurementMode][$lengthKey] += $row
                    }
                }
            }
            finally {
                Stop-App $session
                $session = $null
            }
        }
    }

    $lines.Add(('{0,-5} {1,3} {2,-22} {3,-22} {4,-22} {5,-22} {6,7} {7,8}  {8}' -f
        'Mode', 'Len', 'inputUs [med min-max]', 'matchDoneUs [med min-max]', 'sortDoneUs [med min-max]', 'uiDoneUs [med min-max]', 'ioCalls', 'entries', 'limiter'))
    $lines.Add(('{0,-5} {1,3} {2,-22} {3,-22} {4,-22} {5,-22} {6,7} {7,8}  {8}' -f
        '----', '---', '----------------------', '----------------------', '----------------------', '----------------------', '-------', '--------', '-------'))

    foreach ($measurementMode in $modes) {
        foreach ($lengthKey in @($samples[$measurementMode].Keys | Sort-Object)) {
            $rows = @($samples[$measurementMode][$lengthKey])
            $inputUs = @($rows | ForEach-Object { [double]$_.inputUs })
            $matchUs = @($rows | ForEach-Object { [double]$_.matchDoneUs })
            $sortUs = @($rows | ForEach-Object { [double]$_.sortDoneUs })
            $uiUs = @($rows | ForEach-Object { [double]$_.uiDoneUs })
            $ioCalls = @($rows | ForEach-Object { [double]$_.ioCalls })
            $ioMedian = Get-Median $ioCalls
            $stageMedians = [ordered]@{
                inputUs = Get-Median $inputUs
                matchDoneUs = Get-Median $matchUs
                sortDoneUs = Get-Median $sortUs
                uiDoneUs = Get-Median $uiUs
            }
            $limiter = if ($ioMedian -gt 0) { 'keystroke I/O' } else {
                ($stageMedians.GetEnumerator() | Sort-Object -Property Value -Descending | Select-Object -First 1).Key
            }
            $lines.Add(('{0,-5} {1,3} {2,-22} {3,-22} {4,-22} {5,-22} {6,7:N2} {7,8}  {8}' -f
                $measurementMode, $lengthKey, (Format-Cell $inputUs), (Format-Cell $matchUs), (Format-Cell $sortUs), (Format-Cell $uiUs), $ioMedian, $indexEntries[$measurementMode], $limiter))
        }
    }

    $lines.Add('')
    foreach ($measurementMode in $modes) {
        foreach ($lengthKey in @($samples[$measurementMode].Keys | Sort-Object)) {
            $rows = @($samples[$measurementMode][$lengthKey])
            $lines.Add(('raw {0} len={1}: inputUs {2}; matchDoneUs {3}; sortDoneUs {4}; uiDoneUs {5}' -f
                $measurementMode, $lengthKey, (Format-Range @($rows | ForEach-Object { [double]$_.inputUs })),
                (Format-Range @($rows | ForEach-Object { [double]$_.matchDoneUs })),
                (Format-Range @($rows | ForEach-Object { [double]$_.sortDoneUs })),
                (Format-Range @($rows | ForEach-Object { [double]$_.uiDoneUs }))))
        }
    }

    if ($Burst) {
        $lines.Add('')
        $lines.Add('Burst (q, qa, qab posted back-to-back):')
        $lines.Add(('{0,-5} {1,3} {2,20} {3,12}' -f 'Mode', 'Run', 'publishedQueryLen', 'dumpedSets'))
        foreach ($burstItem in $burstResults) {
            $lines.Add(('{0,-5} {1,3} {2,20} {3,12}' -f $burstItem.Mode, $burstItem.Run, $burstItem.PublishedQueryLen, $burstItem.DumpedSets))
        }
    }

    $lines.Add('')
    if ($Mode -eq 'warm') {
        foreach ($lengthKey in @($samples['warm'].Keys)) {
            foreach ($row in $samples['warm'][$lengthKey]) {
                if ([double]$row.ioCalls -gt 0) { $s4Failed = $true }
            }
        }
        $lines.Add(('S4 warm I/O check: {0} (ioCalls>0 in any warm sample => exit 1).' -f $(if ($s4Failed) { 'FAIL' } else { 'PASS' })))
    } else {
        $lines.Add('S4 warm I/O check: not run (use -Mode warm for the dedicated gate).')
    }
}
finally {
    Stop-App $session
    if ($null -eq $originalConfigDir) { Remove-Item Env:TAPIR_CONFIG_DIR -ErrorAction SilentlyContinue }
    else { $env:TAPIR_CONFIG_DIR = $originalConfigDir }
    if ($null -eq $originalProbeOut) { Remove-Item Env:TAPIR_PROBE_OUT -ErrorAction SilentlyContinue }
    else { $env:TAPIR_PROBE_OUT = $originalProbeOut }
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}

$lines | ForEach-Object { Write-Output $_ }
if ($ReportPath) {
    $parent = Split-Path -Parent $ReportPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $lines -join [Environment]::NewLine | Set-Content -LiteralPath $ReportPath -Encoding utf8
}
if ($s4Failed) { exit 1 }
exit 0

