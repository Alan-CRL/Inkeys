[CmdletBinding()]
param([switch]$PrepareOnly)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot 'Build/ARM64/Debug/Inkeys.exe'
$dllPath = Join-Path $repoRoot 'Inkeys/PptCOM.dll'
$nativeLogRoot = Join-Path (Split-Path -Parent $exePath) 'log'
$utf8 = [Text.UTF8Encoding]::new($false)

function Write-RunMetadata {
    [IO.File]::WriteAllText((Join-Path $runRoot 'manifest.json'), ($metadata | ConvertTo-Json -Depth 8), $utf8)
}

function Test-NativePerfMarker {
    # 分块只读检查固定诊断标记；不把已有的旧 EXE 当作本轮诊断版本。
    $stream = [IO.File]::OpenRead($exePath)
    try {
        $buffer = [byte[]]::new(65536)
        $tail = ''
        $needle = '[PptPerf] v=1 source=native'
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $chunk = $tail + [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
            if ($chunk.IndexOf($needle, [StringComparison]::Ordinal) -ge 0) { return $true }
            $tail = $chunk.Substring([Math]::Max(0, $chunk.Length - $needle.Length + 1))
        }
        return $false
    } finally { $stream.Dispose() }
}

function Get-EmbeddedPptHash {
    # 只按数据资源读取 EXE，不执行入口或初始化产品窗口。
    if (-not ('PptDiagnosticResource' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PptDiagnosticResource {
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
    $header = [IO.File]::ReadAllText((Join-Path $repoRoot 'Inkeys/Resource.h'))
    $match = [regex]::Match($header, '(?m)^#define\s+IDR_DLL1\s+(\d+)')
    if (-not $match.Success) { throw 'Cannot locate IDR_DLL1 in Resource.h.' }
    $bytes = [PptDiagnosticResource]::Read($exePath, [int]$match.Groups[1].Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '') }
    finally { $sha.Dispose() }
}

if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) { throw "请先构建诊断版：$exePath" }
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) { throw "缺少 PptCOM.dll：$dllPath" }
$running = @(Get-Process -Name Inkeys -ErrorAction SilentlyContinue | ForEach-Object {
    $path = $null
    try { $path = $_.Path } catch { }
    [ordered]@{ pid = $_.Id; path = $path }
})
if ($running.Count -gt 0 -and -not $PrepareOnly) {
    throw ('请先自行正常退出正在运行的 Inkeys，再启动诊断（不会自动关闭或强杀）。PID：' + (($running | ForEach-Object { $_.pid }) -join ', '))
}
$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $repoRoot ('Build/Validation/ppt-navigation-' + $stamp)
[void][IO.Directory]::CreateDirectory($runRoot)
$gitHead = $null
try {
    $headOutput = & git -C $repoRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -eq 0) { $gitHead = [string]$headOutput }
} catch { }
$anchorQpc = [Diagnostics.Stopwatch]::GetTimestamp()
$anchorUtc = [DateTimeOffset]::UtcNow.ToString('o')
$metadata = [ordered]@{
    schema_version = 1
    status = 'preparing'
    prepare_only = [bool]$PrepareOnly
    repo_root = $repoRoot
    powershell_executable = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    powershell_version = $PSVersionTable.PSVersion.ToString()
    execution_policy = [string](Get-ExecutionPolicy)
    executable = $exePath
    working_directory = Split-Path -Parent $exePath
    source_head = $gitHead
    exe_sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    pptcom_sha256 = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    embedded_pptcom_sha256 = $null
    native_perf_marker_present = Test-NativePerfMarker
    qpc_utc_anchor = [ordered]@{ qpc = $anchorQpc; frequency = [Diagnostics.Stopwatch]::Frequency; utc = $anchorUtc }
    existing_inkeys = $running
    child_environment = [ordered]@{ INKEYS_PPT_PERF = '1'; INKEYS_PPT_TIMING = '1' }
    pid = $null
    exit_code = $null
    started_utc = $null
    ended_utc = $null
    native_log_root = $nativeLogRoot
    copied_native_logs = @()
    capture_errors = @()
}
Write-RunMetadata
try {
    $metadata.embedded_pptcom_sha256 = Get-EmbeddedPptHash
    if ($metadata.embedded_pptcom_sha256 -ne $metadata.pptcom_sha256) {
        throw 'EXE 内嵌 PptCOM.dll 与当前 DLL 不一致，请重新交接并构建，避免误测旧版本。'
    }
    if ($PrepareOnly) {
        $metadata.status = 'prepared-only'
        Write-RunMetadata
        Write-Output "PREPARE_ONLY=PASS（未启动 GUI、未修改配置或环境）"
        Write-Output "记录目录：$runRoot"
        if (-not $metadata.native_perf_marker_present) { Write-Warning '当前 EXE 尚无 PptPerf 标记；正式测试前须完成诊断版构建。' }
        return
    }
    if (-not $metadata.native_perf_marker_present) { throw '当前 EXE 没有 PptPerf 诊断标记，请先构建本轮诊断版。' }

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exePath
    $info.WorkingDirectory = Split-Path -Parent $exePath
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # 仅写子进程环境，不修改用户配置、当前/永久环境，不依赖 PptCOM Console 开关。
    $info.EnvironmentVariables['INKEYS_PPT_PERF'] = '1'
    $info.EnvironmentVariables['INKEYS_PPT_TIMING'] = '1'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $outFile = [IO.File]::Open((Join-Path $runRoot 'stdout.log'), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $errFile = [IO.File]::Open((Join-Path $runRoot 'stderr.log'), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $metadata.started_utc = [DateTimeOffset]::UtcNow.ToString('o')
        if (-not $process.Start()) { throw 'Inkeys did not start.' }
        $metadata.pid = $process.Id
        $metadata.status = 'running'
        Write-RunMetadata
        # 两条流并行抽取，避免其中一条管道填满反过来阻塞被测进程。
        $outCopy = $process.StandardOutput.BaseStream.CopyToAsync($outFile)
        $errCopy = $process.StandardError.BaseStream.CopyToAsync($errFile)
        Write-Host "诊断版已启动，PID=$($process.Id)。请保留此采集窗口。"
        Write-Host '请自行打开指定 PPT，对比控件和键盘，重点反复 3↔4 / Previous；记下卡顿方式和大致时间。'
        Write-Host '完成后正常退出 Inkeys，日志会自动收集；无需关闭或修改 PPT。'
        Write-Host "本轮目录：$runRoot"
        while (-not $process.WaitForExit(500)) { }
        $process.WaitForExit()
        [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outCopy, $errCopy))
        $metadata.exit_code = $process.ExitCode
        $metadata.status = if ($process.ExitCode -eq 0) { 'exited' } else { 'exited-nonzero' }
    } finally {
        $metadata.ended_utc = [DateTimeOffset]::UtcNow.ToString('o')
        if ($metadata.status -eq 'running') { $metadata.status = 'interrupted-or-incomplete' }
        $outFile.Dispose()
        $errFile.Dispose()
        $process.Dispose()
    }
} catch {
    $metadata.capture_errors += $_.Exception.Message
    $metadata.status = 'capture-failed'
    throw
} finally {
    # 复制本轮创建/更新的原生日志作为快照；不移动原文件，不清理历史记录。
    if ($null -ne $metadata.started_utc -and (Test-Path -LiteralPath $nativeLogRoot)) {
        try {
            $startUtc = [DateTimeOffset]::Parse($metadata.started_utc).UtcDateTime
            $nativeOutput = Join-Path $runRoot 'native'
            [void][IO.Directory]::CreateDirectory($nativeOutput)
            foreach ($log in @(Get-ChildItem -LiteralPath $nativeLogRoot -File -Filter 'idt*.log' | Where-Object { $_.LastWriteTimeUtc -ge $startUtc })) {
                Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $nativeOutput $log.Name)
                $metadata.copied_native_logs += $log.Name
            }
        } catch { $metadata.capture_errors += $_.Exception.Message }
    }
    Write-RunMetadata
    Write-Output "诊断采集状态：$($metadata.status)"
    Write-Output "日志目录：$runRoot"
}
if ($null -ne $metadata.exit_code -and $metadata.exit_code -ne 0) {
    Write-Warning "Inkeys 异常退出（$($metadata.exit_code)），日志仍已保留。"
    exit $metadata.exit_code
}
if ($metadata.capture_errors.Count -gt 0) {
    Write-Warning '部分日志未能收集，请保留 manifest.json 和现有日志。'
    exit 2
}
