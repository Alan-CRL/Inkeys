# 开发用成对协议夹具，仅生成固定证据，不创建 D3D 或窗口。
$common=$args[0];$behavior=$args[1];$command=$args[2]
. $common
$definitions=New-Object Collections.ArrayList
foreach ($backend in @('hardware','warp')) {foreach ($policy in @('optimized_dynamic','compatibility_default')) {
    $suffix='dynamic';$method='dynamic_capability_gated_ring';if ($policy -eq 'compatibility_default') {$suffix='default';$method='default_update_subresource'}
    $profile='paired_ink64_'+$suffix
    [void]$definitions.Add(@{case_id=$backend+'-'+$profile;profile_id=$profile;requested_backend=$backend;resource_bytes=3200000;write_bytes=32;draws_per_iteration=2;width=64;height=64;update_method=$method;constant_write_method='production_field_stores';paired_workload='ink64';upload_policy=$policy;composite_resource_bytes=256;composite_write_bytes=32;composite_update_method='write_discard';workload_contract='primary_draw_copy_small_composite_draw_v3'})
}}
$list=@{schema_version=1;probe_revision=3;capability_marker='INKEYS_DRAW3_DIAGNOSTICS_V3';profile='draw3-resource-v3-fixed';comparison_path='offscreen_embedded_301_302_contract';native_budget_ms=8000;warmup_iterations=1;measured_iterations=4;cold_iterations=1;call_capacity=4096;case_count=4;cases=$definitions}
if ($command -eq '--draw3-resource-probe-list') {[Console]::Out.WriteLine([Draw3PerfWire]::Json($list));exit 0}
if ($command -ne '--draw3-resource-probe' -or $args.Count -ne 6) {exit 2}
$id=$args[3];$root=$args[4];$session=$args[5];$case=$null;foreach ($definition in $definitions) {if ($definition.case_id -ceq $id) {$case=$definition}}
if (-not $case) {exit 2}
$lock=New-Object IO.FileStream((Join-Path $root '.draw3-resource-probe.lock'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
    $facts=[Draw3PerfWire]::ProcessIdentity($PID)
    $start=@{schema_version=1;probe_revision=3;capability_marker=$list.capability_marker;profile=$list.profile;comparison_path=$list.comparison_path;case_id=$id;profile_id=$case.profile_id;session_id=$session;final_pid=$PID;process_start_filetime_utc=$facts[1];exe_path=$facts[0];output_directory=$root;requested_backend=$case.requested_backend}
    Write-PerfJson (Join-Path $root 'probe-start.json') $start
    $result=@{};foreach ($key in $start.Keys) {$result[$key]=$start[$key]}
    $compat=$case.upload_policy -eq 'compatibility_default';$target=$compat -and $case.requested_backend -eq 'hardware'
    $result['actual_backend']=$case.requested_backend;$result['case_definition']=$case;$result['status']='complete';$result['exit_code']=0;$result['reason']='synthetic';$result['qpc_frequency']=1000;$result['start_qpc']=1;$result['end_qpc']=200;$result['thread_cpu_ms']=40.0;$result['process_cpu_ms']=50.0;$result['cold_iterations_completed']=1;$result['warmup_iterations_completed']=1;$result['measured_iterations_completed']=4;$result['calls_seen']=24;$result['calls_retained']=24;$result['calls_dropped']=0;$result['invalid_intervals']=0;$result['drain_complete']=$true;$result['gpu_status']='recorded_per_iteration';$result['shader']=@{vertex_resource_id=302;pixel_resource_id=301;vertex_bytes=128;pixel_bytes=128;vertex_fnv1a64='111';pixel_fnv1a64='222'}
    $result['adapter']=@{available=$true;vendor_id=7447;device_id=1;subsystem_id=1;revision=1;driver_version_available=$true;driver_version='100'}
    $result['effective_upload_policy']='dynamic_discard_then_no_overwrite';if ($compat) {$result['effective_upload_policy']='default_update_subresource'}
    if ($target) {switch ($behavior) {
        'partial' {$result.status='partial';$result.exit_code=5;$result.measured_iterations_completed=2}
        'drain_incomplete' {$result.drain_complete=$false}
        'shader_mismatch' {$result.shader.vertex_fnv1a64='999'}
        'adapter_mismatch' {$result.adapter.device_id=2}
        'work_mismatch' {$result.case_definition.width=128}
    }}
    $duration=10;$operation='write_copy';$usage=2;$cpu=65536;if ($compat) {$duration=2;$operation='update_subresource';$usage=0;$cpu=0}
    $rows=@('call_serial,phase,iteration,operation,site,resource_id,related_resource_id,start_qpc,end_qpc,valid_interval,hresult_available,hresult,map_type,map_flags,subresource,offset_bytes,size_bytes,vertex_count,instance_count')
    $gpu=@('phase,iteration,status,frequency,begin_timestamp,end_timestamp,draw_calls,marker_mask')
    for ($iteration=0;$iteration -lt 6;$iteration++) {
        $phase='measure';if ($iteration -eq 0) {$phase='cold'};if ($iteration -eq 1) {$phase='warmup'}
        $a=10+$iteration*20;$b=$a+$duration;$serial=$iteration*4+1;$bytes=32;if ($target -and $behavior -eq 'upload_mismatch') {$bytes=16}
        $rows+=($serial.ToString()+','+$phase+','+$iteration+','+$operation+',primary,1,0,'+$a+','+$b+',1,0,,0,0,0,0,'+$bytes+',0,0')
        $rows+=(($serial+1).ToString()+','+$phase+','+$iteration+',draw,primary_draw,1,0,'+$b+','+$b+',1,0,,0,0,0,0,0,6,1')
        $rows+=(($serial+2).ToString()+','+$phase+','+$iteration+',write_copy,small_composite,8,0,'+$b+','+$b+',1,0,,0,0,0,0,32,0,0')
        $rows+=(($serial+3).ToString()+','+$phase+','+$iteration+',draw,composite_draw,8,0,'+$b+','+$b+',1,0,,0,0,0,0,0,6,1')
        $status='ready';$frequency=1000;$mask=3;$last=110;if ($target -and $behavior -eq 'gpu_unavailable') {$status='query_failed';$frequency=0;$mask=0;$last=0}
        $gpu+=($phase+','+$iteration+','+$status+','+$frequency+',100,'+$last+',2,'+$mask)
    }
    [IO.File]::WriteAllLines((Join-Path $root 'calls.csv'),$rows,(New-Object Text.UTF8Encoding($true)))
    [IO.File]::WriteAllLines((Join-Path $root 'gpu.csv'),$gpu,(New-Object Text.UTF8Encoding($true)))
    $resources=@('resource_id,role,resource_kind,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,create_hresult',('1,primary_ink,buffer,3200000,0,0,0,'+$usage+',8,'+$cpu+',64,16,0'),'8,dedicated_composite_vertices,buffer,256,0,0,0,2,8,65536,64,16,0','3,offscreen_bgra,texture2d,0,64,64,87,0,40,0,0,0,0')
    [IO.File]::WriteAllLines((Join-Path $root 'resources.csv'),$resources,(New-Object Text.UTF8Encoding($true)))
    Write-PerfJson (Join-Path $root 'probe-result.json') $result
    exit $result.exit_code
} finally {$lock.Dispose()}
