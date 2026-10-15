#requires -Version 5.1
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
$scriptDirectory=Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDirectory 'Performance.Common.ps1')
if (-not $OutputRoot) { $OutputRoot=Join-Path $scriptDirectory '..\..\Build\Draw3PerformanceTests' }
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
$testRoot=Join-Path $OutputRoot ('offline-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testRoot)
$passes=New-Object Collections.ArrayList
function Assert([bool]$Condition,[string]$Name) { if (-not $Condition) { throw ('断言失败: '+$Name) }; [void]$passes.Add($Name) }
function Copy-Map($Original) { $map=@{}; foreach ($key in $Original.Keys) {$map[$key]=$Original[$key]}; return $map }
$frameColumns=('frame_serial,start_qpc,end_qpc,present_return_qpc,active_chain,idle_boundary,physical_before,physical_after,width,height,reason_flags,command_count,down_count,consumed_count,terminal_count,tool_mask,dirty_left,dirty_top,dirty_right,dirty_bottom,present_attempted,present_api_attempted,present_succeeded,present_hresult,present_mode,output_target,output_revision,content_revision,full_present,thread_cpu_ms,control_qpc,ingress_qpc,model_reset_qpc,model_update_qpc,model_predict_qpc,geometry_qpc,snapshot_qpc,composite_qpc,upload_map_qpc,upload_copy_qpc,draw_submit_qpc,present_qpc,ulw_copy_submit_qpc,ulw_map_qpc,ulw_rows_qpc,ulw_update_qpc,deadline_wait_qpc,idle_wait_qpc,stage_observed_mask,stage_invalid_mask,upload_bytes,draw_calls,map_calls,map_failures,discard_maps,no_overwrite_maps,device_epoch' -split ',')
$inputColumns=('callback_token,phase,tablet_context_id,contact_id,packet_count,device_type,callback_entry_qpc,source_qpc,published_qpc,callback_end_qpc,publication_succeeded,callback_outcome,record_id,generation,sequence,admission_revision,consume_qpc,process_end_qpc,adopt_qpc,raster_qpc,stored_qpc,present_return_qpc,frame_serial,workspace_id,page_id,scene_generation,raster_generation,output_generation,output_target,output_revision,content_token,item_token,proof_kind,proof_supported,proof_valid,outcome' -split ',')
$gpuColumns=('frame_serial,device_epoch,status,frequency,geometry_begin,geometry_end,composite_begin,composite_end,readback_begin,readback_end,hresult' -split ',')
$frameColumns+=('prior_ingress_qpc,prior_model_reset_qpc,prior_model_update_qpc,prior_model_predict_qpc,prior_upload_map_qpc,prior_upload_copy_qpc,prior_draw_submit_qpc,prior_stage_observed_mask,prior_stage_invalid_mask,prior_upload_bytes,prior_draw_calls,prior_map_calls,prior_down_count,prior_consumed_count,prior_tool_mask' -split ',')
$inputColumns+='effective_tool'
function Write-Csv([string]$Path,$Columns,$Rows) {
    $writer=New-Object IO.StreamWriter($Path,$false,(New-Object Text.UTF8Encoding($true)))
    try { $writer.WriteLine(($Columns -join ',')); foreach ($row in $Rows) { $cells=@(); foreach ($column in $Columns) { $cells+=('"'+([string]$row[$column]).Replace('"','""')+'"') }; $writer.WriteLine(($cells -join ',')) } }
    finally { $writer.Dispose() }
}
function Frame([long]$Serial,[long]$Start,[long]$Duration,[long]$Chain=1,[int]$Idle=0) {
    $row=@{}; foreach ($column in $frameColumns) { $row[$column]=0 }
    $row.frame_serial=$Serial; $row.start_qpc=$Start; $row.end_qpc=$Start+$Duration; $row.present_return_qpc=$Start+$Duration-[Math]::Min(2,$Duration)
    $row.active_chain=$Chain; $row.idle_boundary=$Idle; $row.physical_before=1; $row.physical_after=1; $row.width=1920; $row.height=1080
    $row.present_attempted=1; $row.present_api_attempted=1; $row.present_succeeded=1; $row.device_epoch=1; $row.output_revision=1; $row.present_mode=1
    $row.draw_calls=1; $row.geometry_qpc=[long]($Duration*0.6); $row.model_update_qpc=[long]($Duration*0.4); $row.present_qpc=[long]($Duration*0.2)
    $row.stage_observed_mask=2088; $row.thread_cpu_ms=''; return $row
}
function Input([long]$Token,[int]$Phase,$Frame) {
    $row=@{}; foreach ($column in $inputColumns) { $row[$column]=0 }
    $start=[long]$Frame.start_qpc
    $row.callback_token=$Token; $row.phase=$Phase; $row.packet_count=3; $row.callback_entry_qpc=$start+1; $row.source_qpc=$start+1
    $row.published_qpc=$start+2; $row.callback_end_qpc=$start+3; $row.publication_succeeded=1; $row.callback_outcome='published'
    $row.record_id=1; $row.generation=1; $row.sequence=$Token*2; $row.admission_revision=1
    $row.consume_qpc=$start+3; $row.process_end_qpc=$start+6; $row.adopt_qpc=$start+4; $row.raster_qpc=$start+5
    $row.present_return_qpc=$Frame.present_return_qpc; $row.frame_serial=$Frame.frame_serial
    $row.workspace_id=1; $row.page_id=1; $row.scene_generation=1; $row.raster_generation=1; $row.output_generation=1; $row.output_revision=1
    $row.content_token=1; $row.proof_kind=1; $row.proof_supported=1; $row.proof_valid=1; $row.outcome='confirmed_software_present'
    if ($Phase -eq 2) { $row.proof_kind=2; $row.item_token=1; $row.stored_qpc=$start+10; $row.raster_qpc=0 }
    return $row
}
$baseFrames=@((Frame 1 100 10),(Frame 2 120 10 1 1),(Frame 3 1000 10 2),(Frame 4 1030 100 2))
$baseInputs=@((Input 1 0 $baseFrames[0]),(Input 2 1 $baseFrames[1]),(Input 3 2 $baseFrames[3]))
$baseGpu=@(@{frame_serial=1;device_epoch=1;status='ready';frequency=1000000;geometry_begin=1000;geometry_end=3000;composite_begin=0;composite_end=0;readback_begin=0;readback_end=0;hresult=0},@{frame_serial=2;device_epoch=1;status='pending';frequency=0;geometry_begin=0;geometry_end=0;composite_begin=0;composite_end=0;readback_begin=0;readback_end=0;hresult=0})
function Capture([string]$Name,$Frames=$baseFrames,$Inputs=$baseInputs,$Gpu=$baseGpu,[int]$Dropped=0) {
    $root=Join-Path $testRoot $Name; [void][IO.Directory]::CreateDirectory($root)
    $hostPath=Join-Path $root 'host-0001'; [void][IO.Directory]::CreateDirectory($hostPath)
    $session=@{schema_version=1;qpc_frequency=1000;status='complete';exported=$true;unavailable=$false;frames_seen=$Frames.Length+$Dropped;frames_retained=$Frames.Length;frames_dropped=$Dropped;inputs_seen=$Inputs.Length;inputs_retained=$Inputs.Length;inputs_dropped=0;receipts_seen=$Inputs.Length;receipts_retained=$Inputs.Length;receipts_dropped=0;invalid_intervals=0;proof_overflow=0;gpu_status='available';payload_budget_bytes=33554432;allocated_payload_bytes=1024;legacy_payload_upper_bound_bytes=33554432;disabled_path_contract='null gate'}
    Write-PerfJson (Join-Path $hostPath 'session.json') $session
    Write-PerfJson (Join-Path $hostPath 'initialization.json') @{schema_version=1;actual_driver_type=1;presenter_mode=1;adapter_available=$true;adapter_description='Synthetic';events=@(@{stage='graphics';start_qpc=1;end_qpc=5;hresult=0;succeeded=$true})}
    Write-Csv (Join-Path $hostPath 'frames.csv') $frameColumns $Frames
    Write-Csv (Join-Path $hostPath 'input-events.csv') $inputColumns $Inputs
    Write-Csv (Join-Path $hostPath 'gpu.csv') $gpuColumns $Gpu
    $facts=[Draw3PerfWire]::ProcessIdentity($PID)
    $launch=@{schema_version=1;session_id=$Name;final_pid=$PID;process_start_filetime_utc=$facts[1];process_start_time=[DateTime]::FromFileTimeUtc([long]$facts[1]).ToString('o');exe_path=$facts[0];output_directory=$root;status='started'}
    Write-PerfJson (Join-Path $root 'launch.json') $launch
    $finish=Copy-Map $launch; $finish.status='completed'; $finish.reason='normal_exit'; $finish.exit_code=0; $finish.export_succeeded=$true; $finish.capture_complete=$true
    $finish.export_status=@{host_runs=1;exported_host_runs=1;frames_seen=$Frames.Length+$Dropped;frames_retained=$Frames.Length;frames_dropped=$Dropped;inputs_seen=$Inputs.Length;inputs_retained=$Inputs.Length;inputs_dropped=0;receipts_seen=$Inputs.Length;receipts_retained=$Inputs.Length;receipts_dropped=0;proof_overflow=0}
    if ($Dropped -gt 0) { $finish.status='incomplete'; $finish.reason='capacity_exhausted'; $finish.capture_complete=$false }
    Write-PerfJson (Join-Path $root 'completion.json') $finish
    return $root
}
function Analyze([string]$Root) {
    & (Join-Path $scriptDirectory 'Analyze-Draw3-Performance.ps1') -CaptureRoot $Root
    $result=Read-PerfJson (Join-Path $Root 'summary.json')
    Assert (($LASTEXITCODE -eq 0) -eq ($result['status'] -eq 'complete')) ('退出码对应完整性 '+[IO.Path]::GetFileName($Root))
    return $result
}
try {
    $round=[Draw3PerfWire]::Parse([Draw3PerfWire]::Json(@{text="中文`r`n引号`"反斜杠\";number=1.25;empty=$null;list=@(1,2)}))
    Assert ($round['text'] -eq "中文`r`n引号`"反斜杠\" -and $round['list'].Count -eq 2) 'JSON Unicode/escape/null roundtrip'
    $rejected=$false; try { [void][Draw3PerfWire]::Parse('{"final_pid":1,"final_pid":2}') } catch {$rejected=$true}; Assert $rejected 'JSON duplicate identity rejected'
    $rejected=$false; try { [void][Draw3PerfWire]::Parse('{"v":NaN}') } catch {$rejected=$true}; Assert $rejected 'JSON NaN rejected'
    foreach ($literal in @('true','"1"','1.4','1.0')) {
        $record=[Draw3PerfWire]::Parse('{"schema_version":'+$literal+'}'); $rejected=$false
        try { [void](Get-PerfJsonInteger $record 'schema_version' 1 1) } catch {$rejected=$true}
        Assert $rejected ('typed JSON integer rejects '+$literal)
    }
    $s=Analyze (Capture 'success')
    Assert ($s['status'] -eq 'complete' -and $s['totals']['frames_valid'] -eq 4 -and $s['totals']['frames_csv_rows'] -eq 4) 'success exact frame denominators'
    Assert ($s['distributions']['frame_work']['p95_ms'] -eq 100 -and $null -eq $s['distributions']['frame_work']['p99_ms']) 'nearest rank P95 and sparse P99 null'
    Assert ($s['distributions']['active_interval']['count'] -eq 2 -and $s['distributions']['active_interval']['max_ms'] -eq 30) 'idle does not join 880 ms interval'
    Assert ($s['distributions']['successful_present_interval']['count'] -eq 2) 'success Present idle boundary'
    Assert ($s['input_phases']['Down']['confirmed'] -eq 1 -and $s['input_phases']['Move']['confirmed'] -eq 1 -and $s['input_phases']['Up']['confirmed'] -eq 1) 'Down Move Up separately confirmed'
    Assert ($s['distributions']['input_Down_callback_present']['p95_ms'] -eq 7 -and $s['distributions']['input_Up_callback_present']['p95_ms'] -eq 97) 'exact callback to software Present latency'
    Assert ($s['distributions']['input_Down_source_present']['p95_ms'] -eq 7 -and $s['distributions']['input_Down_callback_total']['p95_ms'] -eq 2) 'source age and whole callback separately measured'
    Assert ($s['distributions']['input_Up_consume_store']['p95_ms'] -eq 7 -and $s['distributions']['input_Up_consume_raster']['count'] -eq 0) 'Stored Up without fabricated raster'
    Assert ($s['distributions']['input_Down_consume_process_inclusive']['p95_ms'] -eq 3 -and $s['distributions']['input_Down_consume_adopt_inclusive']['p95_ms'] -eq 1) 'process_end later than adopt independent inclusive stages'
    Assert ($s['distributions']['gpu_geometry_command_interval']['p95_ms'] -eq 2 -and $s['distributions']['gpu_composite_command_interval']['count'] -eq 0) 'GPU unknown endpoints are null'
    Assert ($s['distributions']['cpu_geometry']['p95_ms'] -eq 60 -and $s['distributions']['cpu_model_update']['p95_ms'] -eq 40 -and $s['distributions']['frame_work']['p95_ms'] -eq 100) 'nested CPU stage stats not added to total'
    Assert ($s['input_tool_phases']['Pen:Move']['callback_present']['p95_ms'] -eq 7 -and $s['input_tool_phases']['Pen:Move']['consumed'] -eq 1) 'actual tool grouped consumption and software latency'
    $top=@(Import-Csv (Join-Path $testRoot 'success\top-long-frames.csv')); Assert ($top[0].frame_serial -eq '4' -and [double]$top[0].work_ms -eq 100) 'top frames sorted by actual work'
    $relocated=Join-Path $testRoot 'renamed portable result'; Copy-Item -LiteralPath (Join-Path $testRoot 'success') -Destination $relocated -Recurse
    $s=Analyze $relocated; Assert ($s['status'] -eq 'complete' -and $s['session_id'] -eq 'success' -and $s['capture_identity_verified'] -eq $true) 'offline relocation retains original capture identity'
    $badIdentityRoot=Capture 'typed-bad-launch'; $badIdentity=Read-PerfJson (Join-Path $badIdentityRoot 'launch.json'); $badIdentity['schema_version']=$true; Write-PerfJson (Join-Path $badIdentityRoot 'launch.json') $badIdentity
    $s=Analyze $badIdentityRoot; Assert ($s['status'] -eq 'incomplete' -and $s['capture_identity_verified'] -eq $false -and $s['input_phases']['Down']['confirmed'] -eq 0) 'bad handshake scalar cannot confirm landing'
    $priorFrames=@($baseFrames | ForEach-Object {Copy-Map $_}); $priorFrames[0].prior_ingress_qpc=700; $priorFrames[0].prior_model_reset_qpc=650; $priorFrames[0].prior_stage_observed_mask=6; $priorFrames[0].prior_down_count=1; $priorFrames[0].prior_consumed_count=1
    $priorRoot=Capture 'slow-prior-down' $priorFrames
    $s=Analyze $priorRoot; $top=@(Import-Csv (Join-Path $priorRoot 'top-long-frames.csv'))
    Assert ($s['distributions']['cpu_prior_model_reset']['p95_ms'] -eq 650 -and $s['distributions']['frame_work']['p95_ms'] -eq 100 -and $top[0].frame_serial -eq '1' -and [double]$top[0].diagnostic_peak_ms -eq 700) 'pre-frame first Down cost stays separate and appears in long-frame table'
    $gapFrames=@(); for ($i=1;$i -le 104;$i++) {$gapFrames+=(Frame $i ($i*20) 10)}; $gapFrames+=(Frame 105 2610 10)
    $gapRoot=Capture 'low-work-long-interval' $gapFrames @() @(); $s=Analyze $gapRoot; $top=@(Import-Csv (Join-Path $gapRoot 'top-long-frames.csv'))
    Assert ($s['distributions']['frame_work']['max_ms'] -eq 10 -and $s['distributions']['active_interval']['max_ms'] -eq 530 -and $s['distributions']['successful_present_interval']['max_ms'] -eq 530) 'interval stall remains separate from low frame work'
    Assert ($top.Length -eq 100 -and $top[0].frame_serial -eq '105' -and [double]$top[0].diagnostic_peak_ms -eq 530 -and [double]$top[0].work_ms -eq 10) 'interval-only stall retained beyond first 100 frame candidates'
    $gapReport=[IO.File]::ReadAllText((Join-Path $gapRoot 'report.md'),[Text.Encoding]::UTF8)
    Assert ($gapReport.Contains('活动/成功呈现间隔 ≥50 ms') -and -not $gapReport.Contains('样本的首笔/前置阶段')) 'report distinguishes interval stall from prior cost'
    $unavailableRoot=Capture 'host-unavailable'; $session=Read-PerfJson (Join-Path $unavailableRoot 'host-0001\session.json'); $session['unavailable']=$true; $session['status']='unavailable'; Write-PerfJson (Join-Path $unavailableRoot 'host-0001\session.json') $session
    $s=Analyze $unavailableRoot; Assert ($s['status'] -eq 'incomplete' -and $s['input_phases']['Down']['confirmed'] -eq 0) 'Host unavailable cannot be empty-data PASS or landing'
    $failedFrames=@($baseFrames | ForEach-Object {Copy-Map $_}); $failedFrames[1].present_succeeded=0; $failedFrames[1].present_hresult=-2005270523
    $failedInputs=@($baseInputs | ForEach-Object {Copy-Map $_}); $failedInputs[1].proof_valid=0; $failedInputs[1].outcome='present_failed'
    $s=Analyze (Capture 'failed-present' $failedFrames $failedInputs)
    Assert ($s['totals']['present_failure'] -eq 1 -and $s['input_phases']['Move']['confirmed'] -eq 0 -and $s['input_phases']['Move']['consumed'] -eq 1) 'failed Present retains consumption not landing'
    $preparationFrames=@($failedFrames | ForEach-Object {Copy-Map $_}); $preparationFrames[1].present_api_attempted=0
    $s=Analyze (Capture 'present-preparation-failure' $preparationFrames $failedInputs)
    Assert ($s['totals']['present_preparation_failure'] -eq 1 -and $s['totals']['present_noop'] -eq 0 -and $s['totals']['present_failure'] -eq 0) 'Map Copy preparation failure is not noop'
    $s=Analyze (Capture 'forged-failed-proof' $failedFrames $baseInputs)
    Assert ($s['status'] -eq 'incomplete' -and $s['input_phases']['Move']['confirmed'] -eq 0) 'failed Present success claim rejected'
    $noopFrames=@($baseFrames | ForEach-Object {Copy-Map $_}); $noopFrames[1].present_succeeded=0; $noopFrames[1].present_api_attempted=0
    $s=Analyze (Capture 'noop-proof' $noopFrames)
    Assert ($s['input_phases']['Move']['confirmed'] -eq 0 -and $s['totals']['present_noop'] -eq 1) 'noop cannot confirm landing'
    $stale=@($baseInputs | ForEach-Object {Copy-Map $_}); $stale[1].output_revision=999
    $s=Analyze (Capture 'stale-output' $baseFrames $stale); Assert ($s['input_phases']['Move']['confirmed'] -eq 0) 'stale output identity rejected'
    $stale[1].output_revision=1; $stale[1].frame_serial=999
    $s=Analyze (Capture 'uncorrelated' $baseFrames $stale); Assert ($s['input_phases']['Move']['confirmed'] -eq 0) 'uncorrelated frame not nearest-time matched'
    $stale[1].frame_serial=2; $stale[1].sequence=3
    $s=Analyze (Capture 'odd-sequence' $baseFrames $stale); Assert ($s['input_phases']['Move']['confirmed'] -eq 0) 'odd sequence identity rejected'
    $duplicate=Copy-Map $baseInputs[1]; $duplicate.callback_token=4
    $s=Analyze (Capture 'duplicate-publication' $baseFrames ($baseInputs+@($duplicate)))
    Assert ($s['input_phases']['Move']['confirmed'] -eq 1 -and $s['distributions']['input_Move_callback_present']['count'] -eq 1) 'duplicate publication different callback token rejected'
    $unconsumed=@($baseInputs | ForEach-Object {Copy-Map $_}); $unconsumed[1].consume_qpc=0; $unconsumed[1].process_end_qpc=0; $unconsumed[1].adopt_qpc=0; $unconsumed[1].raster_qpc=0; $unconsumed[1].present_return_qpc=0; $unconsumed[1].proof_valid=0; $unconsumed[1].outcome='published_not_consumed'
    $s=Analyze (Capture 'missing-receipt' $baseFrames $unconsumed); Assert ($s['input_phases']['Move']['consumed'] -eq 0 -and $s['input_phases']['Move']['confirmed'] -eq 0 -and $s['input_phases']['Move']['outcomes']['published_not_consumed'] -eq 1) 'missing receipt never landing'
    $cancelled=@($baseInputs | ForEach-Object {Copy-Map $_}); $cancelled[2].callback_outcome='cancelled'; $cancelled[2].outcome='cancelled'; $cancelled[2].proof_valid=0; $cancelled[2].proof_supported=0
    $s=Analyze (Capture 'cancelled-published' $baseFrames $cancelled); Assert ($s['input_phases']['Up']['consumed'] -eq 1 -and $s['input_phases']['Up']['confirmed'] -eq 0) 'cancel callback may publish closure without landing'
    $unsupported=@($baseInputs | ForEach-Object {Copy-Map $_}); $unsupported[1].proof_valid=0; $unsupported[1].proof_supported=0; $unsupported[1].outcome='proof_unsupported'
    $s=Analyze (Capture 'unsupported' $baseFrames $unsupported)
    Assert ($s['input_phases']['Move']['unsupported'] -eq 1 -and $s['distributions']['input_Move_callback_present']['count'] -eq 0 -and $s['distributions']['input_Move_callback_consume']['count'] -eq 1) 'unsupported latency null consumption retained'
    $s=Analyze (Capture 'dropped' $baseFrames $baseInputs $baseGpu 7)
    Assert ($s['status'] -eq 'incomplete' -and $s['totals']['frames_seen'] -eq 11 -and $s['totals']['frames_dropped'] -eq 7 -and $s['distributions']['frame_work']['count'] -eq 4) 'dropped prefix denominators not PASS'
    $badFrames=@($baseFrames | ForEach-Object {Copy-Map $_}); $badFrames[3].end_qpc='NaN'
    $s=Analyze (Capture 'invalid-number' $badFrames)
    Assert ($s['totals']['frames_csv_rows'] -eq 4 -and $s['totals']['frames_invalid'] -eq 1 -and $s['totals']['frames_valid'] -eq 3) 'invalid numeric one row counted once'
    $badInputs=@($baseInputs | ForEach-Object {Copy-Map $_}); $badInputs[0].published_qpc=100
    $s=Analyze (Capture 'negative-one-tick' $baseFrames $badInputs)
    Assert ($s['input_phases']['Down']['confirmed'] -eq 0 -and $s['distributions']['input_Down_callback_publish']['count'] -eq 0) 'negative tick not clamped zero'
    $missing=Capture 'missing-csv'; [IO.File]::Delete((Join-Path $missing 'host-0001\input-events.csv'))
    $s=Analyze $missing; Assert ($s['status'] -eq 'incomplete' -and $s['totals']['input_csv_rows'] -eq 0) 'missing CSV incomplete not zero-latency PASS'
    $missing=Capture 'missing-completion'; [IO.File]::Delete((Join-Path $missing 'completion.json'))
    $s=Analyze $missing; Assert ($s['status'] -eq 'incomplete') 'completion required'
    $invalidSession=Capture 'invalid-session'; [IO.File]::WriteAllText((Join-Path $invalidSession 'host-0001\session.json'),'{"schema_version":1,"schema_version":2}')
    $s=Analyze $invalidSession; Assert ($s['status'] -eq 'incomplete') 'malformed session report remains readable'
    $zeroFrame=Frame 1 100 0; $zeroFrame.stage_observed_mask=32; $zeroFrame.geometry_qpc=0; $zeroFrame.thread_cpu_ms='0'
    $s=Analyze (Capture 'zero-observed' @($zeroFrame) @() @())
    Assert ($s['distributions']['cpu_geometry']['count'] -eq 1 -and $s['distributions']['cpu_geometry']['p50_ms'] -eq 0 -and $s['distributions']['owner_thread_cpu']['count'] -eq 1) 'observed zero retained as finite sample'
    $s=Analyze (Capture 'no-data' @() @() @()); Assert ($s['status'] -eq 'no_data' -and $null -eq $s['distributions']['frame_work']['p50_ms']) 'no frames no fabricated zero FPS'
    $badGpu=@($baseGpu | ForEach-Object {Copy-Map $_}); $badGpu[0].device_epoch=2
    $s=Analyze (Capture 'gpu-stale-epoch' $baseFrames $baseInputs $badGpu); Assert ($s['totals']['gpu_invalid'] -eq 1 -and $s['distributions']['gpu_geometry_command_interval']['count'] -eq 0) 'GPU cross epoch rejected'
    $noRenderGpu=Copy-Map $baseGpu[0]; $noRenderGpu.frame_serial=32; $noRenderGpu.geometry_begin=0; $noRenderGpu.geometry_end=0
    $s=Analyze (Capture 'gpu-non-render' $baseFrames $baseInputs @($noRenderGpu)); Assert ($s['status'] -eq 'complete' -and $s['totals']['gpu_non_render_candidates'] -eq 1 -and $s['distributions']['gpu_geometry_command_interval']['count'] -eq 0) 'GPU empty non-render candidate is null not invalid frame'
    $missing=Capture 'missing-initialization'; [IO.File]::Delete((Join-Path $missing 'host-0001\initialization.json'))
    $s=Analyze $missing; Assert ($s['status'] -eq 'incomplete' -and $s['totals']['frames_valid'] -eq 4) 'missing init keeps partial frame evidence'
    $thousand=@(); for ($index=1;$index -le 1000;$index++) { $thousand+=(Frame $index (10000+$index*2000) $index) }
    $s=Analyze (Capture 'p99-1000' $thousand @() @())
    Assert ($s['distributions']['frame_work']['p95_ms'] -eq 950 -and $s['distributions']['frame_work']['p99_ms'] -eq 990) '1000 samples nearest-rank P99'
    $huge=Capture 'byte-budget'; $large=[IO.File]::OpenWrite((Join-Path $huge 'host-0001\frames.csv')); $large.SetLength(67108865); $large.Dispose()
    $s=Analyze $huge; Assert ($s['status'] -eq 'incomplete' -and $s['totals']['analysis_truncated'] -gt 0) 'CSV byte budget bounded refusal'
    . (Join-Path $scriptDirectory 'Tests/Test-V2-Fixtures.ps1')
    # 整个 mock 仅创建无窗口控制台 fixture，模拟 SuperTop 的初始退出和最终身份。
    $windowsPs=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    foreach ($behavior in @('complete','missing_completion','wrong_identity','wrong_exe','wrong_completion','double_stream','late')) {
        $root=Join-Path $testRoot ('生命周期 空格 中文-'+$behavior); [void][IO.Directory]::CreateDirectory($root)
        Write-PerfJson (Join-Path $root 'collector-session.json') @{started_utc=[DateTime]::UtcNow.ToString('o')}
        $args='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+(Join-Path $scriptDirectory 'Tests\Fake-Bootstrap.ps1')+'" -Root "'+$root+'" -CommonPath "'+(Join-Path $scriptDirectory 'Performance.Common.ps1')+'" -FinalScript "'+(Join-Path $scriptDirectory 'Tests\Fake-Final.ps1')+'" -Behavior '+$behavior
        $bootstrap=Start-Process -FilePath $windowsPs -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'bootstrap.stdout.txt') -RedirectStandardError (Join-Path $root 'bootstrap.stderr.txt')
        $ownHandle=$bootstrap.Handle; Assert ($bootstrap.WaitForExit(10000) -and $bootstrap.ExitCode -eq 0) ('bootstrap exit0 '+$behavior)
        $timeout=10; if ($behavior -eq 'late') { $timeout=1 }
        $result=Wait-PerfFinalProcess $root $windowsPs $timeout 10 25
        if (@('complete','double_stream') -contains $behavior) { Assert ($result.status -eq 'completed') ('final handshake '+$behavior) }
        else { Assert ($result.status -eq 'incomplete') ('lifecycle incomplete '+$behavior) }
        if ($behavior -eq 'late') { $childId=[int][IO.File]::ReadAllText((Join-Path $root 'fake-final-pid.txt')); $child=[Diagnostics.Process]::GetProcessById($childId); Assert (-not $child.HasExited) 'timeout does not kill final'; [void]$child.WaitForExit(10000); $child.Dispose() }
        if (@('wrong_identity','wrong_exe') -contains $behavior) { Start-Sleep -Milliseconds 1000 }
        if ($behavior -eq 'double_stream') { Assert (([IO.File]::ReadAllLines((Join-Path $root 'fake-final.stdout.txt'))).Length -eq 12000 -and ([IO.File]::ReadAllLines((Join-Path $root 'fake-final.stderr.txt'))).Length -eq 12000) 'both full streams no deadlock' }
        $bootstrap.Dispose()
    }
    $root=Join-Path $testRoot 'early-failure'; [void][IO.Directory]::CreateDirectory($root); Write-PerfJson (Join-Path $root 'launch-failure-123.json') @{session_id='early-failure';status='failed_before_final';reason='single_instance';final_pid=123}
    $result=Wait-PerfFinalProcess $root $windowsPs 2 0 25; Assert ($result.status -eq 'failed' -and $result.reason -eq 'single_instance') 'explicit early launch failure'
    $root=Capture 'old-process-launch'; Write-PerfJson (Join-Path $root 'collector-session.json') @{started_utc=[DateTime]::UtcNow.ToString('o')}
    $currentFacts=[Draw3PerfWire]::ProcessIdentity($PID); $result=Wait-PerfFinalProcess $root $currentFacts[0] 1 1 25
    Assert ($result.status -eq 'incomplete' -and $result.reason -like '*本次会话创建之前*') 'expired launch rejected'
    $badManifest=Join-Path $testRoot 'bad-manifest.json'; Write-PerfJson $badManifest @{schema_version=1;capture_schema=1;command_line='--draw3-performance';exe_sha256='wrong';script_sha256=@{}}
    & (Join-Path $scriptDirectory 'Run-Draw3-Performance.ps1') -ExePath $windowsPs -ManifestPath $badManifest -OutputRoot $testRoot
    Assert ($LASTEXITCODE -eq 1) 'manifest mismatch refused before any software launch'
    $hashes=@{}; foreach ($name in @('Performance.Common.ps1','Run-Draw3-Performance.ps1','Analyze-Draw3-Performance.ps1')) { $hashes[$name]=Get-PerfSha256 (Join-Path $scriptDirectory $name) }
    $validManifest=Join-Path $testRoot 'valid-manifest.json'; Write-PerfJson $validManifest @{schema_version=1;capture_schema=1;command_line='--draw3-performance';exe_sha256=(Get-PerfSha256 $currentFacts[0]);script_sha256=$hashes}
    & (Join-Path $scriptDirectory 'Run-Draw3-Performance.ps1') -ExePath $currentFacts[0] -ManifestPath $validManifest -OutputRoot $testRoot
    Assert ($LASTEXITCODE -eq 1) 'old manifest and non-capable executable cannot bypass launch gate'
    $root=Join-Path $testRoot 'hardware-query'; [void][IO.Directory]::CreateDirectory($root)
    & (Join-Path $scriptDirectory 'Run-Draw3-Performance.ps1') -HardwareQuery 'nonexistent' -CaptureRoot $root
    $hardware=Read-PerfJson (Join-Path $root 'hardware-nonexistent.json'); Assert ($hardware['status'] -eq 'unavailable' -and $hardware['errors'].Count -eq 1) 'hardware item failure explicit'
    $collectorTokens=$null; $collectorErrors=$null
    $collectorAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $scriptDirectory 'Run-Draw3-Performance.ps1'),[ref]$collectorTokens,[ref]$collectorErrors)
    $hardwareFunction=$collectorAst.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Collect-Hardware'},$true)
    . ([scriptblock]::Create($hardwareFunction.Extent.Text))
    $collectorScriptPath=Join-Path $scriptDirectory 'Tests\Fake-Hardware.ps1'
    $root=Join-Path $testRoot 'hardware-timeout'; [void][IO.Directory]::CreateDirectory($root)
    Collect-Hardware $root 5
    $hardware=Read-PerfJson (Join-Path $root 'hardware.json'); $states=@{}
    foreach ($query in $hardware['queries']) { $states[$query['query']]=$query['status'] }
    Assert ($hardware['queries'].Count -eq 6 -and $states['computer'] -eq 'timeout' -and $states['cpu'] -eq 'available' -and $states['gpu'] -eq 'partial' -and $states['os'] -eq 'unavailable') 'hardware timeout isolated and remaining fields continue'
    $cmdInfo=New-Object Diagnostics.ProcessStartInfo
    $cmdInfo.FileName=Join-Path $env:SystemRoot 'System32\cmd.exe'; $cmdInfo.Arguments='/c ""'+(Join-Path $scriptDirectory 'Run-Draw3-Performance.cmd')+'" "'+(Join-Path $testRoot 'missing.exe')+'""'
    $cmdInfo.UseShellExecute=$false; $cmdInfo.CreateNoWindow=$true; $cmdInfo.RedirectStandardInput=$true
    $cmdProcess=New-Object Diagnostics.Process; $cmdProcess.StartInfo=$cmdInfo; [void]$cmdProcess.Start(); $cmdProcess.StandardInput.WriteLine(''); $cmdProcess.StandardInput.Close()
    Assert ($cmdProcess.WaitForExit(10000) -and $cmdProcess.ExitCode -eq 1) 'CMD preserves PowerShell failure exit through pause'; $cmdProcess.Dispose()
    $fakeExe=Join-Path $testRoot 'package-fixture.exe'; $pe=New-Object byte[] 512
    $pe[0]=0x4d; $pe[1]=0x5a; $pe[0x3c]=0x80; $pe[0x80]=0x50; $pe[0x81]=0x45; $pe[0x84]=0x64; $pe[0x85]=0xaa
    $literal=[Text.Encoding]::Unicode.GetBytes('--draw3-performance'); [Array]::Copy($literal,0,$pe,256,$literal.Length); [IO.File]::WriteAllBytes($fakeExe,$pe); $rejected=$false; try {Test-PerfV2Capability $fakeExe} catch {$rejected=$true}; Assert $rejected 'old performance literal-only PE refused without launch'; $marker=[Text.Encoding]::ASCII.GetBytes('INKEYS_DRAW3_DIAGNOSTICS_V2'); [Array]::Copy($marker,0,$pe,384,$marker.Length); [IO.File]::WriteAllBytes($fakeExe,$pe)
    $packageOutput=Join-Path $testRoot 'packaged'
    & (Join-Path $scriptDirectory 'Build-Package.ps1') -ExePath $fakeExe -OutputDirectory $packageOutput
    $packageFolders=@(Get-ChildItem -LiteralPath $packageOutput | Where-Object {$_.PSIsContainer}); Assert ($packageFolders.Length -eq 1) 'single unique package directory'
    $pack=Read-PerfJson (Join-Path $packageFolders[0].FullName 'diagnostic-manifest.json')
    Assert ($pack['capture_schema'] -eq 2 -and $pack['package_revision'] -eq 2 -and $pack['capability_marker'] -ceq 'INKEYS_DRAW3_DIAGNOSTICS_V2') 'package v2 identity exact marker and revision'
    Assert ($pack['exe_architecture'] -eq 'arm64' -and $pack['exe_sha256'] -eq (Get-PerfSha256 $fakeExe)) 'package PE machine and EXE SHA identity'
    foreach ($name in $pack['script_sha256'].Keys) { Assert ($pack['script_sha256'][$name] -eq (Get-PerfSha256 (Join-Path $packageFolders[0].FullName $name))) ('package script SHA '+$name) }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($packageFolders[0].FullName+'.zip')
    try { $leafNames=@($archive.Entries | ForEach-Object {[IO.Path]::GetFileName($_.FullName)} | Sort-Object); Assert ($archive.Entries.Count -eq 7 -and $leafNames -contains 'Inkeys.exe' -and $leafNames -contains 'diagnostic-manifest.json' -and $leafNames -notcontains 'Build-Package.ps1') 'ZIP exact seven user files' }
    finally {$archive.Dispose()}
    $archive=[IO.Compression.ZipFile]::OpenRead($packageFolders[0].FullName+'.zip')
    try {foreach ($entry in $archive.Entries) {$name=[IO.Path]::GetFileName($entry.FullName);$stream=$entry.Open();$hash=[Security.Cryptography.SHA256]::Create();try {$actual=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()} finally {$stream.Dispose();$hash.Dispose()};Assert ($actual -ceq (Get-PerfSha256 (Join-Path $packageFolders[0].FullName $name))) ('ZIP stream SHA '+$name)}}
    finally {$archive.Dispose()}
    # 测试版脚本允许现代 AST；用户脚本禁止依赖 PS3+/NET4 新接口。
    foreach ($file in @(Get-ChildItem -LiteralPath $scriptDirectory -Filter '*.ps1')) {
        $tokens=$null; $parseErrors=$null; [void][Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$parseErrors)
        Assert ($parseErrors.Count -eq 0) ('AST '+$file.Name)
        if ($file.Name -notmatch '^(Build-Package|Test-Draw3-Performance)') { $text=[IO.File]::ReadAllText($file.FullName); Assert ($text -notmatch '\$PSScriptRoot|\[ordered\]|\[pscustomobject\]|::new\(|Convert(To|From)-Json|Get-CimInstance|Get-FileHash| -sh[l|r] |\.Restart\(') ('PS2 forbidden API '+$file.Name) }
    }
    Write-PerfJson (Join-Path $testRoot 'test-results.json') @{status='passed';assertions=$passes;count=$passes.Count;ps_version=[string]$PSVersionTable.PSVersion;scope='离线合成数值/无 GUI mock 进程；未实测 Win7 PS2、RTS、D3D、SuperTop 或目标硬件'}
    Write-Host ('PASS '+$passes.Count+' 项；结果: '+$testRoot)
    exit 0
} catch {
    Write-PerfJson (Join-Path $testRoot 'test-results.json') @{status='failed';assertions=$passes;count=$passes.Count;error=$_.Exception.ToString();ps_version=[string]$PSVersionTable.PSVersion}
    Write-Host ('FAIL: '+$_.Exception.Message+'；结果: '+$testRoot)
    exit 1
}
