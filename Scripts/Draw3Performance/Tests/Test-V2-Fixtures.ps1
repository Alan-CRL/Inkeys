$operationNames=@('map','write_copy','write_constant','unmap','bind','copy','clear','draw','present','other')
$graphicsColumns=('call_serial,frame_serial,device_epoch,resource_id,related_resource_id,site_id,operation,resource_role,pass,start_qpc,end_qpc,valid_interval,hresult_available,hresult,subresource,map_type,map_flags,offset_bytes,size_bytes,argument0,argument1,parent_call_serial,attribution,accounting_valid,resource_available,depth' -split ',')
$resourceColumns=('resource_id,device_epoch,resource_kind,resource_role,site_id,descriptor_available,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,created_qpc,destroyed_qpc,create_hresult' -split ',')
$groupColumns=('group_id,site_id,operation,resource_role,pass,attribution,calls_seen,valid_calls,invalid_calls,failed_calls,hresult_calls,size_bytes,total_qpc,min_qpc,max_qpc,max_call_serial,max_frame_serial,arithmetic_overflow' -split ',')
$graphicsFrameColumns=@();foreach ($prefix in @('graphics_','prior_graphics_')) {foreach ($part in @('leaf_qpc','call_count','invalid_count','observed_mask','invalid_mask')) {$graphicsFrameColumns+=($prefix+$part)};foreach ($op in $operationNames) {$graphicsFrameColumns+=($prefix+$op+'_qpc')};foreach ($op in $operationNames) {$graphicsFrameColumns+=($prefix+$op+'_count')}}
$v2GpuColumns=$gpuColumns+@('sample_id','loop_serial','candidate_index','sampling_reason','marker_observed_mask','draw_calls_in_sample','geometry_draw_calls','composite_draw_calls','readback_calls','work_mask','cpu_begin_qpc','cpu_end_qpc','frame_retained','marker_clipped_mask','sampling_domain','domain_candidate_index')
function Graphics-Call([long]$Serial,[string]$Operation,[long]$Start,[long]$End,[long]$FrameSerial=1,[string]$Attribution='frame') {
    $row=@{};foreach ($name in $graphicsColumns) {$row[$name]=0}
    $row.call_serial=$Serial;$row.frame_serial=$FrameSerial;$row.device_epoch=1;$row.resource_id=1;$row.site_id=$Serial;$row.operation=$Operation;$row.resource_role='ink_data';$row.pass='geometry';$row.start_qpc=$Start;$row.end_qpc=$End;$row.valid_interval=1;$row.accounting_valid=1;$row.resource_available=1;$row.attribution=$Attribution;$row.depth=1;$row.hresult='';$row.size_bytes=32
    if ($Operation -eq 'map') {$row.hresult_available=1;$row.hresult=0;$row.map_type=4}
    return $row
}
function CaptureV2([string]$FixtureName) {
    $frames=@((Frame 1 100 100),(Frame 2 300 20));$frames[0].thread_cpu_ms=80;$frames[1].draw_calls=0
    foreach ($frame in $frames) {foreach ($name in $graphicsFrameColumns) {$frame[$name]=0}}
    $frames[0].graphics_leaf_qpc=70;$frames[0].graphics_call_count=4;$frames[0].graphics_observed_mask=27;$frames[0].graphics_map_qpc=30;$frames[0].graphics_write_copy_qpc=5;$frames[0].graphics_unmap_qpc=35
    foreach ($op in @('map','write_copy','unmap','bind')) {$frames[0]['graphics_'+$op+'_count']=1}
    $frames[0].prior_graphics_leaf_qpc=45;$frames[0].prior_graphics_call_count=1;$frames[0].prior_graphics_map_qpc=45;$frames[0].prior_graphics_map_count=1;$frames[0].prior_graphics_observed_mask=1
    $gpu=@();foreach ($frame in $frames) {$g=@{};foreach ($name in $v2GpuColumns) {$g[$name]=0};$g.frame_serial=$frame.frame_serial;$g.loop_serial=$frame.frame_serial;$g.sample_id=$frame.frame_serial;$g.candidate_index=$frame.frame_serial;$g.device_epoch=1;$g.status='ready';$g.frequency=1000000;$g.geometry_begin=1000;$g.geometry_end=3000;$g.marker_observed_mask=3;$g.sampling_reason='first_work';$g.draw_calls_in_sample=$frame.draw_calls;$g.geometry_draw_calls=$frame.draw_calls;$g.work_mask=1;$g.cpu_begin_qpc=$frame.start_qpc;$g.cpu_end_qpc=$frame.end_qpc;$g.frame_retained=1;$g.sampling_domain='frame';$g.domain_candidate_index=$frame.frame_serial;$gpu+=,$g}
    $root=Capture $FixtureName $frames @() @();$hostPath=Join-Path $root 'host-0001'
    Write-Csv (Join-Path $hostPath 'frames.csv') ($frameColumns+$graphicsFrameColumns) $frames
    Write-Csv (Join-Path $hostPath 'gpu.csv') $v2GpuColumns $gpu
    $calls=@((Graphics-Call 1 'map' 100 130),(Graphics-Call 2 'write_copy' 130 135),(Graphics-Call 3 'unmap' 135 170),(Graphics-Call 4 'bind' 170 170),(Graphics-Call 5 'map' 50 95 0 'prior'))
    Write-Csv (Join-Path $hostPath 'graphics-calls.csv') $graphicsColumns $calls
    Write-Csv (Join-Path $hostPath 'slow-graphics-calls.csv') $graphicsColumns @($calls[0],$calls[1],$calls[2],$calls[4])
    $resource=@{};foreach ($name in $resourceColumns) {$resource[$name]=0};$resource.resource_id=1;$resource.device_epoch=1;$resource.resource_kind='buffer';$resource.resource_role='ink_data';$resource.site_id=1;$resource.descriptor_available=1;$resource.byte_width=3200000;$resource.created_qpc=1
    Write-Csv (Join-Path $hostPath 'resources.csv') $resourceColumns @($resource)
    $groups=@();foreach ($call in $calls) {$group=@{};foreach ($name in $groupColumns) {$group[$name]=0};$group.group_id=$call.call_serial;$group.site_id=$call.site_id;$group.operation=$call.operation;$group.resource_role=$call.resource_role;$group.pass=$call.pass;$group.attribution=$call.attribution;$group.calls_seen=1;$group.valid_calls=1;$group.hresult_calls=$call.hresult_available;$group.size_bytes=32;$group.total_qpc=$call.end_qpc-$call.start_qpc;$group.min_qpc=$group.total_qpc;$group.max_qpc=$group.total_qpc;$group.max_call_serial=$call.call_serial;$group.max_frame_serial=$call.frame_serial;$groups+=,$group}
    Write-Csv (Join-Path $hostPath 'graphics-groups.csv') $groupColumns $groups
    $session=Read-PerfJson (Join-Path $hostPath 'session.json');$session['schema_version']=2
    $session['raw_schemas']=@{'session.json'=2;'initialization.json'=1;'frames.csv'=2;'input-events.csv'=1;'gpu.csv'=2;'resources.csv'=1;'graphics-calls.csv'=1;'slow-graphics-calls.csv'=1;'graphics-groups.csv'=1}
    foreach ($name in @('graphics_calls_seen','graphics_calls_retained')) {$session[$name]=5};foreach ($name in @('slow_graphics_calls_seen','slow_graphics_calls_retained')) {$session[$name]=4}
    $session['resources_seen']=1;$session['resources_retained']=1;$session['graphics_groups_retained']=5
    foreach ($name in @('graphics_calls_dropped','slow_graphics_calls_dropped','resources_dropped','graphics_group_calls_dropped','graphics_invalid_intervals','graphics_nested_calls','graphics_unknown_resources','graphics_site_collisions','graphics_cross_frame_calls','graphics_group_overflows','graphics_arithmetic_overflows')) {$session[$name]=0}
    $session['graphics_call_capacity']=8192;$session['slow_graphics_call_capacity']=4096;$session['resource_capacity']=512;$session['graphics_group_capacity']=2048;$session['graphics_accounting']='non_nested_leaf_intervals_split_at_frame_start';$session['graphics_detail_policy']='prefix_plus_independent_slow_prefix';$session['first_retained_call_qpc']=50;$session['last_retained_call_qpc']=170;$session['graphics_detail_dropped_after_qpc']=0;$session['graphics_slow_threshold_qpc']=1;$session['graphics_slow_threshold_ms']=1;$session['payload_components']=@{fixture=@{row_bytes=1024;capacity=1;bytes=1024}};$session['graphics_sites']=@(@{site_id=1;name='fixture.map'},@{site_id=2;name='fixture.write'},@{site_id=3;name='fixture.unmap'},@{site_id=4;name='fixture.bind'},@{site_id=5;name='fixture.prior'})
    Write-PerfJson (Join-Path $hostPath 'session.json') $session
    return $root
}
$root=CaptureV2 'v2-success';$s=Analyze $root
Assert ($s['status'] -eq 'complete' -and $s['capture_schema'] -eq 2 -and $s['graphics']['unique_detail_calls'] -eq 5) 'v2 full/slow prefixes deduplicate call identity'
Assert ($s['distributions']['frame_work']['p50_ms'] -eq 20 -and $s['distributions']['render_frame_work']['p50_ms'] -eq 100 -and $s['gpu_coverage']['draw_frames'] -eq 1) 'v2 real Draw frame denominator excludes empty loop'
$frameDetails=@(Import-Csv (Join-Path $root 'frame-graphics-summary.csv'));Assert ([double]$frameDetails[0].detail_union_ms -eq 70 -and [double]$frameDetails[0].graphics_residual_ms -eq 30 -and [double]$frameDetails[0].work_ms -eq 100) 'v2 leaf union and prior stay within frame; inclusive stages not summed'
Assert ($s['gpu_coverage']['ready_draw_samples'] -eq 1 -and $s['gpu_coverage']['empty_work_samples'] -eq 1 -and $s['distributions']['gpu_geometry_command_interval']['count'] -eq 1 -and $s['distributions']['gpu_composite_command_interval']['count'] -eq 0) 'v2 GPU actual per-stage Draw gating'
$root=CaptureV2 'v2-detail-dropped';$hostPath=Join-Path $root 'host-0001';$session=Read-PerfJson (Join-Path $hostPath 'session.json');$session['graphics_calls_seen']=100;$session['graphics_calls_dropped']=95;$session['graphics_detail_dropped_after_qpc']=170;Write-PerfJson (Join-Path $hostPath 'session.json') $session
$groups=@(Import-Csv (Join-Path $hostPath 'graphics-groups.csv') | ForEach-Object {$r=@{};foreach ($p in $_.PSObject.Properties) {$r[$p.Name]=$p.Value};$r});$groups[0].calls_seen=96;$groups[0].valid_calls=96;$groups[0].hresult_calls=96;$groups[0].total_qpc=3000;$groups[0].max_qpc=100;Write-Csv (Join-Path $hostPath 'graphics-groups.csv') $groupColumns $groups
$s=Analyze $root;Assert ($s['status'] -eq 'incomplete' -and $s['graphics']['coverage'] -eq 'partial' -and $s['graphics']['groups'].Count -eq 5) 'v2 detail saturation reports partial while full-session groups remain'
$root=CaptureV2 'v2-nested';$hostPath=Join-Path $root 'host-0001';$calls=@(Import-Csv (Join-Path $hostPath 'graphics-calls.csv') | ForEach-Object {$r=@{};foreach ($p in $_.PSObject.Properties) {$r[$p.Name]=$p.Value};$r});$nested=Graphics-Call 6 'other' 100 170;$nested.parent_call_serial=1;$nested.accounting_valid=0;$calls+=,$nested;Write-Csv (Join-Path $hostPath 'graphics-calls.csv') $graphicsColumns $calls
$session=Read-PerfJson (Join-Path $hostPath 'session.json');$session['graphics_calls_seen']=6;$session['graphics_calls_retained']=6;$session['graphics_nested_calls']=1;$session['graphics_groups_retained']=6;Write-PerfJson (Join-Path $hostPath 'session.json') $session
$groups=@(Import-Csv (Join-Path $hostPath 'graphics-groups.csv') | ForEach-Object {$r=@{};foreach ($p in $_.PSObject.Properties) {$r[$p.Name]=$p.Value};$r});$g=@{};foreach ($name in $groupColumns) {$g[$name]=0};$g.group_id=6;$g.site_id=6;$g.operation='other';$g.resource_role='ink_data';$g.pass='geometry';$g.attribution='frame';$g.calls_seen=1;$g.invalid_calls=1;$g.min_qpc='';$g.max_qpc='';$groups+=,$g;Write-Csv (Join-Path $hostPath 'graphics-groups.csv') $groupColumns $groups
$s=Analyze $root;$frameDetails=@(Import-Csv (Join-Path $root 'frame-graphics-summary.csv'));Assert ([double]$frameDetails[0].detail_union_ms -eq 70 -and $s['status'] -eq 'incomplete' -and @($s['graphics']['groups'] | Where-Object {$_['operation'] -eq 'other' -and $null -eq $_['min_ms']}).Count -eq 1) 'v2 nested span never double counted in leaf union'
foreach ($behavior in @('missing_stream','duplicate_call','void_hresult','bad_clock','resource_duplicate','gpu_no_draw','gpu_missing_marker','gpu_epoch','gpu_prior','gpu_prior_bad_frame','group_enum','group_duplicate_key','group_site','capacity_retained','resource_role','summary_sum','summary_mask','summary_count','schema_bool')) {
    $root=CaptureV2 ('v2-'+$behavior);$hostPath=Join-Path $root 'host-0001'
    switch ($behavior) {
        'missing_stream' {[IO.File]::Delete((Join-Path $hostPath 'graphics-groups.csv'))}
        'duplicate_call' {[IO.File]::AppendAllText((Join-Path $hostPath 'graphics-calls.csv'),([IO.File]::ReadAllLines((Join-Path $hostPath 'graphics-calls.csv'))[1]+[Environment]::NewLine))}
        'void_hresult' {$lines=[IO.File]::ReadAllLines((Join-Path $hostPath 'graphics-calls.csv'));$cells=[Draw3PerfWire]::Csv($lines[3]);$cells[13]='0';$lines[3]=$cells -join ',';[IO.File]::WriteAllLines((Join-Path $hostPath 'graphics-calls.csv'),$lines,(New-Object Text.UTF8Encoding($true)))}
        'bad_clock' {$lines=[IO.File]::ReadAllLines((Join-Path $hostPath 'graphics-calls.csv'));$cells=[Draw3PerfWire]::Csv($lines[1]);$cells[10]='99';$lines[1]=$cells -join ',';[IO.File]::WriteAllLines((Join-Path $hostPath 'graphics-calls.csv'),$lines,(New-Object Text.UTF8Encoding($true)))}
        'resource_duplicate' {[IO.File]::AppendAllText((Join-Path $hostPath 'resources.csv'),([IO.File]::ReadAllLines((Join-Path $hostPath 'resources.csv'))[1]+[Environment]::NewLine))}
        'group_enum' {$rows=@(Import-Csv (Join-Path $hostPath 'graphics-groups.csv'));$rows[0].operation='invented';$rows | Export-Csv (Join-Path $hostPath 'graphics-groups.csv') -NoTypeInformation -Encoding UTF8}
        'group_duplicate_key' {$rows=@(Import-Csv (Join-Path $hostPath 'graphics-groups.csv'));$rows[1].site_id=$rows[0].site_id;$rows[1].operation=$rows[0].operation;$rows | Export-Csv (Join-Path $hostPath 'graphics-groups.csv') -NoTypeInformation -Encoding UTF8}
        'group_site' {$rows=@(Import-Csv (Join-Path $hostPath 'graphics-groups.csv'));$rows[0].site_id='999';$rows | Export-Csv (Join-Path $hostPath 'graphics-groups.csv') -NoTypeInformation -Encoding UTF8}
        'capacity_retained' {$session=Read-PerfJson (Join-Path $hostPath 'session.json');$session['graphics_call_capacity']=4;Write-PerfJson (Join-Path $hostPath 'session.json') $session}
        'resource_role' {$rows=@(Import-Csv (Join-Path $hostPath 'resources.csv'));$rows[0].resource_role='invented';$rows | Export-Csv (Join-Path $hostPath 'resources.csv') -NoTypeInformation -Encoding UTF8}
        'summary_sum' {$rows=@(Import-Csv (Join-Path $hostPath 'frames.csv'));$rows[0].graphics_leaf_qpc='80';$rows | Export-Csv (Join-Path $hostPath 'frames.csv') -NoTypeInformation -Encoding UTF8}
        'summary_mask' {$rows=@(Import-Csv (Join-Path $hostPath 'frames.csv'));$rows[0].graphics_observed_mask='2048';$rows | Export-Csv (Join-Path $hostPath 'frames.csv') -NoTypeInformation -Encoding UTF8}
        'summary_count' {$rows=@(Import-Csv (Join-Path $hostPath 'frames.csv'));$rows[0].graphics_call_count='99';$rows | Export-Csv (Join-Path $hostPath 'frames.csv') -NoTypeInformation -Encoding UTF8}
        'schema_bool' {$session=Read-PerfJson (Join-Path $hostPath 'session.json');$session['raw_schemas']['graphics-calls.csv']=$true;Write-PerfJson (Join-Path $hostPath 'session.json') $session}
        default {$gpu=@(Import-Csv (Join-Path $hostPath 'gpu.csv') | ForEach-Object {$r=@{};foreach ($p in $_.PSObject.Properties) {$r[$p.Name]=$p.Value};$r});if ($behavior -eq 'gpu_no_draw') {$gpu[0].draw_calls_in_sample=0;$gpu[0].geometry_draw_calls=0};if ($behavior -eq 'gpu_missing_marker') {$gpu[0].marker_observed_mask=1};if ($behavior -eq 'gpu_epoch') {$gpu[0].device_epoch=2};if (@('gpu_prior','gpu_prior_bad_frame') -contains $behavior) {$gpu[0].sampling_domain='prior';$gpu[0].frame_retained=0;$gpu[0].loop_serial=0;if ($behavior -eq 'gpu_prior') {$gpu[0].frame_serial=0}};Write-Csv (Join-Path $hostPath 'gpu.csv') $v2GpuColumns $gpu}
    }
    $s=Analyze $root
    if ($behavior -eq 'gpu_prior') {Assert ($s['status'] -eq 'complete' -and $s['gpu_coverage']['ready_draw_samples'] -eq 0 -and $s['gpu_coverage']['ready_prior_draw_segments'] -eq 1) 'prior GPU is independent and never borrows a CPU frame'}
    elseif (@('gpu_no_draw','gpu_missing_marker') -contains $behavior) {Assert ($s['gpu_coverage']['ready_draw_samples'] -eq 0 -and $null -eq $s['distributions']['gpu_geometry_command_interval']['p50_ms']) ('GPU insufficient not zero '+$behavior)}
    else {Assert ($s['status'] -eq 'incomplete') ('v2 malformed bounded evidence '+$behavior)}
}
$emptyFrames=@($baseFrames | ForEach-Object {$r=Copy-Map $_;$r.draw_calls=0;$r});$s=Analyze (Capture 'v1-empty-gpu-coverage' $emptyFrames)
Assert ($s['graphics']['coverage'] -eq 'not_collected_by_v1' -and $s['gpu_coverage']['ready_draw_samples'] -eq 0 -and $s['distributions']['gpu_geometry_command_interval']['count'] -eq 0) 'legacy v1 empty-loop GPU cannot exonerate shader'
$probeExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe';$fakeProbe=Join-Path $scriptDirectory 'Tests\Fake-ResourceProbe.ps1'
foreach ($behavior in @('success','timeout','crash','wrong_session','wrong_exe','wrong_filetime','wrong_backend','wrong_schema','exit_mismatch','unsupported','failed','missing_result','double_stream')) {
    $root=Capture ('matrix-'+$behavior);$prefix=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$fakeProbe,(Join-Path $scriptDirectory 'Performance.Common.ps1'),$behavior)
    $timeout=5;if ($behavior -eq 'timeout') {$timeout=2}
    $matrix=Invoke-PerfResourceMatrix $root $probeExe ([IO.Path]::GetFileName($root)) (Get-PerfSha256 $probeExe) $true $true $false $timeout 30 $prefix
    Assert ($matrix.cases.Count -eq 2 -and $matrix.cases[1].status -eq 'complete') ('matrix failure isolation and continue '+$behavior)
    if (@('success','double_stream') -contains $behavior) {Assert ($matrix.status -eq 'complete' -and $matrix.cases[0].identity_verified) ('owned probe success '+$behavior)}
    else {Assert ($matrix.status -eq 'partial' -and $matrix.cases[0].status -ne 'complete') ('owned probe partial '+$behavior)}
    if ($behavior -eq 'timeout') {Assert ($matrix.cases[0].worker.timed_out -and $matrix.cases[0].worker.termination_requested -and $matrix.cases[0].worker.stopped_confirmed) 'timeout only terminates owned worker and confirms exit'}
    if ($behavior -eq 'double_stream') {$worker=$matrix.cases[0].worker;Assert ($worker.stdout_seen_bytes -gt 1048576 -and $worker.stderr_seen_bytes -gt 1048576 -and $worker.stdout_retained_bytes -eq 1048576 -and $worker.stderr_retained_bytes -eq 1048576 -and $worker.drains_complete) 'dual stream bounded retention continues draining both pipes'}
    if ($behavior -eq 'success') {
        $launch=Read-PerfJson (Join-Path $root 'launch.json');$launch['exe_path']=$probeExe;Write-PerfJson (Join-Path $root 'launch.json') $launch;$completion=Read-PerfJson (Join-Path $root 'completion.json');$completion['exe_path']=$probeExe;Write-PerfJson (Join-Path $root 'completion.json') $completion
        $s=Analyze $root;Assert ($s['resource_matrix']['pairs'].Count -eq 1 -and $s['resource_matrix']['pairs'][0]['hardware_to_warp_ratio'] -eq 2) 'matrix matched HW/WARP CPU-call ratio and cold/warmup separated'
        $relocated=Join-Path $testRoot 'matrix portable rename';Copy-Item -LiteralPath $root -Destination $relocated -Recurse;$s=Analyze $relocated;Assert ($s['resource_matrix']['pairs'].Count -eq 1) 'matrix offline relocation preserves immutable original identity'
        $reportText=[IO.File]::ReadAllText((Join-Path $root 'report.md'));Assert ($reportText.Contains('HW measure ms') -and $reportText.Contains('最长 measure 单调用') -and $reportText.Contains('production_field_stores 实际写 36B')) 'Chinese report contains numeric matrix, max call, drain/GPU and unequal constant-write boundary'
        Assert ($s['resource_matrix']['cases'][0]['gpu_coverage']['ready_draw_samples'] -eq 0 -and $null -eq $s['resource_matrix']['cases'][0]['gpu_coverage']['draw_command_interval']['p50_ms']) 'matrix query_failed does not become zero GPU time'
        foreach ($rawFailure in @('resource_duplicate','gpu_duplicate','gpu_bad_status','call_missing_resource','call_missing_draw','gpu_draw_count')) {
            $badRoot=Join-Path $testRoot ('matrix-raw-'+$rawFailure);Copy-Item -LiteralPath $root -Destination $badRoot -Recurse;$casePath=Join-Path $badRoot 'resource-probes\c001'
            switch ($rawFailure) {
                'resource_duplicate' {$path=Join-Path $casePath 'resources.csv';[IO.File]::AppendAllText($path,([IO.File]::ReadAllLines($path)[1]+[Environment]::NewLine))}
                'gpu_duplicate' {$path=Join-Path $casePath 'gpu.csv';[IO.File]::AppendAllText($path,([IO.File]::ReadAllLines($path)[1]+[Environment]::NewLine))}
                'gpu_bad_status' {$path=Join-Path $casePath 'gpu.csv';$rows=@(Import-Csv $path);$rows[0].status='invented';$rows | Export-Csv $path -NoTypeInformation -Encoding UTF8}
                'call_missing_resource' {$path=Join-Path $casePath 'calls.csv';$rows=@(Import-Csv $path);$rows[3].resource_id='999';$rows | Export-Csv $path -NoTypeInformation -Encoding UTF8}
                'call_missing_draw' {$path=Join-Path $casePath 'calls.csv';$rows=@(Import-Csv $path);$rows[5].operation='map';$rows[5].hresult_available='1';$rows[5].hresult='0';$rows | Export-Csv $path -NoTypeInformation -Encoding UTF8}
                'gpu_draw_count' {$path=Join-Path $casePath 'gpu.csv';$rows=@(Import-Csv $path);$rows[2].draw_calls='0';$rows | Export-Csv $path -NoTypeInformation -Encoding UTF8}
            }
            $bad=Analyze $badRoot;Assert ($bad['status'] -eq 'complete' -and $bad['resource_matrix']['status'] -eq 'partial' -and $bad['resource_matrix']['pairs'].Count -eq 0) ('matrix raw invalidity stays separate and cannot compare '+$rawFailure)
        }

    }
}
foreach ($gate in @('product_alive','state_unknown','explicit_skip')) {
    $root=Join-Path $testRoot ('gate-'+$gate);[void][IO.Directory]::CreateDirectory($root)
    $matrix=Invoke-PerfResourceMatrix $root $probeExe ('gate-'+$gate) (Get-PerfSha256 $probeExe) ($gate -eq 'explicit_skip') ($gate -ne 'state_unknown') ($gate -eq 'explicit_skip') 2 10 @()
    Assert ($matrix.status -eq 'skipped' -and -not [IO.Directory]::Exists((Join-Path $root 'resource-probes\_list'))) ('matrix no worker before product stop gate '+$gate)
}

$root=Join-Path $testRoot 'gate-changed-exe';[void][IO.Directory]::CreateDirectory($root)
$matrix=Invoke-PerfResourceMatrix $root $probeExe 'gate-changed-exe' ('0'*64) $true $true $false 2 10 @()
Assert ($matrix.status -eq 'partial' -and -not [IO.Directory]::Exists((Join-Path $root 'resource-probes\_list'))) 'replaced EXE SHA refused before any probe parameter'
$root=Join-Path $testRoot 'gate-live-process';[void][IO.Directory]::CreateDirectory($root);$ownIdentity=[Draw3PerfWire]::ProcessIdentity($PID)
$matrix=Invoke-PerfResourceMatrix $root $probeExe 'gate-live-process' (Get-PerfSha256 $probeExe) $true $true $false 2 10 @() @([IO.Path]::GetFileNameWithoutExtension($ownIdentity[0]))
Assert ($matrix.status -eq 'skipped' -and $matrix.reason -eq 'skipped_product_process_present' -and -not [IO.Directory]::Exists((Join-Path $root 'resource-probes\_list'))) 'live product-name observation prevents starting matrix without killing process'
