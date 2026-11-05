[CmdletBinding()]
param(
    [ValidateRange(1, 100)][int]$Rounds = 20,
    [switch]$PrepareOnly,
    [switch]$PromptForNotes,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PrepareOnly -and $SelfTest) { throw '-PrepareOnly and -SelfTest cannot be combined; no process has been started.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot 'Build/ARM64/Debug/Inkeys.exe'
$dllPath = Join-Path $repoRoot 'Inkeys/PptCOM.dll'
$nativeLogRoot = Join-Path (Split-Path -Parent $exePath) 'log'
$utf8 = [Text.UTF8Encoding]::new($false)
$clockFrequency = [Diagnostics.Stopwatch]::Frequency

function Write-JsonFile($Path, $Value) {
    # 同目录替换，Ctrl+C 时保留上一份完整 JSON。
    $temporary = $Path + '.tmp'
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 14), $utf8)
    if ([IO.File]::Exists($Path)) {
        $backup = $Path + '.previous'
        [IO.File]::Replace($temporary, $Path, $backup)
        [IO.File]::Delete($backup)
    }
    else { [IO.File]::Move($temporary, $Path) }
}

function Get-RunningInkeys {
    @(Get-Process -Name Inkeys -ErrorAction SilentlyContinue | ForEach-Object {
        $image = $null
        try { $image = $_.Path } catch { }
        [ordered]@{ pid = $_.Id; path = $image }
    })
}

function Test-ProbeMarkers {
    $stream = [IO.File]::OpenRead($exePath)
    try {
        $buffer = [byte[]]::new(65536)
        $tail = ''; $shutdown = $false; $managed = $false
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $chunk = $tail + [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
            $shutdown = $shutdown -or $chunk.Contains('[ShutdownTrace] v=1 source=native')
            $managed = $managed -or $chunk.Contains('[PptPerf] v=1 source=native')
            if ($shutdown -and $managed) { return $true }
            $tail = $chunk.Substring([Math]::Max(0, $chunk.Length - 64))
        }
        return $false
    } finally { $stream.Dispose() }
}

function Get-EmbeddedPptHash {
    if (-not ('ShutdownDiagnosticResource' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ShutdownDiagnosticResource {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr FindResource(IntPtr module, IntPtr name, string type);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    public static byte[] Read(string path, int id) {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 2);
        if (module == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        try {
            IntPtr resource = FindResource(module, new IntPtr(id), "DLL");
            if (resource == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            uint size = SizeofResource(module, resource);
            IntPtr data = LockResource(LoadResource(module, resource));
            if (size == 0 || data == IntPtr.Zero) throw new InvalidOperationException("Empty DLL resource");
            byte[] bytes = new byte[checked((int)size)];
            Marshal.Copy(data, bytes, 0, bytes.Length);
            return bytes;
        } finally { FreeLibrary(module); }
    }
}
'@
    }
    $match = [regex]::Match([IO.File]::ReadAllText((Join-Path $repoRoot 'Inkeys/Resource.h')), '(?m)^#define\s+IDR_DLL1\s+(\d+)')
    if (-not $match.Success) { throw 'Cannot locate IDR_DLL1.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([ShutdownDiagnosticResource]::Read($exePath, [int]$match.Groups[1].Value))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Read-TraceEvent([string]$Line) {
    $match = [regex]::Match($Line, '\[(ShutdownTrace|PptPerf)\]\s+(.*)')
    if (-not $match.Success) { return }
    $fields = @{}
    foreach ($field in [regex]::Matches($match.Groups[2].Value, '(\w+)=([^\s]+)')) { $fields[$field.Groups[1].Value] = $field.Groups[2].Value }
    foreach ($required in @('pid', 'span', 'event', 'stage', 'qpc', 'frequency', 'source')) { if (-not $fields.ContainsKey($required)) { return } }
    $namespace = if ($match.Groups[1].Value -eq 'ShutdownTrace' -and $fields['source'] -eq 'native') { 'native' }
        elseif ($match.Groups[1].Value -eq 'PptPerf' -and $fields['source'] -eq 'managed' -and $fields['stage'] -match '^(shutdown-|cleanup-)') { 'managed' }
        else { return }
    $processNumber = [long]0; $qpcNumber = [long]0; $frequencyNumber = [long]0
    if (-not [long]::TryParse($fields['pid'], [ref]$processNumber)) { return }
    $valid = $fields['span'] -match '^\d+$' -and [long]::TryParse($fields['qpc'], [ref]$qpcNumber) -and $qpcNumber -ge 0 -and [long]::TryParse($fields['frequency'], [ref]$frequencyNumber) -and $frequencyNumber -gt 0
    [pscustomobject]@{ namespace = $namespace; pid = $processNumber; span = $fields['span']; event = $fields['event']; stage = $fields['stage']; qpc = $qpcNumber; frequency = $frequencyNumber; valid = $valid }
}

function Get-TraceAnalysis([string[]]$Lines, [long]$ChildPid, $DeathQpc, [long]$DeathFrequency) {
    $pairs = @{}; $close = $null; $last = $null; $nativeIssues = @(); $invalidManagedRecords = @()
    foreach ($line in $Lines) {
        $record = Read-TraceEvent $line
        if ($null -eq $record -or $record.pid -ne $ChildPid) { continue }
        if (-not $record.valid) {
            if ($record.namespace -eq 'native') { $nativeIssues += [pscustomobject]@{ pid = $record.pid; span = $record.span; stage = $record.stage; issue = 'invalid-qpc-frequency-or-span'; raw = $line } }
            else { $invalidManagedRecords += [pscustomobject]@{ record = $record; raw = $line } }
            continue
        }
        $key = '{0}/{1}/{2}' -f $record.namespace, $record.pid, $record.span
        if (-not $pairs.ContainsKey($key)) { $pairs[$key] = [ordered]@{ namespace = $record.namespace; pid = $record.pid; span = $record.span; stage = $record.stage; begin = $null; end = $null } }
        if ($record.event -eq 'begin') { $pairs[$key].begin = $record }
        elseif ($record.event -eq 'end') { $pairs[$key].end = $record }
        if ($record.namespace -eq 'native') {
            if ($null -eq $last -or $record.qpc -gt $last.qpc) { $last = $record }
            if ($record.event -eq 'begin' -and $record.stage -eq 'set-off' -and ($null -eq $close -or $record.qpc -lt $close.qpc)) { $close = $record }
        }
    }
    $finalWindows = @($pairs.Values | Where-Object { $_.namespace -eq 'managed' -and $null -ne $_.begin -and $_.stage -in @('shutdown-managed-finally', 'cleanup-final') })
    $managedUnfinished = @(); $managedIssues = @()
    $native = @(); $managed = @(); $unfinished = @(); $mainMs = $null; $mainEnd = $null
    foreach ($pair in $pairs.Values) {
        $duration = $null
        if ($null -ne $pair.begin -and $null -ne $pair.end -and $pair.end.qpc -ge $pair.begin.qpc -and $pair.begin.frequency -eq $pair.end.frequency) {
            $duration = [Math]::Round(1000.0 * ($pair.end.qpc - $pair.begin.qpc) / $pair.begin.frequency, 4)
        }
        $row = [ordered]@{ namespace = $pair.namespace; pid = $pair.pid; span = $pair.span; stage = $pair.stage; begin_qpc = $(if ($null -ne $pair.begin) { $pair.begin.qpc } else { $null }); end_qpc = $(if ($null -ne $pair.end) { $pair.end.qpc } else { $null }); duration_ms = $duration }
        if ($pair.namespace -eq 'native') {
            if ($null -eq $pair.begin -and $null -ne $pair.end) { $nativeIssues += [pscustomobject]@{ pid = $pair.pid; span = $pair.span; stage = $pair.stage; issue = 'end-without-begin' } }
            elseif ($null -ne $pair.begin -and $null -ne $pair.end -and $null -eq $duration) { $nativeIssues += [pscustomobject]@{ pid = $pair.pid; span = $pair.span; stage = $pair.stage; issue = 'reversed-qpc-or-frequency-mismatch'; begin_qpc = $pair.begin.qpc; end_qpc = $pair.end.qpc; begin_frequency = $pair.begin.frequency; end_frequency = $pair.end.frequency } }
            $native += [pscustomobject]$row
            if ($null -ne $pair.begin -and $null -eq $pair.end) { $unfinished += [pscustomobject]$row }
            if ($pair.stage -eq 'main-shutdown' -and $null -ne $duration) { $mainMs = $duration; $mainEnd = $pair.end }
        } else {
            # 启动期 cleanup-rebind 仍保留，但不能算入本次正常关闭耗时。
            $point = if ($null -ne $pair.begin) { $pair.begin } else { $pair.end }
            $pairEnd = if ($null -ne $pair.end) { $pair.end.qpc } elseif ($null -ne $DeathQpc) { $DeathQpc } else { [long]::MaxValue }
            # managed 可在退出槽 CAS 后、native 首次 Capture 前进入 finally，按区间重叠归属。
            $beginInside = $null -ne $close -and $null -ne $point -and $point.qpc -ge $close.qpc -and ($null -eq $DeathQpc -or $point.qpc -le $DeathQpc)
            $endInside = $null -ne $close -and $null -ne $pair.end -and $pair.end.qpc -ge $close.qpc -and ($null -eq $DeathQpc -or $pair.end.qpc -le $DeathQpc)
            # 损坏的反向 span 也按任一端点判断相关性，再由完整性检查拒绝，不能先把它过滤掉。
            $overlaps = $null -ne $close -and $null -ne $point -and $point.qpc -le $close.qpc -and $pairEnd -ge $close.qpc
            $row['in_shutdown_window'] = $beginInside -or $endInside -or $overlaps
            $row['attributed_to_final_shutdown'] = $false
            if ($row.in_shutdown_window -and $pair.stage -ne 'cleanup-rebind') {
                if ($pair.stage -in @('shutdown-managed-finally', 'cleanup-final')) { $row['attributed_to_final_shutdown'] = $true }
                foreach ($window in $finalWindows) {
                    if ($point.qpc -ge $window.begin.qpc -and ($null -eq $window.end -or ($point.qpc -le $window.end.qpc -and $window.end.qpc -ge $close.qpc))) { $row['attributed_to_final_shutdown'] = $true; break }
                }
            }
            if ($row.attributed_to_final_shutdown) {
                if ($null -ne $pair.begin -and $null -eq $pair.end) { $managedUnfinished += [pscustomobject]$row }
                elseif ($null -eq $pair.begin -or $null -eq $duration) { $managedIssues += [pscustomobject]@{ pid = $pair.pid; span = $pair.span; stage = $pair.stage; issue = 'missing-begin-or-invalid-qpc-frequency'; begin_qpc = $row.begin_qpc; end_qpc = $row.end_qpc } }
            }
            $managed += [pscustomobject]$row
        }
    }
    if ($null -ne $close) {
        foreach ($invalid in $invalidManagedRecords) {
            $record = $invalid.record
            $attributed = $record.stage -in @('shutdown-managed-finally', 'cleanup-final')
            foreach ($window in $finalWindows) {
                if ($record.stage -ne 'cleanup-rebind' -and $record.qpc -ge $window.begin.qpc -and ($null -eq $window.end -or $record.qpc -le $window.end.qpc) -and ($null -eq $window.end -or $window.end.qpc -ge $close.qpc)) { $attributed = $true; break }
            }
            if ($attributed) { $managedIssues += [pscustomobject]@{ pid = $record.pid; span = $record.span; stage = $record.stage; issue = 'invalid-qpc-frequency-or-span'; raw = $invalid.raw } }
        }
    }
    $latency = $null
    if ($null -ne $close -and $null -ne $DeathQpc -and $DeathQpc -ge $close.qpc -and $close.frequency -eq $DeathFrequency) {
        $latency = [Math]::Round(1000.0 * ($DeathQpc - $close.qpc) / $DeathFrequency, 4)
    }
    [ordered]@{
        accepted_close_qpc = $(if ($null -ne $close) { $close.qpc } else { $null })
        accepted_close_to_observed_death_ms = $latency
        main_shutdown_ms = $mainMs
        after_main_shutdown_ms = $(if ($null -ne $mainEnd -and $null -ne $DeathQpc -and $DeathQpc -ge $mainEnd.qpc -and $DeathFrequency -eq $mainEnd.frequency) { [Math]::Round(1000.0 * ($DeathQpc - $mainEnd.qpc) / $DeathFrequency, 4) } else { $null })
        last_stage = $(if ($null -ne $last) { $last.stage } else { $null })
        last_active_stage = $(if ($unfinished.Count) { ($unfinished | Sort-Object begin_qpc -Descending | Select-Object -First 1).stage } else { $null })
        unfinished_native_spans = @($unfinished | Sort-Object begin_qpc)
        invalid_native_spans = @($nativeIssues)
        native_spans = @($native | Sort-Object begin_qpc)
        managed_cleanup_spans = @($managed | Sort-Object begin_qpc)
        unfinished_managed_final_spans = @($managedUnfinished | Sort-Object begin_qpc)
        invalid_managed_final_spans = @($managedIssues)
        incomplete = $null -eq $latency -or $null -eq $mainMs -or $unfinished.Count -gt 0 -or $nativeIssues.Count -gt 0 -or $managedUnfinished.Count -gt 0 -or $managedIssues.Count -gt 0
        slow = $null -ne $latency -and $latency -ge 3000
    }
}

function Get-RoundSummary($Round) {
    $analysis = $Round.analysis
    [pscustomobject][ordered]@{
        round = $Round.round; pid = $Round.pid; status = $Round.status
        exit_code = $Round.exit_code; exit_hex = $Round.exit_hex
        normal = $null -ne $analysis -and $Round.exit_code -eq 0 -and $Round.stream_drain_complete -and -not $analysis.incomplete -and -not $analysis.slow -and $Round.capture_errors.Count -eq 0 -and $Round.followup_processes.Count -eq 0
        slow = $null -ne $analysis -and $analysis.slow
        nonzero = $null -ne $Round.exit_code -and $Round.exit_code -ne 0
        incomplete = -not $Round.stream_drain_complete -or $null -eq $analysis -or $analysis.incomplete -or $Round.capture_errors.Count -gt 0 -or $Round.followup_processes.Count -gt 0
        accepted_close_to_observed_death_ms = $(if ($null -ne $analysis) { $analysis.accepted_close_to_observed_death_ms } else { $null })
        main_shutdown_ms = $(if ($null -ne $analysis) { $analysis.main_shutdown_ms } else { $null })
        after_main_shutdown_ms = $(if ($null -ne $analysis) { $analysis.after_main_shutdown_ms } else { $null })
        last_stage = $(if ($null -ne $analysis) { $analysis.last_stage } else { $null })
        last_active_stage = $(if ($null -ne $analysis) { $analysis.last_active_stage } else { $null })
        unfinished_native_spans = $(if ($null -ne $analysis) { $analysis.unfinished_native_spans.Count } else { $null })
        invalid_native_spans = $(if ($null -ne $analysis) { $analysis.invalid_native_spans.Count } else { $null })
        unfinished_managed_final_spans = $(if ($null -ne $analysis) { $analysis.unfinished_managed_final_spans.Count } else { $null })
        invalid_managed_final_spans = $(if ($null -ne $analysis) { $analysis.invalid_managed_final_spans.Count } else { $null })
        stream_drain_ms = $Round.stream_drain_ms; notes = $Round.notes; folder = $Round.folder
    }
}

function Save-Session {
    Write-JsonFile (Join-Path $sessionRoot 'manifest.json') $session
    $summaries = @($roundRecords | ForEach-Object { Get-RoundSummary $_ })
    Write-JsonFile (Join-Path $sessionRoot 'summary.json') ([ordered]@{ schema_version = 1; status = $session.status; requested_rounds = $session.requested_rounds; completed_rounds = @($roundRecords | Where-Object { $null -ne $_.observed_death_qpc }).Count; rounds = $summaries })
    if ($summaries.Count) { [IO.File]::WriteAllLines((Join-Path $sessionRoot 'summary.csv'), [string[]]@($summaries | ConvertTo-Csv -NoTypeInformation), $utf8) }
    else { [IO.File]::WriteAllText((Join-Path $sessionRoot 'summary.csv'), 'round,pid,status,exit_code,exit_hex,normal,slow,nonzero,incomplete' + "`r`n", $utf8) }
}

function Save-Round($Round) {
    Write-JsonFile (Join-Path (Join-Path $sessionRoot $Round.folder) 'manifest.json') $Round
    Save-Session
}

function Invoke-CaptureRound([int]$Number, [string]$LaunchImage, [string]$LaunchArguments, [bool]$Mock) {
    $folder = 'round-{0:d2}' -f $Number
    $roundRoot = Join-Path $sessionRoot $folder
    [void][IO.Directory]::CreateDirectory($roundRoot)
    $round = [ordered]@{
        schema_version = 1; round = $Number; folder = $folder; status = 'starting'; pid = $null
        executable = $LaunchImage; exe_sha256 = $session.exe_sha256; pptcom_sha256 = $session.pptcom_sha256; embedded_pptcom_sha256 = $session.embedded_pptcom_sha256
        source_head = $session.source_head; source_dirty = $session.source_dirty
        qpc_utc_anchor = [ordered]@{ qpc = [Diagnostics.Stopwatch]::GetTimestamp(); frequency = $clockFrequency; utc = [DateTimeOffset]::UtcNow.ToString('o') }
        started_utc = $null; launch_qpc = $null; observed_death_utc = $null; observed_death_qpc = $null; capture_ended_utc = $null
        exit_code = $null; exit_hex = $null; stream_drain_ms = $null; stream_drain_complete = $false
        copied_native_logs = @(); capture_errors = @(); followup_processes = @(); notes = ''; analysis = $null
    }
    [void]$roundRecords.Add($round)
    Save-Round $round
    $child = [Diagnostics.Process]::new()
    $stdout = $null; $stderr = $null; $tail = $null; $copyOut = $null; $copyError = $null
    try {
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $LaunchImage; $info.Arguments = $LaunchArguments
        $info.WorkingDirectory = if ($Mock) { $sessionRoot } else { Split-Path -Parent $exePath }
        $info.UseShellExecute = $false; $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
        # 只改本轮 child 环境；进程退场后不会给用户或系统留下诊断开关。
        $info.EnvironmentVariables['INKEYS_PPT_PERF'] = '1'
        $info.EnvironmentVariables['INKEYS_PPT_TIMING'] = '1'
        $child.StartInfo = $info
        $stdout = [IO.File]::Open((Join-Path $roundRoot 'stdout.log'), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $stderr = [IO.File]::Open((Join-Path $roundRoot 'stderr.log'), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $round.started_utc = [DateTimeOffset]::UtcNow.ToString('o')
        $round.launch_qpc = [Diagnostics.Stopwatch]::GetTimestamp()
        if (-not $child.Start()) { throw 'Child process did not start.' }
        $round.pid = $child.Id; $round.status = 'running'
        $copyOut = $child.StandardOutput.BaseStream.CopyToAsync($stdout)
        $copyError = $child.StandardError.BaseStream.CopyToAsync($stderr)
        Save-Round $round
        if ($Mock) { Write-Host ('Self-test round {0}/{1}, fixed console child PID={2}.' -f $Number, $session.requested_rounds, $child.Id) }
        else { Write-Host ('第 {0}/{1} 轮，PID={2}。请自行探索界面，完成后点击“关闭软件”；本轮不要点击重启。' -f $Number, $session.requested_rounds, $child.Id) }
        $tail = [IO.StreamReader]::new([IO.File]::Open((Join-Path $roundRoot 'stderr.log'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite), $utf8)
        $liveBuffer = [char[]]::new(8192)
        $pending = ''; $watchLines = [Collections.Generic.List[string]]::new(); $noticed = $false
        $nextSample = [Diagnostics.Stopwatch]::GetTimestamp()
        while ($true) {
            if ($child.WaitForExit(200)) {
                # 进程死亡观测必须先于管道排空、文件复制和报告分析。
                $round.observed_death_qpc = [Diagnostics.Stopwatch]::GetTimestamp()
                $round.observed_death_utc = [DateTimeOffset]::UtcNow.ToString('o')
                $round.exit_code = $child.ExitCode
                $round.exit_hex = '0x' + [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$child.ExitCode), 0).ToString('X8')
                $round.status = if ($child.ExitCode -eq 0) { 'exited' } else { 'exited-nonzero' }
                Save-Round $round
                break
            }
            $now = [Diagnostics.Stopwatch]::GetTimestamp()
            if (-not $noticed -and $now -ge $nextSample) {
                # 实时提示只读有界增量，不能因扫描大日志推迟下一次进程死亡观测。
                $read = $tail.Read($liveBuffer, 0, $liveBuffer.Length)
                $text = $pending + [string]::new($liveBuffer, 0, $read)
                $parts = [regex]::Split($text, '\r?\n'); $pending = $parts[$parts.Length - 1]
                if ($pending.Length -gt 4096) { $pending = $pending.Substring($pending.Length - 4096) }
                for ($index = 0; $index -lt $parts.Length - 1; ++$index) { if ($parts[$index].Contains('[ShutdownTrace]')) { [void]$watchLines.Add($parts[$index]) } }
                $watch = Get-TraceAnalysis $watchLines.ToArray() $child.Id $now $clockFrequency
                if ($null -ne $watch.accepted_close_to_observed_death_ms -and $watch.accepted_close_to_observed_death_ms -ge 5000) {
                    Write-Host ('本轮接纳关闭已超过 5 秒，进程仍存活；最后活动阶段：{0}。继续等待原退出监督。' -f $watch.last_active_stage)
                    $noticed = $true
                }
                $nextSample = $now + $clockFrequency
            }
        }
        $drain = [Diagnostics.Stopwatch]::StartNew()
        $round.stream_drain_complete = [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($copyOut, $copyError), 10000)
        $round.stream_drain_ms = $drain.Elapsed.TotalMilliseconds
        if (-not $round.stream_drain_complete) { $round.capture_errors += 'Streams did not drain within 10 seconds; possible inherited handles. Raw partial logs retained.' }
    } catch {
        $round.capture_errors += $_.Exception.Message
        $round.status = 'capture-failed'
    } finally {
        # Ctrl+C 仅结束采集，不结束被测程序、其他 Inkeys 或 PPT。
        if ($null -eq $round.observed_death_qpc -and $round.status -eq 'running') { $round.status = 'interrupted' }
        if ($null -ne $tail) { $tail.Dispose() }
        if ($null -ne $stdout) { $stdout.Dispose() }
        if ($null -ne $stderr) { $stderr.Dispose() }
        $child.Dispose()
        $lines = @()
        foreach ($name in @('stdout.log', 'stderr.log')) {
            $path = Join-Path $roundRoot $name
            if ([IO.File]::Exists($path)) { $lines += [IO.File]::ReadAllLines($path, $utf8) }
        }
        $round.analysis = Get-TraceAnalysis $lines ([long]$round.pid) $round.observed_death_qpc $clockFrequency
        if (-not $Mock -and $null -ne $round.started_utc -and [IO.Directory]::Exists($nativeLogRoot)) {
            try {
                $destination = Join-Path $roundRoot 'native'; [void][IO.Directory]::CreateDirectory($destination)
                $since = [DateTimeOffset]::Parse($round.started_utc).UtcDateTime
                foreach ($log in @(Get-ChildItem -LiteralPath $nativeLogRoot -File -Filter 'idt*.log' | Where-Object { $_.LastWriteTimeUtc -ge $since })) {
                    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $destination $log.Name)
                    $round.copied_native_logs += $log.Name
                }
            } catch { $round.capture_errors += $_.Exception.Message }
        }
        $round.capture_ended_utc = [DateTimeOffset]::UtcNow.ToString('o')
        Save-Round $round
    }
    if ($PromptForNotes -and -not $Mock) { $round.notes = Read-Host '可选：本轮操作或异常现象（直接回车跳过）'; Save-Round $round }
    $summary = Get-RoundSummary $round
    Write-Host ('第 {0} 轮已记录：exit={1}，关闭接纳到死亡观测={2} ms，slow={3}，incomplete={4}' -f $Number, $round.exit_hex, $summary.accepted_close_to_observed_death_ms, $summary.slow, $summary.incomplete)
    return $round
}

function Invoke-Series([int]$Count, [string]$MockScript = '') {
    $mock = -not [string]::IsNullOrEmpty($MockScript)
    try {
        $session.status = 'running'; Save-Session
        for ($number = 1; $number -le $Count; ++$number) {
            if (-not $mock) {
                $deadline = [DateTime]::UtcNow.AddSeconds(3)
                $existing = @(Get-RunningInkeys)
                while ($existing.Count -and $number -gt 1 -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200; $existing = @(Get-RunningInkeys) }
                if ($existing.Count) {
                    $session.status = 'stopped-existing-instance'; $session.existing_inkeys = $existing
                    if ($roundRecords.Count) { $roundRecords[$roundRecords.Count - 1].followup_processes = $existing; Save-Round $roundRecords[$roundRecords.Count - 1] }
                    Write-Warning '仍有 Inkeys 实例（可能发生重启/提权交接），已停止自动启动下一轮。请保留记录并自行关闭现有程序。'
                    break
                }
                if ((Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash -ne $session.exe_sha256 -or (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash -ne $session.pptcom_sha256) {
                    $session.status = 'stopped-version-changed'; Write-Warning 'EXE 或 DLL 在轮次之间发生变化；为避免混用版本，采集已停止。'; break
                }
            }
            $image = if ($mock) { $session.powershell_executable } else { $exePath }
            $arguments = if ($mock) { '-NoLogo -NoProfile -NonInteractive -File "' + $MockScript + '" -Round ' + $number } else { '' }
            $result = Invoke-CaptureRound $number $image $arguments $mock
            if (-not $mock -and $null -ne $result.observed_death_qpc) {
                # 最后一轮也检查后继实例，不能把重启/提权 launcher 的退出记作干净关闭。
                $followupDeadline = [DateTime]::UtcNow.AddSeconds(3)
                $followups = @(Get-RunningInkeys)
                while ($followups.Count -and [DateTime]::UtcNow -lt $followupDeadline) { Start-Sleep -Milliseconds 200; $followups = @(Get-RunningInkeys) }
                if ($followups.Count) {
                    $result.followup_processes = $followups; Save-Round $result
                    $session.status = 'stopped-existing-instance'; $session.existing_inkeys = $followups
                    Write-Warning '本轮退出后仍有 Inkeys 实例，不能确认干净关闭；已停止后续轮次，不会自动关闭现有程序。'
                    break
                }
            }
            if ($null -eq $result.observed_death_qpc -or -not $result.stream_drain_complete) { $session.status = 'stopped-incomplete-capture'; break }
            if ($null -eq $result.analysis.accepted_close_qpc) { $session.status = 'stopped-no-close-marker'; Write-Warning '没有该 PID 的关闭接纳记录；不能把启动器退出当成正常关闭，已停止下一轮。'; break }
            if ($number -lt $Count) { Start-Sleep -Milliseconds $(if ($mock) { 20 } else { 2000 }) }
        }
        if ($session.status -eq 'running') { $session.status = 'completed' }
    } finally {
        if ($session.status -eq 'running') { $session.status = 'interrupted' }
        $session.ended_utc = [DateTimeOffset]::UtcNow.ToString('o')
        Save-Session
        Write-Host ('报告目录：' + $sessionRoot)
    }
}

function Test-TraceParser {
    $fixture = @(
        '[ShutdownTrace] source=native pid=42 span=1 event=end stage=set-off qpc=120 frequency=1000',
        '[ShutdownTrace] source=native pid=42 span=2 event=begin stage=main-shutdown qpc=110 frequency=1000',
        '[ShutdownTrace] source=native pid=99 span=1 event=begin stage=set-off qpc=1 frequency=1000',
        '[PptPerf] source=managed pid=42 span=1 event=begin stage=cleanup-rebind qpc=10 frequency=1000',
        '[PptPerf] source=managed pid=42 span=1 event=end stage=cleanup-rebind qpc=40 frequency=1000',
        '[PptPerf] source=managed pid=42 span=2 event=begin stage=shutdown-managed-finally qpc=130 frequency=1000',
        '[PptPerf] source=managed pid=42 span=2 event=end stage=shutdown-managed-finally qpc=170 frequency=1000',
        '[ShutdownTrace] source=native pid=42 span=2 event=end stage=main-shutdown qpc=200 frequency=1000',
        '[ShutdownTrace] source=native pid=42 span=1 event=begin stage=set-off qpc=100 frequency=1000'
    )
    $value = Get-TraceAnalysis $fixture 42 250 1000
    if ($value.accepted_close_to_observed_death_ms -ne 150 -or $value.main_shutdown_ms -ne 90 -or $value.after_main_shutdown_ms -ne 50 -or $value.native_spans.Count -ne 2 -or $value.managed_cleanup_spans.Count -ne 2 -or $value.incomplete -or $value.last_stage -ne 'main-shutdown') { throw 'Namespace/PID/QPC pairing test failed.' }
    if (($value.managed_cleanup_spans | Where-Object stage -eq 'cleanup-rebind').in_shutdown_window) { throw 'Startup rebind was attributed to shutdown.' }
    $missing = Get-TraceAnalysis ($fixture | Where-Object { $_ -notmatch 'event=end stage=main-shutdown' }) 42 250 1000
    if (-not $missing.incomplete -or $missing.unfinished_native_spans.Count -ne 1) { throw 'Missing end was treated as success.' }
    foreach ($extra in @(
        @('[ShutdownTrace] source=native pid=42 span=3 event=end stage=orphan qpc=220 frequency=1000'),
        @('[ShutdownTrace] source=native pid=42 span=3 event=begin stage=mismatch qpc=130 frequency=1000', '[ShutdownTrace] source=native pid=42 span=3 event=end stage=mismatch qpc=160 frequency=2000'),
        @('[ShutdownTrace] source=native pid=42 span=3 event=begin stage=reversed qpc=160 frequency=1000', '[ShutdownTrace] source=native pid=42 span=3 event=end stage=reversed qpc=130 frequency=1000'),
        @('[ShutdownTrace] source=native pid=42 span=3 event=end stage=bad-clock qpc=220 frequency=0')
    )) {
        $invalid = Get-TraceAnalysis ($fixture + $extra) 42 250 1000
        if (-not $invalid.incomplete -or $invalid.invalid_native_spans.Count -ne 1) { throw 'Invalid/end-only native span was treated as complete.' }
    }
    $openFinal = Get-TraceAnalysis ($fixture + '[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-final qpc=95 frequency=1000') 42 250 1000
    if (-not $openFinal.incomplete -or $openFinal.unfinished_managed_final_spans.Count -ne 1) { throw 'Managed final cleanup began before native Capture but was not attributed.' }
    $closedFinal = Get-TraceAnalysis ($fixture + @('[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-final qpc=95 frequency=1000', '[PptPerf] source=managed pid=42 span=3 event=end stage=cleanup-final qpc=105 frequency=1000')) 42 250 1000
    if ($closedFinal.incomplete -or -not ($closedFinal.managed_cleanup_spans | Where-Object span -eq '3').attributed_to_final_shutdown) { throw 'Overlapping managed final cleanup was not attributed.' }
    $oldRebind = Get-TraceAnalysis ($fixture + '[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-rebind qpc=5 frequency=1000') 42 250 1000
    if ($oldRebind.incomplete -or $oldRebind.unfinished_managed_final_spans.Count) { throw 'Old rebind-only incompleteness was attributed to close.' }
    foreach ($extra in @(
        @('[PptPerf] source=managed pid=42 span=3 event=end stage=cleanup-final qpc=160 frequency=1000'),
        @('[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-final qpc=130 frequency=1000', '[PptPerf] source=managed pid=42 span=3 event=end stage=cleanup-final qpc=160 frequency=2000'),
        @('[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-final qpc=130 frequency=1000', '[PptPerf] source=managed pid=42 span=3 event=end stage=cleanup-final qpc=90 frequency=1000'),
        @('[PptPerf] source=managed pid=42 span=3 event=begin stage=cleanup-final qpc=130 frequency=0')
    )) {
        $invalid = Get-TraceAnalysis ($fixture + $extra) 42 250 1000
        if (-not $invalid.incomplete -or $invalid.invalid_managed_final_spans.Count -ne 1) { throw 'Invalid managed final cleanup was treated as complete.' }
    }
    $slow = Get-TraceAnalysis $fixture 42 3500 1000
    if (-not $slow.slow) { throw 'Slow close threshold test failed.' }
    $absent = Get-TraceAnalysis @('[ShutdownTrace] source=native pid=99 span=1 event=begin stage=set-off qpc=1 frequency=1000') 42 250 1000
    if ($null -ne $absent.accepted_close_to_observed_death_ms -or -not $absent.incomplete) { throw 'Absent set-off was treated as zero milliseconds.' }
}

$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$sessionRoot = Join-Path $repoRoot ('Build/Validation/shutdown-series-' + $(if ($SelfTest) { 'selftest-' } else { '' }) + $stamp)
[void][IO.Directory]::CreateDirectory($sessionRoot)
$roundRecords = [Collections.Generic.List[object]]::new()
$sourceHead = $null; $sourceDirty = @()
try { $sourceHead = [string](& git -C $repoRoot rev-parse HEAD 2>$null); $sourceDirty = @(& git -C $repoRoot -c submodule.recurse=false status --porcelain --ignore-submodules 2>$null) } catch { }
$session = [ordered]@{
    schema_version = 1; status = 'preparing'; requested_rounds = $(if ($SelfTest) { 2 } else { $Rounds })
    prepare_only = [bool]$PrepareOnly; self_test = [bool]$SelfTest; prompt_for_notes = [bool]$PromptForNotes
    started_utc = [DateTimeOffset]::UtcNow.ToString('o'); ended_utc = $null; repo_root = $repoRoot
    powershell_executable = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    powershell_version = $PSVersionTable.PSVersion.ToString(); execution_policy = [string](Get-ExecutionPolicy)
    executable = $exePath; exe_sha256 = $null; pptcom_sha256 = $null; embedded_pptcom_sha256 = $null; probe_markers_present = $false
    source_head = $sourceHead; source_dirty = $sourceDirty
    qpc_utc_anchor = [ordered]@{ qpc = [Diagnostics.Stopwatch]::GetTimestamp(); frequency = $clockFrequency; utc = [DateTimeOffset]::UtcNow.ToString('o') }
    child_environment = [ordered]@{ INKEYS_PPT_PERF = '1'; INKEYS_PPT_TIMING = '1' }
    metric = 'accepted set-off QPC to collector-observed process death; not click latency or process lifetime'
    slow_threshold_ms = 3000; exit_poll_interval_ms = 200; existing_inkeys = @(); errors = @()
}
Save-Session
try {
    if ($SelfTest) {
        Test-TraceParser
        $mockPath = Join-Path $sessionRoot 'mock-child.ps1'
        [IO.File]::WriteAllText($mockPath, @'
param([int]$Round)
$frequency = [Diagnostics.Stopwatch]::Frequency
function Emit($Span, $Event, $Stage) {
    [Console]::Error.WriteLine(('[ShutdownTrace] source=native pid={0} span={1} event={2} stage={3} qpc={4} frequency={5}' -f $PID, $Span, $Event, $Stage, [Diagnostics.Stopwatch]::GetTimestamp(), $frequency))
}
Start-Sleep -Milliseconds 300
Emit 1 begin set-off
Emit 2 begin main-shutdown
$payload = 'x' * 2048
for ($index = 0; $index -lt 128; ++$index) { [Console]::Out.WriteLine($payload); [Console]::Error.WriteLine($payload) }
if ($Round -eq 2) { Emit 2 end main-shutdown }
Emit 1 end set-off
if ($Round -eq 1) { exit 7 }
exit 0
'@, [Text.UTF8Encoding]::new($true))
        Invoke-Series 2 $mockPath
        $summary = Get-Content -LiteralPath (Join-Path $sessionRoot 'summary.json') -Raw | ConvertFrom-Json
        if ($session.status -ne 'completed' -or $roundRecords.Count -ne 2 -or $summary.rounds.Count -ne 2 -or $summary.rounds[0].exit_code -ne 7 -or $summary.rounds[1].exit_code -ne 0 -or -not $summary.rounds[0].nonzero -or -not $summary.rounds[0].incomplete -or -not $summary.rounds[1].normal -or [IO.Directory]::Exists((Join-Path $sessionRoot 'round-03'))) { throw 'Real child sequence/nonzero/persistence test failed.' }
        foreach ($item in $roundRecords) {
            if (-not $item.stream_drain_complete -or $item.capture_errors.Count -or $null -eq $item.analysis.accepted_close_to_observed_death_ms) { throw 'Real child stream capture failed.' }
            $lifetimeMs = 1000.0 * ($item.observed_death_qpc - $item.launch_qpc) / $clockFrequency
            if ($lifetimeMs - $item.analysis.accepted_close_to_observed_death_ms -lt 250 -or [DateTimeOffset]::Parse($item.capture_ended_utc) -lt [DateTimeOffset]::Parse($item.observed_death_utc)) { throw 'Close latency included preceding lifetime or death was sampled after capture drain.' }
            foreach ($name in @('stdout.log', 'stderr.log')) { if ((Get-Item -LiteralPath (Join-Path (Join-Path $sessionRoot $item.folder) $name)).Length -lt 200000) { throw 'Child stream was truncated.' } }
        }
        Write-Output 'SELF_TEST=PASS (parser + exactly two hidden console children; no Inkeys launch)'
        return
    }
    if (-not [IO.File]::Exists($exePath) -or -not [IO.File]::Exists($dllPath)) { throw '缺少当前 ARM64 Debug EXE 或 PptCOM.dll，请先完成构建。' }
    $session.exe_sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    $session.pptcom_sha256 = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    $session.embedded_pptcom_sha256 = Get-EmbeddedPptHash
    $session.probe_markers_present = Test-ProbeMarkers
    $session.existing_inkeys = @(Get-RunningInkeys)
    if ($session.pptcom_sha256 -ne $session.embedded_pptcom_sha256) { throw 'EXE 内嵌 PptCOM 与当前 DLL 不一致，不能混用版本采集。' }
    if (-not $session.probe_markers_present) { throw 'EXE 缺少退出阶段探针标记。' }
    if ($PrepareOnly) {
        $session.status = 'prepared-only'; Save-Session
        Write-Output ('PREPARE_ONLY=PASS；计划 ' + $Rounds + ' 轮，未启动 Inkeys、未修改配置或永久环境。')
        Write-Output ('记录目录：' + $sessionRoot)
        return
    }
    Write-Host ('开始 {0} 轮手动关闭采集。每轮自行探索界面后点击“关闭软件”，退出后两秒自动启动下一轮。' -f $Rounds)
    Write-Host 'Ctrl+C 只停止采集，不会强制关闭 Inkeys/PPT。耗时从软件接纳关闭起算，阶段存在嵌套，不应相加。'
    Invoke-Series $Rounds
} catch {
    $session.status = 'failed'; $session.errors += $_.Exception.Message
    throw
} finally {
    if ($session.status -eq 'running' -or $session.status -eq 'preparing') { $session.status = 'interrupted' }
    if ($null -eq $session.ended_utc) { $session.ended_utc = [DateTimeOffset]::UtcNow.ToString('o') }
    Save-Session
}
