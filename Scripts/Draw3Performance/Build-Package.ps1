#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ExePath,[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$scriptDirectory=Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDirectory 'Performance.Common.ps1')
$ExePath=[IO.Path]::GetFullPath($ExePath)
if (-not [IO.File]::Exists($ExePath)) { throw 'EXE 不存在' }
# 只检查已构建文件，不运行旧 EXE 探测能力，避免未知参数启动 GUI。
$architecture=Get-PerfExeArchitecture $ExePath
Test-PerfV3Capability $ExePath
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $scriptDirectory '..\..\Build\Draw3PerformancePackages' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$packageId='Draw3Performance-v3-'+$architecture+'-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N')
$folder=Join-Path $OutputDirectory $packageId
if ([IO.Directory]::Exists($folder)) { throw '包目录已存在' }
[void][IO.Directory]::CreateDirectory($folder)
[IO.File]::Copy($ExePath,(Join-Path $folder 'Inkeys.exe'),$false)
$hashes=@{}
foreach ($name in @('Run-Draw3-Performance.cmd','Run-Draw3-Performance.ps1','Analyze-Draw3-Performance.ps1','Performance.Common.ps1','README.md')) {
    [IO.File]::Copy((Join-Path $scriptDirectory $name),(Join-Path $folder $name),$false)
    $hashes[$name]=Get-PerfSha256 (Join-Path $folder $name)
}
Write-PerfJson (Join-Path $folder 'diagnostic-manifest.json') @{
    schema_version=1;capture_schema=2;diagnostic_package_version=3;package_revision=3;capability_marker='INKEYS_DRAW3_DIAGNOSTICS_V3';
    package_id=$packageId;created_utc=[DateTime]::UtcNow.ToString('o');command_line='--draw3-performance';
    resource_probe_command='--draw3-resource-probe';resource_probe_list_command='--draw3-resource-probe-list';resource_probe_schema=1;probe_revision=3;resource_probe_profile='draw3-resource-v3-fixed';
    probe_limits=@{case_timeout_seconds=20;matrix_timeout_seconds=480;case_count=56;native_budget_ms=8000;call_capacity=4096;stdout_bytes=1048576;stderr_bytes=1048576;raw_file_bytes=4194304;matrix_disk_bytes=67108864};
    exe_sha256=(Get-PerfSha256 (Join-Path $folder 'Inkeys.exe'));exe_architecture=$architecture;exe_file_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($ExePath).FileVersion;script_sha256=$hashes;
    raw_schemas=@{'session.json'=2;'initialization.json'=1;'frames.csv'=2;'input-events.csv'=1;'gpu.csv'=2;'resources.csv'=1;'graphics-calls.csv'=1;'slow-graphics-calls.csv'=1;'graphics-groups.csv'=1};
    entry_check='PE/exact V3 sentinel 静态匹配；不等于实际运行验证';user_powershell_minimum='2.0';package_builder_powershell_minimum='5.1'
}
$zipPath=$folder+'.zip'
Compress-Archive -LiteralPath $folder -DestinationPath $zipPath -ErrorAction Stop
Write-Host ('诊断包目录: '+$folder)
Write-Host ('诊断包 ZIP: '+$zipPath)
