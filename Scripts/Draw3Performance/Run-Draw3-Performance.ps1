param(
    [string]$ExePath = '',
    [string]$OutputRoot = '',
    [string]$ManifestPath = '',
    [int]$LaunchTimeoutSeconds = 180,
    [int]$CaptureTimeoutSeconds = 0,
    [int]$HardwareTimeoutSeconds = 15,
    [switch]$Zip,
    [switch]$SkipResourceProbes,
    [string]$HardwareQuery = '',
    [string]$CaptureRoot = ''
)
$ErrorActionPreference = 'Stop'
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$collectorScriptPath = $MyInvocation.MyCommand.Path
. (Join-Path $scriptDirectory 'Performance.Common.ps1')

function Get-HardwareQuery([string]$Name) {
    $definitions = @{
        computer = @('Win32_ComputerSystem', @('Manufacturer','Model','TotalPhysicalMemory','NumberOfLogicalProcessors'))
        cpu = @('Win32_Processor', @('Name','Manufacturer','Architecture','NumberOfCores','NumberOfLogicalProcessors','MaxClockSpeed','CurrentClockSpeed'))
        os = @('Win32_OperatingSystem', @('Caption','Version','BuildNumber','ServicePackMajorVersion','OSArchitecture','TotalVisibleMemorySize','FreePhysicalMemory'))
        gpu = @('Win32_VideoController', @('Name','PNPDeviceID','DriverVersion','DriverDate','AdapterRAM','CurrentHorizontalResolution','CurrentVerticalResolution','CurrentRefreshRate','Status'))
        monitor = @('Win32_DesktopMonitor', @('Name','ScreenWidth','ScreenHeight','PixelsPerXLogicalInch','PixelsPerYLogicalInch','Status'))
    }
    if ($Name -eq 'power') {
        $powerTool = Join-Path $env:SystemRoot 'System32\powercfg.exe'
        $text = @(& $powerTool /getactivescheme 2>&1)
        if ($LASTEXITCODE -ne 0) { throw "powercfg 退出码 $LASTEXITCODE" }
        return @{query=$Name; status='available'; raw=($text -join [Environment]::NewLine); errors=@()}
    }
    if (-not $definitions.ContainsKey($Name)) { throw '未知硬件查询' }
    $className = $definitions[$Name][0]; $fields = $definitions[$Name][1]
    Add-Type -AssemblyName System.Management
    $searcher = New-Object System.Management.ManagementObjectSearcher(('SELECT ' + ($fields -join ',') + ' FROM ' + $className))
    $values = New-Object Collections.ArrayList
    $errors = New-Object Collections.ArrayList
    try {
        $collection = $searcher.Get()
        try {
            foreach ($item in $collection) {
                $row = @{}
                foreach ($field in $fields) {
                    try {
                        $value = $item[$field]
                        if ($null -eq $value) { [void]$errors.Add(@{field=$field; reason='WMI 返回 null'}); $row[$field]=$null }
                        elseif ($value -is [string] -or $value -is [bool]) { $row[$field]=$value }
                        else { $row[$field]=[string]$value }
                    } catch { $row[$field]=$null; [void]$errors.Add(@{field=$field; reason=$_.Exception.Message}) }
                }
                [void]$values.Add($row); $item.Dispose()
            }
        } finally { $collection.Dispose() }
    } finally { $searcher.Dispose() }
    $status = 'available'
    if ($values.Count -eq 0) { $status='unavailable'; [void]$errors.Add(@{field='*'; reason='WMI 无记录'}) }
    elseif ($errors.Count -gt 0) { $status='partial' }
    return @{query=$Name; status=$status; values=$values; errors=$errors; note='WMI 快照；CPU 当前频率、显存/显示值不能替代实测或软件实际设备/显示配置'}
}

if ($HardwareQuery) {
    try { $value = Get-HardwareQuery $HardwareQuery }
    catch { $value=@{query=$HardwareQuery; status='unavailable'; errors=@(@{field='*'; reason=$_.Exception.Message})} }
    Write-PerfJson (Join-Path $CaptureRoot ('hardware-' + $HardwareQuery + '.json')) $value
    exit 0
}

function Collect-Hardware([string]$Root, [int]$TimeoutSeconds) {
    $queries = New-Object Collections.ArrayList
    $workerExe = Join-Path $PSHOME 'powershell.exe'
    if (-not [IO.File]::Exists($workerExe)) { $workerExe=Join-Path $PSHOME 'pwsh.exe' }
    foreach ($name in @('computer','cpu','os','gpu','monitor','power')) {
        $resultPath = Join-Path $Root ('hardware-' + $name + '.json')
        $worker = $null
        try {
            $arguments = '-NoLogo -NoProfile -ExecutionPolicy Bypass -File "' + $collectorScriptPath + '" -HardwareQuery ' + $name + ' -CaptureRoot "' + $Root + '"'
            $worker = Start-Process -FilePath $workerExe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Root ('hardware-' + $name + '.stdout.txt')) -RedirectStandardError (Join-Path $Root ('hardware-' + $name + '.stderr.txt'))
            $workerHandle = $worker.Handle
            if (-not $worker.WaitForExit($TimeoutSeconds * 1000)) {
                # 截止只针对本脚本刚创建、持有句柄的硬件 worker，绝不终止软件实例。
                $worker.Kill(); if (-not $worker.WaitForExit(5000)) { throw '硬件 worker 终止未确认' }
                Write-PerfJson $resultPath @{query=$name; status='timeout'; errors=@(@{field='*'; reason=('硬件 worker 超过 ' + $TimeoutSeconds + ' 秒')})}
            }
            if (-not [IO.File]::Exists($resultPath)) { throw 'worker 未导出结果' }
            [void]$queries.Add((Read-PerfJson $resultPath))
        } catch { [void]$queries.Add(@{query=$name; status='unavailable'; errors=@(@{field='*'; reason=$_.Exception.Message})}) }
        finally {
            if ($worker) {
                try {
                    if (-not $worker.HasExited) { $worker.Kill(); if (-not $worker.WaitForExit(5000)) { throw '硬件 worker 未停止；为避免干扰性能测试，软件尚未启动' } }
                } finally { $worker.Dispose() }
            }
        }
    }
    $dlls=New-Object Collections.ArrayList
    foreach ($name in @('d3d11.dll','dxgi.dll','d2d1.dll','dwrite.dll','dcomp.dll','RTSCom.dll')) {
        $path=Join-Path $env:SystemRoot ('System32\' + $name)
        try { if (-not [IO.File]::Exists($path)) { throw '文件不可用' }; [void]$dlls.Add(@{name=$name; status='available'; file_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($path).FileVersion}) }
        catch { [void]$dlls.Add(@{name=$name; status='unavailable'; reason=$_.Exception.Message}) }
    }
    Write-PerfJson (Join-Path $Root 'hardware.json') @{
        schema_version=1; collected_utc=[DateTime]::UtcNow.ToString('o'); queries=$queries; system_dlls=$dlls;
        collector_ps_version=[string]$PSVersionTable.PSVersion; collector_clr_version=[string][Environment]::Version;
        collector_process_bits=([IntPtr]::Size*8); processor_architecture=$env:PROCESSOR_ARCHITECTURE;
        remote_session_environment=$env:SESSIONNAME; actual_draw3_environment_source='host-NNNN/initialization.json';
        dpi=@{status='see_application_snapshot'; reason='WMI logical DPI 不是软件实际目标 DPI；以软件实际显示初始化证据为准'}
    }
}

function Copy-CurrentLogs([string]$Root, [string]$Executable, [DateTime]$StartedUtc, [string]$OwnerLog) {
    $copies=New-Object Collections.ArrayList; $budget=16777216L; $copied=0L; $count=0
    $destination=Join-Path $Root 'ordinary-logs'
    [void][IO.Directory]::CreateDirectory($destination)
    $exeDirectory=Split-Path -Parent $Executable
    $copiedPaths=@{}
    if ($OwnerLog) {
        try {
            $ownerPath=[IO.Path]::GetFullPath($OwnerLog)
            $allowed=$false
            foreach ($parent in @((Join-Path $exeDirectory 'log'),(Join-Path $exeDirectory 'Inkeys\log'))) { if ($ownerPath.StartsWith($parent.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { $allowed=$true } }
            if (-not $allowed -or [IO.Path]::GetFileName($ownerPath) -notlike 'idt*.log') { throw 'owner 日志路径不在软件日志目录' }
            $info=New-Object IO.FileInfo($ownerPath)
            if (-not $info.Exists -or $info.Length -gt $budget) { throw 'owner 日志缺失或超过复制预算' }
            [IO.File]::Copy($ownerPath,(Join-Path $destination ('00-'+$info.Name)),$false)
            $copiedPaths[$ownerPath]=$true; $count++; $copied+=$info.Length
            [void]$copies.Add(@{name=$info.Name;status='copied_final_owner';bytes=$info.Length})
        } catch { [void]$copies.Add(@{name=$OwnerLog;status='owner_log_unavailable';reason=$_.Exception.Message}) }
    }
    foreach ($directory in @((Join-Path $exeDirectory 'log'),(Join-Path $exeDirectory 'Inkeys\log'))) {
        if (-not [IO.Directory]::Exists($directory)) { continue }
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Filter 'idt*.log' -ErrorAction SilentlyContinue)) {
            if ($copiedPaths.ContainsKey($item.FullName)) { continue }
            if ($item.LastWriteTimeUtc -lt $StartedUtc.AddSeconds(-1)) { continue }
            if ($count -ge 32 -or ($copied + $item.Length) -gt $budget) { [void]$copies.Add(@{name=$item.Name; status='skipped_budget'; bytes=$item.Length}); continue }
            try { $target=Join-Path $destination ($count.ToString('D2') + '-' + $item.Name); [IO.File]::Copy($item.FullName,$target,$false); $copied+=$item.Length; $count++; [void]$copies.Add(@{name=$item.Name; status='copied_by_time_window'; bytes=$item.Length}) }
            catch { [void]$copies.Add(@{name=$item.Name; status='unavailable'; reason=$_.Exception.Message}) }
        }
    }
    Write-PerfJson (Join-Path $Root 'ordinary-log-index.json') @{files=$copies; byte_budget=$budget; note='普通日志仅按本轮时间窗口辅助收集；不是最终进程身份或呈现成功证据'}
}

$resultRoot=''; $bootstrap=$null; $collectorStatus=$null
try {
    if ($LaunchTimeoutSeconds -lt 1 -or $LaunchTimeoutSeconds -gt 3600 -or $HardwareTimeoutSeconds -lt 1 -or $HardwareTimeoutSeconds -gt 120 -or $CaptureTimeoutSeconds -lt 0) { throw '超时参数无效' }
    if (-not $ExePath) { $ExePath=Join-Path $scriptDirectory 'Inkeys.exe' }
    $ExePath=[IO.Path]::GetFullPath($ExePath)
    if (-not [IO.File]::Exists($ExePath)) { throw '未找到 EXE；请传入路径或将诊断包放在软件旁' }
    if (-not $ManifestPath) { $ManifestPath=Join-Path $scriptDirectory 'diagnostic-manifest.json' }
    $manifest=Read-PerfJson $ManifestPath
    Test-PerfPackageManifest $manifest $ExePath $scriptDirectory
    $exeHash=Get-PerfSha256 $ExePath
    $names=@('Inkeys','Inkeys64','InkeysArm64',[IO.Path]::GetFileNameWithoutExtension($ExePath))
    foreach ($name in $names) { if ([Diagnostics.Process]::GetProcessesByName($name).Length -gt 0) { throw "已有 $name 进程。请正常退出该实例后再运行诊断包。" } }
    if (-not $OutputRoot) { $OutputRoot=Join-Path $scriptDirectory 'Inkeys-Draw3-Diagnostics' }
    $OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
    [void][IO.Directory]::CreateDirectory($OutputRoot)
    $sessionId='draw3-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N')
    $resultRoot=Join-Path $OutputRoot $sessionId
    if ([IO.Directory]::Exists($resultRoot)) { throw '会话目录已经存在' }
    [void][IO.Directory]::CreateDirectory($resultRoot)
    $startedUtc=[DateTime]::UtcNow
    Write-PerfJson (Join-Path $resultRoot 'collector-session.json') @{schema_version=1; capture_schema=2; package_revision=3; skip_resource_probes=[bool]$SkipResourceProbes; session_id=$sessionId; started_utc=$startedUtc.ToString('o'); exe_path=$ExePath; exe_sha256=$exeHash; exe_file_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($ExePath).FileVersion; exe_architecture=$manifest['exe_architecture']; argv=@('--draw3-performance',$resultRoot); package_id=$manifest['package_id']; launch_timeout_seconds=$LaunchTimeoutSeconds; capture_timeout_seconds=$CaptureTimeoutSeconds}
    [IO.File]::Copy([IO.Path]::GetFullPath($ManifestPath),(Join-Path $resultRoot 'diagnostic-manifest.json'),$false)
    [IO.File]::WriteAllText((Join-Path $resultRoot 'observations.txt'),"请记录卡顿发生的时间、工具、动作、是否刚从空闲恢复、是否切页/撤销，以及体感不跟手的描述。此文件属于人工观察。" + [Environment]::NewLine,(New-Object Text.UTF8Encoding($true)))
    Write-Host '正在测试前收集硬件快照，完成后这些查询会全部退出。'
    Collect-Hardware $resultRoot $HardwareTimeoutSeconds
    Write-Host '诊断包 3：软件即将启动。正常书写并复现卡顿，完成后从软件正常退出。'
    Write-Host '软件退出后自动执行最多 8 分钟的无窗口资源对照。完成前请保留本窗口，随后自动生成报告。'
    Write-Host ('结果目录: ' + $resultRoot)
    $argumentText='--draw3-performance "' + $resultRoot.TrimEnd('\') + '"'
    $bootstrap=Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -ArgumentList $argumentText -PassThru -RedirectStandardOutput (Join-Path $resultRoot 'bootstrap.stdout.txt') -RedirectStandardError (Join-Path $resultRoot 'bootstrap.stderr.txt')
    $bootstrapHandle=$bootstrap.Handle
    $collectorStatus=Wait-PerfFinalProcess $resultRoot $ExePath $LaunchTimeoutSeconds $CaptureTimeoutSeconds
    $bootstrapExit=$null
    if ($bootstrap.HasExited) { $bootstrap.WaitForExit(); $bootstrapExit=$bootstrap.ExitCode }
    $collectorStatus['bootstrap_pid']=$bootstrap.Id; $collectorStatus['bootstrap_exit_code']=$bootstrapExit; $collectorStatus['ended_utc']=[DateTime]::UtcNow.ToString('o')
    Write-PerfJson (Join-Path $resultRoot 'collector-status.json') $collectorStatus
    $ownerLog=''; if ($collectorStatus['completion']) { $ownerLog=[string]$collectorStatus['completion']['log_file'] }
    Copy-CurrentLogs $resultRoot $ExePath $startedUtc $ownerLog
    $normalShutdown=$false
    if ($collectorStatus['completion'] -and $collectorStatus['completion']['normal_shutdown'] -is [bool]) { $normalShutdown=$collectorStatus['completion']['normal_shutdown'] -eq $true }
    Write-Host '正在检查最终产品停止证据并执行独立资源矩阵；单项失败会保留结果并继续。'
    $matrix=Invoke-PerfResourceMatrix $resultRoot $ExePath $sessionId $exeHash ($collectorStatus['product_stopped_confirmed'] -eq $true) $normalShutdown ([bool]$SkipResourceProbes) 20 480 @() $names
    Write-Host ('资源矩阵状态: '+$matrix.status+'；'+$matrix.reason)
    # 软件仍运行时不读尚未封口的原始数据，离线报告只能保留 incomplete。
    & (Join-Path $scriptDirectory 'Analyze-Draw3-Performance.ps1') -CaptureRoot $resultRoot
    $analysisExit=$LASTEXITCODE
    if ($Zip) {
        if (Get-Command Compress-Archive -ErrorAction SilentlyContinue) {
            try { Compress-Archive -LiteralPath $resultRoot -DestinationPath ($resultRoot + '.zip') -ErrorAction Stop; Write-Host ('压缩包: ' + $resultRoot + '.zip') }
            catch { Write-PerfJson (Join-Path $resultRoot 'zip-status.json') @{status='unavailable'; reason=$_.Exception.Message} }
        } else { Write-PerfJson (Join-Path $resultRoot 'zip-status.json') @{status='unavailable'; reason='此 PowerShell 没有 Archive 模块；完整结果目录仍可手动压缩'} }
    }
    Write-Host ('采集状态: ' + $collectorStatus['status'] + '；请查看 report.md')
    if ($collectorStatus['status'] -ne 'completed' -or $analysisExit -ne 0) { exit 2 }
    exit 0
} catch {
    Write-Host ('诊断包错误: ' + $_.Exception.Message)
    if ($resultRoot) { Write-PerfJson (Join-Path $resultRoot 'collector-status.json') @{status='failed'; reason=$_.Exception.Message; ended_utc=[DateTime]::UtcNow.ToString('o')} }
    exit 1
} finally { if ($bootstrap) { $bootstrap.Dispose() } }
