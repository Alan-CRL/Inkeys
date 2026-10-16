param([string[]]$Behaviors=@('success','partial','drain_incomplete','shader_mismatch','adapter_mismatch','work_mismatch','upload_mismatch','gpu_unavailable'))
$probeExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$fakeProbe=Join-Path $scriptDirectory 'Tests\Fake-V3-ResourceProbe.ps1'
Assert ([IO.File]::ReadAllText((Join-Path $scriptDirectory 'Run-Draw3-Performance.ps1')).Contains('诊断包 3：软件即将启动')) 'v3 collector banner'
foreach ($behavior in $Behaviors) {
    $root=Capture ('v3-paired-'+$behavior)
    $prefix=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$fakeProbe,(Join-Path $scriptDirectory 'Performance.Common.ps1'),$behavior)
    $matrix=Invoke-PerfResourceMatrix $root $probeExe ([IO.Path]::GetFileName($root)) (Get-PerfSha256 $probeExe) $true $true $false 20 120 $prefix
    Assert ($matrix.cases.Count -eq 4 -and $matrix.probe_revision -eq 3) ('v3 matrix version/count '+$behavior)
    $launch=Read-PerfJson (Join-Path $root 'launch.json');$launch['exe_path']=$probeExe;Write-PerfJson (Join-Path $root 'launch.json') $launch
    $completion=Read-PerfJson (Join-Path $root 'completion.json');$completion['exe_path']=$probeExe;Write-PerfJson (Join-Path $root 'completion.json') $completion
    $s=Analyze $root;$pairs=@($s['resource_matrix']['upload_pairs']);$hardware=$null;$warp=$null
    Assert ([IO.File]::ReadAllText((Join-Path $root 'report.md')).Contains('# Draw3 性能诊断包 3 分析报告（兼容 v1/v2）')) ('v3 report title '+$behavior)
    foreach ($pair in $pairs) {if ($pair['requested_backend'] -eq 'hardware') {$hardware=$pair} else {$warp=$pair}}
    Assert ($pairs.Count -eq 2 -and $warp['status'] -eq 'comparable') ('v3 unaffected backend remains comparable '+$behavior)
    if (@('success','gpu_unavailable') -contains $behavior) {
        Assert ($hardware['status'] -eq 'comparable' -and $hardware['dynamic_to_default_cpu_ratio'] -eq 5 -and $hardware['cpu_lower_policy'] -eq 'compatibility_default') ('v3 matching CPU leaf pair '+$behavior)
        if ($behavior -eq 'gpu_unavailable') {Assert ($null -eq $hardware['default_measure_gpu_interval_ms'] -and $hardware['dynamic_measure_gpu_interval_ms'] -eq 40) 'GPU unavailable remains null and separate from CPU evidence'}
        else {Assert ($hardware['dynamic_measure_gpu_interval_ms'] -eq 40 -and $hardware['default_measure_gpu_interval_ms'] -eq 40) 'GPU four measured intervals exclude cold/warmup'}
    } else {Assert ($hardware['status'] -eq 'unavailable' -and $null -eq $hardware['dynamic_to_default_cpu_ratio'] -and $null -eq $hardware['cpu_lower_policy']) ('v3 partial/mismatch no winner or zero '+$behavior)}
    Assert ([IO.File]::Exists((Join-Path $root 'resource-upload-pairs.csv'))) ('v3 paired CSV '+$behavior)
}
