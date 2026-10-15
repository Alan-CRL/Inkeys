# 开发用无窗口协议夹具；不创建任何图形设备或产品窗口。
$common=$args[0];$behavior=$args[1];$command=$args[2]
. $common
$definitions=New-Object Collections.ArrayList
foreach ($backend in @('hardware','warp')) {[void]$definitions.Add(@{case_id=$backend+'-constant48_fields_draw';profile_id='constant48_fields_draw';requested_backend=$backend;resource_bytes=48;write_bytes=48;draws_per_iteration=1;width=64;height=64;update_method='discard';constant_write_method='fields'})}
$list=@{schema_version=1;probe_revision=2;capability_marker='INKEYS_DRAW3_DIAGNOSTICS_V2';profile='draw3-resource-v2-fixed';comparison_path='offscreen_embedded_301_302_contract';native_budget_ms=8000;warmup_iterations=1;measured_iterations=4;cold_iterations=1;call_capacity=4096;case_count=2;cases=$definitions}
if ($command -eq '--draw3-resource-probe-list') {[Console]::Out.WriteLine([Draw3PerfWire]::Json($list));exit 0}
if ($command -ne '--draw3-resource-probe' -or $args.Count -ne 6) {exit 2}
$id=$args[3];$root=$args[4];$session=$args[5];$case=$null;foreach ($definition in $definitions) {if ($definition.case_id -eq $id) {$case=$definition}}
if (-not $case) {exit 2}
$lock=$null
try {
    $lock=New-Object IO.FileStream((Join-Path $root '.draw3-resource-probe.lock'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $facts=[Draw3PerfWire]::ProcessIdentity($PID)
    $start=@{schema_version=1;probe_revision=2;capability_marker=$list.capability_marker;profile=$list.profile;comparison_path=$list.comparison_path;case_id=$id;profile_id=$case.profile_id;session_id=$session;final_pid=$PID;process_start_filetime_utc=$facts[1];exe_path=$facts[0];output_directory=$root;requested_backend=$case.requested_backend}
    Write-PerfJson (Join-Path $root 'probe-start.json') $start
    if ($id -like 'hardware-*') {
        if ($behavior -eq 'timeout') {Start-Sleep -Seconds 60;exit 4}
        if ($behavior -eq 'crash') {exit 4}
        if ($behavior -eq 'double_stream') {$chunk=New-Object string('x',4096);for ($i=0;$i -lt 400;$i++) {[Console]::Out.WriteLine($chunk);[Console]::Error.WriteLine($chunk)}}
    }
    $result=@{};foreach ($key in $start.Keys) {$result[$key]=$start[$key]}
    $result['actual_backend']=$case.requested_backend;$result['case_definition']=$case;$result['status']='complete';$result['exit_code']=0;$result['reason']='synthetic';$result['qpc_frequency']=1000;$result['start_qpc']=1;$result['end_qpc']=200;$result['thread_cpu_ms']=40.0;$result['process_cpu_ms']=50.0;$result['cold_iterations_completed']=1;$result['warmup_iterations_completed']=1;$result['measured_iterations_completed']=4;$result['calls_seen']=12;$result['calls_retained']=12;$result['calls_dropped']=0;$result['invalid_intervals']=0;$result['drain_complete']=$true;$result['gpu_status']='not_collected_by_fixture';$result['shader']=@{vertex_resource_id=302;pixel_resource_id=301;vertex_bytes=128;pixel_bytes=128;vertex_fnv1a64='111';pixel_fnv1a64='222'}
    if ($id -like 'hardware-*') {
        switch ($behavior) {
            'wrong_session' {$result.session_id='wrong-session'}
            'wrong_exe' {$result.exe_path='C:\wrong\Inkeys.exe'}
            'wrong_filetime' {$result.process_start_filetime_utc='1'}
            'wrong_backend' {$result.actual_backend='warp'}
            'wrong_schema' {$result.schema_version=$true}
            'exit_mismatch' {$result.exit_code=4}
            'unsupported' {$result.status='unsupported';$result.exit_code=3;$result.actual_backend='';$result.measured_iterations_completed=0}
            'failed' {$result.status='failed';$result.exit_code=4;$result.measured_iterations_completed=0}
            'missing_result' {exit 4}
        }
    }
    $duration=10;if ($case.requested_backend -eq 'warp') {$duration=5}
    $header='call_serial,phase,iteration,operation,site,resource_id,related_resource_id,start_qpc,end_qpc,valid_interval,hresult_available,hresult,map_type,map_flags,subresource,offset_bytes,size_bytes,vertex_count,instance_count'
    $rows=@($header)
    for ($iteration=0;$iteration -lt 6;$iteration++) {$phase='measure';if ($iteration -eq 0) {$phase='cold'};if ($iteration -eq 1) {$phase='warmup'};$startTick=10+$iteration*20;$serial=1+$iteration*2;$endTick=$startTick+$duration;$rows+=($serial.ToString()+','+$phase+','+$iteration+',map,fixture,1,0,'+$startTick+','+$endTick+',1,1,0,4,0,0,0,48,0,0');$rows+=(($serial+1).ToString()+','+$phase+','+$iteration+',draw,fixture_draw,1,0,'+$endTick+','+$endTick+',1,0,,0,0,0,0,0,6,1')}
    [IO.File]::WriteAllLines((Join-Path $root 'calls.csv'),$rows,(New-Object Text.UTF8Encoding($true)))
    [IO.File]::WriteAllText((Join-Path $root 'resources.csv'),'resource_id,role,resource_kind,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,create_hresult'+[Environment]::NewLine+'1,constant,buffer,48,0,0,0,2,4,65536,0,0,0'+[Environment]::NewLine,(New-Object Text.UTF8Encoding($true)))
    $gpuRows=@('phase,iteration,status,frequency,begin_timestamp,end_timestamp,draw_calls,marker_mask');for ($iteration=0;$iteration -lt 6;$iteration++) {$phase='measure';if ($iteration -eq 0) {$phase='cold'};if ($iteration -eq 1) {$phase='warmup'};$gpuRows+=($phase+','+$iteration+',query_failed,0,0,0,1,0')};[IO.File]::WriteAllLines((Join-Path $root 'gpu.csv'),$gpuRows,(New-Object Text.UTF8Encoding($true)))
    Write-PerfJson (Join-Path $root 'probe-result.json') $result
    if ($id -like 'hardware-*' -and $behavior -eq 'exit_mismatch') {exit 0};exit $result.exit_code
} finally {if ($lock) {$lock.Dispose()}}
