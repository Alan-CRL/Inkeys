#requires -Version 5.1
param([string]$Root,[string]$CommonPath,[string]$Behavior='complete',[int]$DelayMilliseconds=600)
$ErrorActionPreference='Stop'
. $CommonPath
if ($Behavior -eq 'late') { Start-Sleep -Milliseconds 1800 }
$facts=[Draw3PerfWire]::ProcessIdentity($PID)
$stamp=[long]$facts[1]
if ($Behavior -eq 'wrong_identity') { $stamp+=10000000 }
$identity=@{schema_version=1;session_id=[IO.Path]::GetFileName($Root);final_pid=$PID;process_start_filetime_utc=[string]$stamp;process_start_time=[DateTime]::FromFileTimeUtc($stamp).ToString('o');exe_path=$facts[0];output_directory=$Root;status='started'}
if ($Behavior -eq 'wrong_exe') { $identity.exe_path=Join-Path $Root 'unrelated.exe' }
Write-PerfJson (Join-Path $Root 'launch.json') $identity
if ($Behavior -eq 'double_stream') { for ($index=0;$index -lt 12000;$index++) { [Console]::Out.WriteLine('output-' + $index); [Console]::Error.WriteLine('error-' + $index) } }
Start-Sleep -Milliseconds $DelayMilliseconds
if ($Behavior -ne 'missing_completion') {
    $identity.status='completed'; $identity.reason='fake_normal_exit'; $identity.exit_code=0
    if ($Behavior -eq 'wrong_completion') { $identity.process_start_filetime_utc=[string]($stamp+10000000); $identity.process_start_time=[DateTime]::FromFileTimeUtc($stamp+10000000).ToString('o') }
    Write-PerfJson (Join-Path $Root 'completion.json') $identity
}
exit 0
