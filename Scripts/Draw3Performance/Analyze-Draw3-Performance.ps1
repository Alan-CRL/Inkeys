param([Parameter(Mandatory=$true)][string]$CaptureRoot)
$ErrorActionPreference='Stop'
$scriptDirectory=Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDirectory 'Performance.Common.ps1')
$CaptureRoot=[IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\')
$issues=New-Object Collections.ArrayList
$metrics=@{}; $frameIndex=@{}; $longFrames=New-Object Collections.ArrayList
$hosts=New-Object Collections.ArrayList; $phases=@{}; $toolPhases=@{}; $gpuStatuses=@{}
$totals=@{validation_issues=0L;frames_seen=0L;frames_retained=0L;frames_dropped=0L;inputs_seen=0L;inputs_retained=0L;inputs_dropped=0L;receipts_seen=0L;receipts_retained=0L;receipts_dropped=0L;proof_overflow=0L;invalid_intervals=0L;frames_csv_rows=0L;frames_valid=0L;frames_invalid=0L;input_csv_rows=0L;input_valid=0L;input_invalid=0L;present_attempted=0L;present_api_attempted=0L;present_success=0L;present_failure=0L;present_non_s_ok_status=0L;present_preparation_failure=0L;present_noop=0L;active_intervals=0L;idle_boundaries=0L;gpu_rows=0L;gpu_invalid=0L;analysis_truncated=0L}
$stageNames=@('control','ingress','model_reset','model_update','model_predict','geometry','snapshot','composite','upload_map','upload_copy','draw_submit','present','ulw_copy_submit','ulw_map','ulw_rows','ulw_update','deadline_wait','idle_wait')
$priorNames=@('ingress','model_reset','model_update','model_predict','upload_map','upload_copy','draw_submit')
$toolNames=@('Pen','HardPen','Highlighter','Eraser','Laser','SolidLine','DashedLine','OutlineRectangle','FilledRectangle')
$totals.gpu_non_render_candidates=0L; $totals.gpu_unmatched=0L
$captureSchema=1; $renderGroups=@{}; $graphicsTop=New-Object Collections.ArrayList
$graphics=@{coverage='not_collected_by_v1';calls_seen=0L;calls_retained=0L;calls_dropped=0L;slow_calls_seen=0L;slow_calls_retained=0L;slow_calls_dropped=0L;resources_seen=0L;resources_retained=0L;resources_dropped=0L;unique_detail_calls=0L;invalid_detail_calls=0L;group_calls_dropped=0L;site_collisions=0L;groups=(New-Object Collections.ArrayList);hosts=@{}}
$gpuCoverage=@{draw_frames=0L;ready_draw_samples=0L;ready_draw_frames=0L;empty_work_samples=0L;missing_marker_samples=0L;clipped_marker_samples=0L;unmatched_samples=0L;longest_covered_frame_work_ms=$null;primary_draw_frames=0L;selection_draw_frames=0L;ink_draw_frames=0L;legacy_inferred_samples=0L;geometry_samples=0L;composite_samples=0L;readback_samples=0L;stage_without_work=0L;prior_samples=0L;ready_prior_draw_segments=0L}
$graphicsRoles=@('unknown','ink_data','global_constants','highlighter_data','laser_data','composite_vertices','layer_texture','back_buffer','staging_readback','snapshot_texture','other');$graphicsPasses=@('unknown','initialization','geometry','snapshot','composite','present','recovery');$graphicsAttributions=@('frame','prior','split_prior_frame','cross_frame','unassigned')
$coveredGpuFrames=@{}; $graphicsOperations=@('map','write_copy','write_constant','unmap','bind','copy','clear','draw','present','other')
$rowBudget=262144; $frameBudget=65536; $metricBudget=262144; $hostBudget=16

function Add-Issue([string]$Message) { $totals.validation_issues++; if ($issues.Count -lt 256) { [void]$issues.Add($Message) } }
function Number($Row,[string]$Name,[bool]$Optional=$false) {
    $value=0L
    if ($Optional -and [string]$Row[$Name] -eq '') { return $null }
    if (-not [long]::TryParse([string]$Row[$Name],[Globalization.NumberStyles]::Integer,[Globalization.CultureInfo]::InvariantCulture,[ref]$value) -or $value -lt 0) { throw "数值非法: $Name" }
    return $value
}
function Flag($Row,[string]$Name) { $n=Number $Row $Name; if ($n -gt 1) { throw "布尔非法: $Name" }; return ($n -eq 1) }
function Signed($Row,[string]$Name) {
    $n=0L; if (-not [long]::TryParse([string]$Row[$Name],[ref]$n)) { throw "有符号数非法: $Name" }; return $n
}
function Add-Metric([string]$Name,[double]$Value) {
    if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value) -or $Value -lt 0) { throw "非有限指标: $Name" }
    if (-not $metrics.ContainsKey($Name)) { $metrics[$Name]=New-Object 'Collections.Generic.List[double]' }
    if ($metrics[$Name].Count -ge $metricBudget) { $totals.analysis_truncated++; return }
    $metrics[$Name].Add($Value)
}
function Add-Delta($Row,[string]$Start,[string]$End,[string]$Name,[double]$Frequency) {
    $a=Number $Row $Start; $b=Number $Row $End
    if ($a -eq 0 -or $b -eq 0) { return $false }
    # 跨线程负一 tick 也不伪装成零；纳入非法/量化不确定计数。
    if ($b -lt $a) { throw "时间逆序或量化不确定: $Start -> $End" }
    Add-Metric $Name (([double]($b-$a))*1000.0/$Frequency)
    return $true
}
function Count-Key($Map,[string]$Key) { if (-not $Map.ContainsKey($Key)) { $Map[$Key]=0L }; $Map[$Key]++ }
function Csv-Rows([string]$Path,[string[]]$Required,[scriptblock]$Action,[int]$Budget) {
    if (-not [IO.File]::Exists($Path)) { Add-Issue ('缺少 CSV: ' + $Path); return 0L }
    if ((New-Object IO.FileInfo($Path)).Length -gt 67108864) { Add-Issue ('CSV 超过 64 MiB，拒绝读取: ' + $Path); $totals.analysis_truncated++; return 0L }
    $reader=New-Object IO.StreamReader($Path,[Text.Encoding]::UTF8,$true)
    $rows=0L
    try {
        $line=$reader.ReadLine(); if ($null -eq $line -or $line.Length -gt 16384) { throw 'CSV header 缺失/过长' }
        $header=[Draw3PerfWire]::Csv($line); $columns=@{}
        for ($i=0; $i -lt $header.Length; $i++) { if ($columns.ContainsKey($header[$i])) { throw 'CSV 重复列' }; $columns[$header[$i]]=$i }
        foreach ($field in $Required) { if (-not $columns.ContainsKey($field)) { throw "CSV 缺列 $field" } }
        while ($null -ne ($line=$reader.ReadLine())) {
            if ($rows -ge $Budget) { $totals.analysis_truncated++; Add-Issue ('达到分析行数界限: ' + $Path); break }
            $rows++
            try {
                if ($line.Length -gt 16384) { throw 'CSV 行超过 16 KiB' }
                $cells=[Draw3PerfWire]::Csv($line); if ($cells.Length -ne $header.Length) { throw 'CSV 列数不符' }
                $row=@{}; for ($i=0; $i -lt $header.Length; $i++) { $row[$header[$i]]=$cells[$i] }
                & $Action $row
            } catch { Add-Issue ([IO.Path]::GetFileName($Path) + ' 行 ' + $rows + ': ' + $_.Exception.Message); & $Action $null }
        }
    } catch { Add-Issue ($Path + ': ' + $_.Exception.Message) }
    finally { $reader.Dispose() }
    return $rows
}
function Distribution($Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) { return @{count=0;mean_ms=$null;p50_ms=$null;p90_ms=$null;p95_ms=$null;p99_ms=$null;max_ms=$null;standard_deviation_ms=$null;p99_reason='无有效样本'} }
    $array=$Values.ToArray(); [Array]::Sort($array); $n=$array.Length; $sum=0.0; $squares=0.0
    foreach ($v in $array) { $sum+=$v; $squares+=$v*$v }; $mean=$sum/$n
    $p99=$null; $reason='少于 1000 个有效样本，不报告 P99'
    if ($n -ge 1000) { $p99=$array[[int][Math]::Ceiling($n*0.99)-1]; $reason=$null }
    return @{count=$n;mean_ms=$mean;p50_ms=$array[[int][Math]::Ceiling($n*0.50)-1];p90_ms=$array[[int][Math]::Ceiling($n*0.90)-1];p95_ms=$array[[int][Math]::Ceiling($n*0.95)-1];p99_ms=$p99;max_ms=$array[$n-1];standard_deviation_ms=[Math]::Sqrt([Math]::Max(0.0,$squares/$n-$mean*$mean));p99_reason=$reason}
}
function Display-Number($Number) { if ($null -eq $Number) { return '不可用' }; return ([double]$Number).ToString('0.###',[Globalization.CultureInfo]::InvariantCulture) }
function Csv-Cell($Value) { return '"' + ([string]$Value).Replace('"','""') + '"' }

function Save-AnalysisCsv([string]$Path,$Columns,$Rows) {
    $writer=New-Object IO.StreamWriter($Path,$false,(New-Object Text.UTF8Encoding($true)))
    try { $writer.WriteLine(($Columns -join ',')); foreach ($item in $Rows) { $cells=@(); foreach ($name in $Columns) { $value=$item[$name]; if ($value -is [double]) {$value=$value.ToString('R',[Globalization.CultureInfo]::InvariantCulture)}; $cells+=(Csv-Cell $value) }; $writer.WriteLine(($cells -join ',')) } }
    finally {$writer.Dispose()}
}
function Keep-Slowest($List,$Item,[string]$Field,[int]$Limit=100) {
    if ($List.Count -lt $Limit) { [void]$List.Add($Item); return }
    $minimum=0; for ($index=1;$index -lt $List.Count;$index++) { if ($List[$index][$Field] -lt $List[$minimum][$Field]) {$minimum=$index} }
    if ($Item[$Field] -gt $List[$minimum][$Field]) {$List[$minimum]=$Item}
}
function Analyze-GraphicsHost([string]$Directory,[string]$HostName,$Session,[double]$Frequency) {
    $hc=@{resources=@{};detail_groups=@{};invalid=0L;prefix_rows=0L;slow_rows=0L;group_rows=0L;resource_rows=0L;detail_policy=$Session['graphics_detail_policy'];first_retained_qpc=$Session['first_retained_call_qpc'];last_retained_qpc=$Session['last_retained_call_qpc'];detail_dropped_after_qpc=$Session['graphics_detail_dropped_after_qpc'];sites=@{}}
    $graphics.hosts[$HostName]=$hc
    try {
        foreach ($kind in @('graphics_calls','slow_graphics_calls','resources')) {
            $seen=Get-PerfJsonInteger $Session ($kind+'_seen'); $retained=Get-PerfJsonInteger $Session ($kind+'_retained'); $dropped=Get-PerfJsonInteger $Session ($kind+'_dropped')
            if ($seen -ne ($retained+$dropped)) { throw ($kind+' seen != retained+dropped') }
            $target=$kind; if ($kind -eq 'graphics_calls') {$target='calls'}; if ($kind -eq 'slow_graphics_calls') {$target='slow_calls'}
            $graphics[$target+'_seen']+=$seen; $graphics[$target+'_retained']+=$retained; $graphics[$target+'_dropped']+=$dropped
            if ($dropped -gt 0 -and ($kind -ne 'slow_graphics_calls' -or $Session['graphics_calls_dropped'] -gt 0)) { $graphics.coverage='partial'; Add-Issue ($HostName+' '+$kind+' 容量满，明细仅覆盖有限前缀') }
        }
        [void](Get-PerfJsonInteger $Session 'graphics_call_capacity' 1 8192); [void](Get-PerfJsonInteger $Session 'slow_graphics_call_capacity' 1 4096); [void](Get-PerfJsonInteger $Session 'resource_capacity' 1 512); [void](Get-PerfJsonInteger $Session 'graphics_group_capacity' 1 2048)
        foreach ($pair in @(@('graphics_calls_retained','graphics_call_capacity'),@('slow_graphics_calls_retained','slow_graphics_call_capacity'),@('resources_retained','resource_capacity'),@('graphics_groups_retained','graphics_group_capacity'))) {if ((Get-PerfJsonInteger $Session $pair[0]) -gt (Get-PerfJsonInteger $Session $pair[1])) {throw ('retained超过声明capacity: '+$pair[0])}}
        if ((Get-PerfJsonString $Session 'graphics_accounting') -cne 'non_nested_leaf_intervals_split_at_frame_start' -or (Get-PerfJsonString $Session 'graphics_detail_policy') -cne 'prefix_plus_independent_slow_prefix') { throw 'graphics accounting/detail policy 不支持' }
        foreach ($field in @('graphics_group_calls_dropped','graphics_invalid_intervals','graphics_nested_calls','graphics_unknown_resources','graphics_site_collisions','graphics_cross_frame_calls','graphics_group_overflows','graphics_arithmetic_overflows')) { $hc[$field]=Get-PerfJsonInteger $Session $field }
        $graphics.group_calls_dropped+=$hc.graphics_group_calls_dropped;$graphics.site_collisions+=$hc.graphics_site_collisions
        foreach ($field in @('graphics_group_calls_dropped','graphics_invalid_intervals','graphics_nested_calls','graphics_site_collisions','graphics_cross_frame_calls','graphics_group_overflows','graphics_arithmetic_overflows')) { if ($hc[$field] -gt 0) {Add-Issue ($HostName+' '+$field+'='+$hc[$field]+'，覆盖为部分证据');$graphics.coverage='partial'} }
        $allocated=Get-PerfJsonInteger $Session 'allocated_payload_bytes'; $budget=Get-PerfJsonInteger $Session 'payload_budget_bytes' 1 33554432; $legacy=Get-PerfJsonInteger $Session 'legacy_payload_upper_bound_bytes'
        if ($allocated -gt $budget -or ($allocated+$legacy) -gt 67108864) {throw '固定 payload 账本超出32/64MiB合同'}
        $componentSum=0L;if ($Session['payload_components'] -isnot [Collections.IDictionary]) {throw 'v2缺少payload_components实账'}
        foreach ($component in $Session['payload_components'].Values) {$rowBytes=Get-PerfJsonInteger $component 'row_bytes';$capacity=Get-PerfJsonInteger $component 'capacity';$bytes=Get-PerfJsonInteger $component 'bytes';if ([decimal]$rowBytes*$capacity -ne $bytes) {throw 'payload component sizeof×capacity不符'};$componentSum+=$bytes}
        if ($componentSum -ne $allocated) {throw 'payload_components总和与allocated_payload_bytes不符'}
        if ($Session['graphics_sites'] -isnot [Collections.IList] -or $Session['graphics_sites'].Count -gt 512) {throw 'graphics_sites缺失或超界'}
        foreach ($site in $Session['graphics_sites']) {$siteId=Get-PerfJsonInteger $site 'site_id' 1 4294967295;$siteName=Get-PerfJsonString $site 'name';if ($hc.sites.ContainsKey([string]$siteId)) {throw '重复site_id'};$hc.sites[[string]$siteId]=$siteName}
    } catch { Add-Issue ($HostName+' graphics session: '+$_.Exception.Message); $graphics.coverage='partial' }
    $resourceColumns=@('resource_id','device_epoch','resource_kind','resource_role','site_id','descriptor_available','byte_width','width','height','format','usage','bind_flags','cpu_access_flags','misc_flags','structure_byte_stride','created_qpc','destroyed_qpc','create_hresult')
    $resourceAction={param($row)
        if ($null -eq $row) {$hc.invalid++;return}
        foreach ($name in @('resource_id','device_epoch','site_id','byte_width','width','height','format','usage','bind_flags','cpu_access_flags','misc_flags','structure_byte_stride','created_qpc','destroyed_qpc')) {[void](Number $row $name)}
        [void](Signed $row 'create_hresult'); [void](Flag $row 'descriptor_available')
        $id=Number $row 'resource_id'; if ($id -eq 0 -or $hc.resources.ContainsKey([string]$id)) {throw '资源ID为0或重复'}
        if (@('unknown','buffer','texture1d','texture2d','texture3d') -cnotcontains [string]$row.resource_kind) {throw 'resource_kind 非法'}
        if ($graphicsRoles -cnotcontains [string]$row.resource_role) {throw 'resource role枚举非法'}
        if (-not $hc.sites.ContainsKey([string]$row.site_id)) {Add-Issue ($HostName+' resource '+$id+':site字典缺失');$graphics.coverage='partial'}
        $hc.resources[[string]$id]=$row
    }
    $hc.resource_rows=Csv-Rows (Join-Path $Directory 'resources.csv') $resourceColumns $resourceAction 512
    if ($hc.resource_rows -ne $Session['resources_retained']) {Add-Issue ($HostName+' resources CSV行数与retained不符')}
    $columns=@('call_serial','frame_serial','device_epoch','resource_id','related_resource_id','site_id','operation','resource_role','pass','start_qpc','end_qpc','valid_interval','hresult_available','hresult','subresource','map_type','map_flags','offset_bytes','size_bytes','argument0','argument1','parent_call_serial','attribution','accounting_valid','resource_available','depth')
    $details=@{}
    foreach ($stream in @('graphics-calls.csv','slow-graphics-calls.csv')) {
        $streamSeen=@{}
        $callAction={param($row)
            if ($null -eq $row) {$hc.invalid++;$graphics.invalid_detail_calls++;return}
            foreach ($name in @('call_serial','frame_serial','device_epoch','resource_id','related_resource_id','site_id','start_qpc','end_qpc','subresource','map_type','map_flags','offset_bytes','size_bytes','argument0','argument1','parent_call_serial','depth')) {[void](Number $row $name)}
            foreach ($name in @('valid_interval','accounting_valid','hresult_available','resource_available')) {[void](Flag $row $name)}
            $serial=Number $row 'call_serial'; if ($serial -eq 0 -or $streamSeen.ContainsKey([string]$serial)) {throw '重复调用ID'}; $streamSeen[[string]$serial]=$true
            if ($details.ContainsKey([string]$serial)) { foreach ($name in $columns) {if ([string]$row[$name] -cne [string]$details[[string]$serial][$name]) {throw '完整/慢前缀的同调用原始字段不符'}}; return }
            $details[[string]$serial]=$row; $graphics.unique_detail_calls++
            if ($graphicsOperations -cnotcontains [string]$row.operation -or $graphicsAttributions -cnotcontains [string]$row.attribution -or $graphicsRoles -cnotcontains [string]$row.resource_role -or $graphicsPasses -cnotcontains [string]$row.pass) {throw 'call operation/role/pass/attribution枚举非法'}
            $a=Number $row 'start_qpc'; $b=Number $row 'end_qpc'; $valid=Flag $row 'valid_interval'; $accounting=Flag $row 'accounting_valid'; $hr=$null
            if (Flag $row 'hresult_available') {$hr=Signed $row 'hresult'} elseif ([string]$row.hresult -ne '') {throw 'void API 不应伪造HRESULT'}
            if ($valid -and ($a -eq 0 -or $b -lt $a)) {throw '有效调用却缺少单调时钟'}
            if ($accounting -and (-not $valid -or (Number $row 'parent_call_serial') -ne 0)) {throw 'nested/非法区间不能声明accounting_valid'}
            $duration=$null; if ($valid) {$duration=[double]($b-$a)*1000.0/$Frequency}
            $resource=$hc.resources[[string]$row.resource_id]
            if ($resource -and (Number $resource 'device_epoch') -ne (Number $row 'device_epoch')) {throw 'resource epoch不符'}
            if ((Flag $row 'resource_available') -and ((Number $row 'resource_id') -eq 0 -or -not $resource)) {Add-Issue ($HostName+' call '+$serial+':资源描述前缀缺失');$graphics.coverage='partial'}
            $facts=$frameIndex[$HostName+':'+$row.frame_serial]
            if ($facts -and $facts.epoch -ne (Number $row 'device_epoch')) {throw 'call frame epoch不符'}
            if (-not $hc.sites.ContainsKey([string]$row.site_id)) {Add-Issue ($HostName+' call '+$serial+':site字典缺失');$graphics.coverage='partial'}
            $entry=@{host=$HostName;site=$hc.sites[[string]$row.site_id];source_file=$stream;source_row=$streamSeen.Count;call_serial=$serial;frame_serial=$row.frame_serial;device_epoch=$row.device_epoch;resource_id=$row.resource_id;related_resource_id=$row.related_resource_id;resource_role=$row.resource_role;operation=$row.operation;pass=$row.pass;site_id=$row.site_id;attribution=$row.attribution;duration_ms=$duration;valid_interval=$valid;accounting_valid=$accounting;hresult_available=$row.hresult_available;hresult=$hr;map_type=$row.map_type;map_flags=$row.map_flags;offset_bytes=$row.offset_bytes;size_bytes=$row.size_bytes;resource_byte_width=$null;frame_work_ms=$null;frame_owner_cpu_ms=$null;frame_draw_calls=$null;frame_map_calls=$null;frame_consumed_count=$null;output_target=$null;tool_mask=$null;start_qpc=$a;end_qpc=$b;parent_call_serial=$row.parent_call_serial}
            if ($resource) {$entry.resource_byte_width=$resource.byte_width}
            if ($facts) {
                $entry.frame_work_ms=$facts.work_ms;$entry.frame_owner_cpu_ms=$facts.thread_cpu_ms;$entry.frame_draw_calls=$facts.draw_calls;$entry.frame_map_calls=$facts.map_calls;$entry.frame_consumed_count=$facts.consumed_count;$entry.output_target=$facts.target;$entry.tool_mask=$facts.tool_mask
                if ($accounting -and @('frame','split_prior_frame') -ccontains [string]$row.attribution) {
                    $left=[Math]::Max($a,$facts.start);$right=[Math]::Min($b,$facts.end)
                    if ($b -gt $facts.end -or ($row.attribution -eq 'frame' -and $a -lt $facts.start)) {throw '调用帧归属边界不符'}
                    if ($right -ge $left) {[void]$facts.call_intervals.Add(@{start=$left;end=$right})}
                }
                if ($valid) {Keep-Slowest $facts.top_calls $entry 'duration_ms' 3}
            } elseif ($row.attribution -eq 'frame' -and (Number $row 'frame_serial') -gt 0) {Add-Issue ($HostName+' call '+$serial+':关联帧未保留');$graphics.coverage='partial'}
            if ($valid) {
                Keep-Slowest $graphicsTop $entry 'duration_ms'
                $key=$row.site_id+'|'+$row.operation+'|'+$row.resource_role+'|'+$row.pass+'|'+$row.attribution
                if (-not $hc.detail_groups.ContainsKey($key)) {$hc.detail_groups[$key]=@{values=(New-Object 'Collections.Generic.List[double]');calls=0L;failed=0L;bytes=0L}}
                $g=$hc.detail_groups[$key];$g.calls++;$g.values.Add($duration);$g.bytes+=(Number $row 'size_bytes');if ($null -ne $hr -and $hr -lt 0) {$g.failed++}
            } else {$graphics.invalid_detail_calls++}
        }
        $limit=8192;if ($stream -eq 'slow-graphics-calls.csv') {$limit=4096}
        $rows=Csv-Rows (Join-Path $Directory $stream) $columns $callAction $limit
        $kind='graphics_calls';if ($stream -eq 'slow-graphics-calls.csv') {$kind='slow_graphics_calls';$hc.slow_rows=$rows} else {$hc.prefix_rows=$rows}
        if ($rows -ne $Session[$kind+'_retained']) {Add-Issue ($HostName+' '+$stream+' 行数与retained不符')}
    }
    $groupSeen=@{};$groupKeys=@{};$hc.group_call_sum=0L
    $groupColumns=@('group_id','site_id','operation','resource_role','pass','attribution','calls_seen','valid_calls','invalid_calls','failed_calls','hresult_calls','size_bytes','total_qpc','min_qpc','max_qpc','max_call_serial','max_frame_serial','arithmetic_overflow')
    $groupAction={param($row)
        if ($null -eq $row) {$hc.invalid++;return}
        foreach ($name in @('group_id','site_id','calls_seen','valid_calls','invalid_calls','failed_calls','hresult_calls','size_bytes','total_qpc','max_call_serial','max_frame_serial')) {[void](Number $row $name)}; [void](Flag $row 'arithmetic_overflow')
        if ((Number $row 'valid_calls') -gt 0) {[void](Number $row 'min_qpc');[void](Number $row 'max_qpc')}
        $id=[string]$row.group_id;if ($groupSeen.ContainsKey($id)) {throw '重复group_id'};$groupSeen[$id]=$true
        if ((Number $row 'group_id') -eq 0 -or $graphicsOperations -cnotcontains [string]$row.operation -or $graphicsRoles -cnotcontains [string]$row.resource_role -or $graphicsPasses -cnotcontains [string]$row.pass -or $graphicsAttributions -cnotcontains [string]$row.attribution) {throw 'group ID/枚举非法'}
        if (-not $hc.sites.ContainsKey([string]$row.site_id)) {Add-Issue ($HostName+' group '+$id+':site字典缺失');$graphics.coverage='partial'}
        if ((Number $row 'calls_seen') -ne ((Number $row 'valid_calls')+(Number $row 'invalid_calls')) -or (Number $row 'failed_calls') -gt (Number $row 'hresult_calls') -or (Number $row 'hresult_calls') -gt (Number $row 'calls_seen')) {throw 'graphics group 分母不符'}
        $key=$row.site_id+'|'+$row.operation+'|'+$row.resource_role+'|'+$row.pass+'|'+$row.attribution
        if ($groupKeys.ContainsKey($key)) {throw '同group key换ID重复'};$groupKeys[$key]=$true
        if ((Number $row 'valid_calls') -gt 0 -and -not (Flag $row 'arithmetic_overflow')) {$minimum=Number $row 'min_qpc';$maximum=Number $row 'max_qpc';$total=Number $row 'total_qpc';$validCount=Number $row 'valid_calls';if ($minimum -gt $maximum -or [decimal]$minimum*$validCount -gt $total -or [decimal]$maximum*$validCount -lt $total) {throw 'group min/max/total不符'}}
        $d=Distribution $null;if ($hc.detail_groups.ContainsKey($key)) {$d=Distribution $hc.detail_groups[$key].values}
        $g=@{host=$HostName;site=$hc.sites[[string]$row.site_id];group_id=$row.group_id;site_id=$row.site_id;operation=$row.operation;resource_role=$row.resource_role;pass=$row.pass;attribution=$row.attribution;calls_seen=(Number $row 'calls_seen');valid_calls=(Number $row 'valid_calls');invalid_calls=(Number $row 'invalid_calls');failed_calls=(Number $row 'failed_calls');hresult_calls=(Number $row 'hresult_calls');size_bytes=(Number $row 'size_bytes');total_ms=$null;min_ms=$null;max_ms=$null;max_call_serial=$row.max_call_serial;max_frame_serial=$row.max_frame_serial;detail_distribution=$d;detail_samples=$d.count;detail_p50_ms=$d.p50_ms;detail_p95_ms=$d.p95_ms;detail_max_ms=$d.max_ms;percentile_scope='deduplicated_prefix_plus_slow_prefix_selection_biased'}
        if (-not (Flag $row 'arithmetic_overflow') -and $g.valid_calls -gt 0) {$g.total_ms=[double](Number $row 'total_qpc')*1000.0/$Frequency;$g.min_ms=[double](Number $row 'min_qpc')*1000.0/$Frequency;$g.max_ms=[double](Number $row 'max_qpc')*1000.0/$Frequency}
        $hc.group_call_sum+=$g.calls_seen
        [void]$graphics.groups.Add($g)
    }
    $hc.group_rows=Csv-Rows (Join-Path $Directory 'graphics-groups.csv') $groupColumns $groupAction 2048
    if ($hc.group_rows -ne $Session['graphics_groups_retained']) {Add-Issue ($HostName+' graphics groups 行数与retained不符')}
    if ($hc.group_call_sum -ne ($Session['graphics_calls_seen']-$Session['graphics_group_calls_dropped'])) {Add-Issue ($HostName+' graphics groups全程调用数与seen/group-dropped不符')}
    $hc.resource_descriptors=$hc.resources.Count;$hc.detail_group_count=$hc.detail_groups.Count;$hc.site_count=$hc.sites.Count;$hc.Remove('resources');$hc.Remove('detail_groups');$hc.Remove('sites')
    foreach ($fact in $frameIndex.Values) {
        if ($fact.host -ne $HostName) {continue}
        $union=0L;$right=$null
        foreach ($part in @($fact.call_intervals | Sort-Object -Property @{Expression={$_.start}})) {if ($null -eq $right -or $part.start -gt $right) {$union+=$part.end-$part.start;$right=$part.end} elseif ($part.end -gt $right) {$union+=$part.end-$right;$right=$part.end}}
        $fact.detail_union_ms=[double]$union*1000.0/$Frequency
        $fact.detail_unattributed_ms=$fact.work_ms-$fact.detail_union_ms
        $fact.detail_union_policy='retained_accounting_valid_leaf_union_within_frame_only'
        $labels=@();foreach ($call in @($fact.top_calls | Sort-Object -Property @{Expression={$_.duration_ms};Descending=$true})) {$labels+=($call.operation+'/'+$call.resource_role+'#'+$call.resource_id+'@'+$call.site_id+' '+(Display-Number $call.duration_ms)+'ms')};$fact.top_calls_text=$labels -join '; '
        if ($fact.entry) {$fact.entry.graphics_leaf_ms=$fact.graphics_leaf_ms;$fact.entry.graphics_unattributed_ms=$fact.graphics_residual_ms;$fact.entry.detail_union_ms=$fact.detail_union_ms;$fact.entry.top_graphics_calls=$fact.top_calls_text}
        # 报告仅保留有限明细的并集，不用它补造已饱和后的调用时间。
        $fact.Remove('call_intervals');$fact.Remove('top_calls')
    }
}
function Write-GraphicsAnalysisFiles([string]$Root) {
    Save-AnalysisCsv (Join-Path $Root 'graphics-call-summary.csv') @('host','group_id','site_id','site','operation','resource_role','pass','attribution','calls_seen','valid_calls','invalid_calls','failed_calls','hresult_calls','size_bytes','total_ms','min_ms','max_ms','max_call_serial','max_frame_serial','detail_samples','detail_p50_ms','detail_p95_ms','detail_max_ms','percentile_scope') @($graphics.groups | Sort-Object -Property @{Expression={$_.total_ms};Descending=$true})
    Save-AnalysisCsv (Join-Path $Root 'top-long-graphics-calls.csv') @('host','site','source_file','source_row','call_serial','frame_serial','device_epoch','resource_id','related_resource_id','resource_role','operation','pass','site_id','attribution','duration_ms','valid_interval','accounting_valid','hresult_available','hresult','map_type','map_flags','offset_bytes','size_bytes','resource_byte_width','frame_work_ms','frame_owner_cpu_ms','frame_draw_calls','frame_map_calls','frame_consumed_count','output_target','tool_mask','start_qpc','end_qpc','parent_call_serial') @($graphicsTop | Sort-Object -Property @{Expression={$_.duration_ms};Descending=$true})
    Save-AnalysisCsv (Join-Path $Root 'frame-graphics-summary.csv') @('host','serial','epoch','target','tool_mask','draw_calls','map_calls','consumed_count','work_ms','thread_cpu_ms','graphics_summary_valid','graphics_call_count','graphics_invalid_count','graphics_invalid_mask','graphics_leaf_ms','graphics_residual_ms','detail_union_ms','detail_unattributed_ms','detail_union_policy','top_calls_text') @($frameIndex.Values | Sort-Object host,serial)
}

function Analyze-ResourceMatrix([string]$Root,$Launch) {
    # 对照失败与产品采集完整性分开；其 CSV 错误写入矩阵自己的 issues。
    $issues=New-Object Collections.ArrayList; $totals=@{analysis_truncated=0L;validation_issues=0L}
    $ms=@{status='not_collected';reason='没有资源矩阵；v1原包未采集该对照';cases=(New-Object Collections.ArrayList);pairs=(New-Object Collections.ArrayList);issues=$issues}
    $matrixRoot=Join-Path $Root 'resource-probes';$matrixPath=Join-Path $matrixRoot 'matrix.json'
    $csvRows=New-Object Collections.ArrayList
    if (-not [IO.File]::Exists($matrixPath)) {Save-AnalysisCsv (Join-Path $Root 'resource-probe-comparison.csv') @('case_id','status','reason','identity_verified','measure_api_sum_ms','hardware_to_warp_measure_ratio') @();return $ms}
    try {
        $matrix=Read-PerfJson $matrixPath;[void](Get-PerfJsonInteger $matrix 'schema_version' 1 1);[void](Get-PerfJsonInteger $matrix 'probe_revision' 2 2)
        $ms.status=[string]$matrix['status'];$ms.reason=[string]$matrix['reason'];$ms['profile']=$matrix['profile']
        if (@('complete','partial','skipped','running') -cnotcontains $ms.status) {throw '未知matrix status'}
        if ($ms.status -eq 'skipped') {return $ms}
        if ($ms.status -eq 'running') {$ms.status='partial';$ms.reason='父矩阵未完成，保留已落盘case'}
        if (-not $Launch -or (Get-PerfJsonString $matrix 'session_id') -cne [string]$Launch['session_id'] -or (Get-PerfJsonString $matrix 'exe_path') -cne [string]$Launch['exe_path']) {throw 'matrix与原始产品会话身份不符'}
        if ($matrix['product_stopped_confirmed'] -isnot [bool] -or $matrix['product_stopped_confirmed'] -ne $true -or $matrix['normal_shutdown'] -isnot [bool] -or $matrix['normal_shutdown'] -ne $true) {throw 'matrix缺少产品已停止/正常退出门证据'}
        $expected=$matrix['expected_cases'];$entries=$matrix['cases'];$list=$matrix['list']
        if ($expected -isnot [Collections.IList] -or $entries -isnot [Collections.IList] -or $expected.Count -gt 64 -or $entries.Count -gt 64) {throw 'matrix case列表超界/类型非法'}
        if ($expected.Count -eq 0) {return $ms}
        [void](Get-PerfJsonInteger $list 'schema_version' 1 1);[void](Get-PerfJsonInteger $list 'probe_revision' 2 2)
        if ($list['capability_marker'] -cne 'INKEYS_DRAW3_DIAGNOSTICS_V2' -or $list['profile'] -cne 'draw3-resource-v2-fixed' -or (Get-PerfJsonInteger $list 'case_count' 1 64) -ne $expected.Count) {throw 'matrix list能力/版本/数量不符'}
        $definitions=@{};$caseDirectories=@{};$caseOrdinal=0;foreach ($case in $expected) {$id=Get-PerfJsonString $case 'case_id';if ($id -cnotmatch '^[a-z0-9][a-z0-9_-]{0,95}$' -or $definitions.ContainsKey($id)) {throw 'matrix重复或非法case ID'};$definitions[$id]=$case;$caseOrdinal++;$caseDirectories[$id]='c'+$caseOrdinal.ToString('D3')}
        $seen=@{};$profiles=@{}
        foreach ($entry in $entries) {
            $id=[string]$entry['case_id'];$case=$definitions[$id]
            if (-not $case -or $seen.ContainsKey($id)) {throw 'matrix未知/重复case结果'};$seen[$id]=$true
            $caseSummary=@{case_id=$id;profile_id=$case['profile_id'];requested_backend=$case['requested_backend'];status=$entry['status'];reason=$entry['reason'];identity_verified=$false;resource_bytes=$case['resource_bytes'];write_bytes=$case['write_bytes'];draws_per_iteration=$case['draws_per_iteration'];update_method=$case['update_method'];constant_write_method=$case['constant_write_method'];cold_api_sum_ms=$null;warmup_api_sum_ms=$null;measure_api_sum_ms=$null;drain_api_sum_ms=$null;thread_cpu_ms=$null;process_cpu_ms=$null;hardware_to_warp_measure_ratio=$null;operations=@{};gpu_status=$null}
            [void]$ms.cases.Add($caseSummary);[void]$csvRows.Add($caseSummary)
            if ($entry['status'] -eq 'skipped') {continue}
            $caseLeaf=Get-PerfJsonString $entry 'directory';if ($caseLeaf -cnotmatch '^c[0-9]{3}$' -or $caseLeaf -cne $caseDirectories[$id]) {throw 'matrix case相对目录非法/顺序不符'}
            $caseDirectory=Join-Path $matrixRoot $caseLeaf
            try {
                $request=Read-PerfJson (Join-Path $caseDirectory 'request.json');$observed=Read-PerfJson (Join-Path $caseDirectory 'worker-status.json')
                [void](Get-PerfJsonInteger $observed 'schema_version' 1 1);[void](Get-PerfJsonInteger $observed 'pid' 1 2147483647)
                if ((Get-PerfJsonString $request 'session_id') -cne [string]$matrix['session_id'] -or (Get-PerfJsonString $request 'case_id') -cne $id -or (Get-PerfJsonString $request 'exe_sha256') -cne (Get-PerfJsonString $matrix 'exe_sha256') -or (Get-PerfJsonString $request 'exe_path') -cne [string]$matrix['exe_path']) {throw 'request/parent会话或EXE SHA身份不符'}
                $originalDirectory=Get-PerfJsonString $request 'output_directory'
                $start=Read-PerfJson (Join-Path $caseDirectory 'probe-start.json');$result=Read-PerfJson (Join-Path $caseDirectory 'probe-result.json')
                $caseSummary.status=Test-PerfProbeResult $result $start $list $case ([string]$matrix['session_id']) $originalDirectory $observed $caseDirectory
                $caseSummary.identity_verified=$true;$caseSummary.reason=$result['reason'];$caseSummary.thread_cpu_ms=$result['thread_cpu_ms'];$caseSummary.process_cpu_ms=$result['process_cpu_ms'];$caseSummary.gpu_status=$result['gpu_status']
                foreach ($field in @('status','exit_code','pid','process_start_filetime_utc','exe_path','stopped_confirmed')) {if ([string]$observed[$field] -cne [string]$entry['worker'][$field]) {throw ('matrix/worker-status不符: '+$field)}}
                $frequency=[double](Get-PerfJsonInteger $result 'qpc_frequency');$phaseSums=@{cold=0.0;warmup=0.0;measure=0.0;drain=0.0;initialization=0.0};$phaseCounts=@{cold=0L;warmup=0L;measure=0L;drain=0L;initialization=0L};$phaseIterations=@{cold=@{};warmup=@{};measure=@{};drain=@{};initialization=@{}};$callIds=@{};$rawDrawCounts=@{};$gpuDrawCounts=@{}
                $before=[long]$totals.validation_issues;$resourceTable=@{}
                $resourceAction={param($resource)
                    if ($null -eq $resource) {return}
                    foreach ($field in @('resource_id','byte_width','width','height','format','usage','bind_flags','cpu_access_flags','misc_flags','structure_byte_stride')) {[void](Number $resource $field)};[void](Signed $resource 'create_hresult')
                    $resourceId=Number $resource 'resource_id';if ($resourceId -eq 0 -or $resourceTable.ContainsKey([string]$resourceId)) {throw 'probe resource ID为0/重复'}
                    if (@('buffer','texture2d') -cnotcontains [string]$resource.resource_kind -or -not [string]$resource.role) {throw 'probe resource kind/role非法'}
                    $resourceTable[[string]$resourceId]=$resource
                }
                $resourceRows=Csv-Rows (Join-Path $caseDirectory 'resources.csv') @('resource_id','role','resource_kind','byte_width','width','height','format','usage','bind_flags','cpu_access_flags','misc_flags','structure_byte_stride','create_hresult') $resourceAction 64
                $caseSummary['resources']=$resourceTable.Values;$caseSummary['resource_rows']=$resourceRows
                if ($caseSummary.status -eq 'complete' -and $resourceRows -eq 0) {throw 'probe complete缺少资源描述'}
                $probeGpu=@{rows=0L;statuses=@{};ready_samples=0L;ready_draw_samples=0L;ready_measure_draw_samples=0L;draw_calls=0L;empty_or_unmeasured_samples=0L;command_interval=$null;draw_command_interval=$null;evidence_reason='没有ready且带真实Draw/完整marker的GPU样本；不填0'}
                $gpuValues=New-Object 'Collections.Generic.List[double]';$gpuDrawValues=New-Object 'Collections.Generic.List[double]';$gpuIds=@{}
                $probeGpuAction={param($sample)
                    if ($null -eq $sample) {return}
                    foreach ($field in @('iteration','frequency','begin_timestamp','end_timestamp','draw_calls','marker_mask')) {[void](Number $sample $field)}
                    $phase=[string]$sample.phase;$iteration=Number $sample 'iteration'
                    if (@('cold','warmup','measure') -cnotcontains $phase -or $iteration -gt 5 -or ($phase -eq 'cold' -and $iteration -ne 0) -or ($phase -eq 'warmup' -and $iteration -ne 1) -or ($phase -eq 'measure' -and $iteration -lt 2) -or $gpuIds.ContainsKey([string]$iteration)) {throw 'probe GPU phase/iteration重复或非法'}
                    $gpuIds[[string]$iteration]=$true;$gpuStatus=[string]$sample.status;$gpuDrawCounts[$phase+'/'+$iteration]=(Number $sample 'draw_calls')
                    if (@('not_started','ready','pending','disjoint','invalid','query_failed') -cnotcontains $gpuStatus) {throw 'probe GPU未知状态'}
                    $probeGpu.rows++;Count-Key $probeGpu.statuses $gpuStatus;$probeGpu.draw_calls+=(Number $sample 'draw_calls')
                    if ((Number $sample 'marker_mask') -gt 3) {throw 'probe GPU marker mask非法'}
                    if ($gpuStatus -eq 'ready') {
                        $freq=Number $sample 'frequency';$a=Number $sample 'begin_timestamp';$b=Number $sample 'end_timestamp'
                        if ($freq -le 0 -or $a -eq 0 -or $b -lt $a -or (Number $sample 'marker_mask') -ne 3) {throw 'probe ready GPU缺少有效端点/frequency/marker'}
                        $ms=[double]($b-$a)*1000.0/$freq;$probeGpu.ready_samples++;$gpuValues.Add($ms)
                        if ((Number $sample 'draw_calls') -gt 0) {$probeGpu.ready_draw_samples++;$gpuDrawValues.Add($ms);if ($phase -eq 'measure') {$probeGpu.ready_measure_draw_samples++}}
                        else {$probeGpu.empty_or_unmeasured_samples++}
                    } else {$probeGpu.empty_or_unmeasured_samples++}
                }
                $probeGpuRows=Csv-Rows (Join-Path $caseDirectory 'gpu.csv') @('phase','iteration','status','frequency','begin_timestamp','end_timestamp','draw_calls','marker_mask') $probeGpuAction 6
                if ($probeGpuRows -ne 6 -or $gpuIds.Count -ne 6) {throw 'probe GPU固定6个阶段行缺失/多余'}
                $probeGpu.command_interval=Distribution $gpuValues;$probeGpu.draw_command_interval=Distribution $gpuDrawValues
                if ($probeGpu.ready_draw_samples -gt 0) {$probeGpu.evidence_reason='实际Draw的immediate命令流区间，含CPU提交空隙；不是shader纯忙时'}
                if ($probeGpu.ready_samples -gt 0 -and $probeGpu.ready_draw_samples -eq 0) {$probeGpu.evidence_reason='仅clear/copy/readback命令窗口，没有Draw；不能评价shader'}
                if ($Case['draws_per_iteration'] -eq 0 -and $probeGpu.statuses['not_started'] -eq 6) {$probeGpu.evidence_reason='CPU-only更新对照未开启GPU query；not_started合法且没有GPU时间'}
                $caseSummary['gpu_coverage']=$probeGpu
                $caseSummary['gpu_ready_draw_samples']=$probeGpu.ready_draw_samples;$caseSummary['gpu_ready_samples']=$probeGpu.ready_samples;$caseSummary['gpu_nonready_samples']=$probeGpu.rows-$probeGpu.ready_samples;$caseSummary['gpu_evidence_reason']=$probeGpu.evidence_reason
                $rawAction={param($call)
                    if ($null -eq $call) {return}
                    $serial=Number $call 'call_serial';if ($serial -eq 0 -or $callIds.ContainsKey([string]$serial)) {throw 'probe重复调用ID'};$callIds[[string]$serial]=$true
                    foreach ($field in @('iteration','resource_id','related_resource_id','map_type','map_flags','subresource','offset_bytes','size_bytes','vertex_count','instance_count')) {[void](Number $call $field)}
                    $phase=[string]$call.phase;if (-not $phaseSums.ContainsKey($phase)) {throw 'probe未知phase'}
                    if ([string]$call.operation -ceq 'draw') {Count-Key $rawDrawCounts ($phase+'/'+(Number $call 'iteration'))}
                    foreach ($field in @('resource_id','related_resource_id')) {if ((Number $call $field) -gt 0 -and -not $resourceTable.ContainsKey([string]$call[$field])) {throw ('probe调用引用缺失资源: '+$field)}}
                    $available=Flag $call 'hresult_available';if ($available) {[void](Signed $call 'hresult')} elseif ([string]$call.hresult -ne '') {throw 'probe void API伪造HRESULT'}
                    if (-not (Flag $call 'valid_interval')) {return}
                    $a=Number $call 'start_qpc';$b=Number $call 'end_qpc';if ($frequency -le 0 -or $a -eq 0 -or $b -lt $a) {throw 'probe非法时钟'}
                    $duration=[double]($b-$a)*1000.0/$frequency;if ($phase -eq 'measure' -and ($null -eq $caseSummary['max_measure_call_ms'] -or $duration -gt $caseSummary['max_measure_call_ms'])) {$caseSummary['max_measure_call_ms']=$duration;$caseSummary['max_measure_operation']=$call.operation;$caseSummary['max_measure_site']=$call.site;$caseSummary['max_measure_resource_id']=$call.resource_id;$resource=$resourceTable[[string]$call.resource_id];$caseSummary['max_measure_resource_role']=$null;if ($resource) {$caseSummary['max_measure_resource_role']=$resource.role}};$phaseSums[$phase]+=$duration;$phaseCounts[$phase]++;$phaseIterations[$phase][[string]$call.iteration]=$true
                    $operation=[string]$call.operation;$key=$phase+'/'+$operation
                    if (-not $caseSummary.operations.ContainsKey($key)) {$caseSummary.operations[$key]=@{phase=$phase;operation=$operation;calls=0L;failed=0L;bytes=0L;total_ms=0.0;values=(New-Object 'Collections.Generic.List[double]')}}
                    $g=$caseSummary.operations[$key];$g.calls++;$g.total_ms+=$duration;$g.bytes+=(Number $call 'size_bytes');$g.values.Add($duration);if ($available -and (Signed $call 'hresult') -lt 0) {$g.failed++}
                }
                $rawRows=Csv-Rows (Join-Path $caseDirectory 'calls.csv') @('call_serial','phase','iteration','operation','site','resource_id','related_resource_id','start_qpc','end_qpc','valid_interval','hresult_available','hresult','map_type','map_flags','subresource','offset_bytes','size_bytes','vertex_count','instance_count') $rawAction 4096
                if ($rawRows -ne $result['calls_retained'] -or $totals.validation_issues -gt $before) {throw 'probe raw调用行数/字段不完整'}
                if ($caseSummary.status -eq 'complete' -and ($phaseIterations.measure.Count -ne $list['measured_iterations'] -or $phaseIterations.cold.Count -ne $list['cold_iterations'] -or $phaseIterations.warmup.Count -ne $list['warmup_iterations'])) {throw 'probe raw实际迭代覆盖不足'}
                if ($caseSummary.status -eq 'complete') {
                    # 用raw实际Draw逐轮交叉核验，不把result/definition重复声明当工作量证据。
                    for ($iteration=0;$iteration -lt 6;$iteration++) {$phase='measure';if ($iteration -eq 0) {$phase='cold'};if ($iteration -eq 1) {$phase='warmup'};$key=$phase+'/'+$iteration;$drawCount=0L;if ($rawDrawCounts.ContainsKey($key)) {$drawCount=$rawDrawCounts[$key]};if ($drawCount -ne $case['draws_per_iteration'] -or $gpuDrawCounts[$key] -ne $drawCount) {throw ('probe实际Draw分母与definition/GPU不符: '+$key)}}
                }
                foreach ($phase in @('cold','warmup','measure','drain')) {if ($phaseCounts[$phase] -gt 0) {$caseSummary[$phase+'_api_sum_ms']=$phaseSums[$phase]}}
                foreach ($g in $caseSummary.operations.Values) {$g['distribution']=Distribution $g.values;$g.Remove('values')}
                if ($caseSummary.status -eq 'complete' -and $null -ne $caseSummary.measure_api_sum_ms -and $phaseCounts.measure -gt 0) {
                    $profile=[string]$case['profile_id'];if (-not $profiles.ContainsKey($profile)) {$profiles[$profile]=@{}};$profiles[$profile][[string]$case['requested_backend']]=@{row=$caseSummary;definition=$case;result=$result}
                }
            } catch {if ($entry['status'] -ne 'timeout' -and $entry['status'] -ne 'termination_unconfirmed') {$caseSummary.status='invalid_result'};$caseSummary.identity_verified=$false;$caseSummary.reason=$_.Exception.Message;$ms.status='partial';Add-Issue ($id+': '+$caseSummary.reason)}
        }
        foreach ($id in $definitions.Keys) {if (-not $seen.ContainsKey($id)) {$ms.status='partial';Add-Issue ('matrix缺少父case状态: '+$id)}}
        foreach ($profile in $profiles.Keys) {
            $pair=$profiles[$profile];if (-not $pair.ContainsKey('hardware') -or -not $pair.ContainsKey('warp')) {continue};$hardware=$pair.hardware;$warp=$pair.warp;$comparable=$true
            foreach ($field in @('resource_bytes','write_bytes','draws_per_iteration','width','height','update_method','constant_write_method')) {if ([string]$hardware.definition[$field] -cne [string]$warp.definition[$field]) {$comparable=$false}}
            if ($hardware.definition['draws_per_iteration'] -gt 0) {foreach ($field in @('vertex_fnv1a64','pixel_fnv1a64')) {if ([string]$hardware.result['shader'][$field] -cne [string]$warp.result['shader'][$field]) {$comparable=$false}}}
            if ($comparable -and $warp.row.measure_api_sum_ms -gt 0) {
                $ratio=[double]$hardware.row.measure_api_sum_ms/$warp.row.measure_api_sum_ms;$hardware.row.hardware_to_warp_measure_ratio=$ratio;$warp.row.hardware_to_warp_measure_ratio=$ratio
                [void]$ms.pairs.Add(@{profile_id=$profile;hardware_measure_api_sum_ms=$hardware.row.measure_api_sum_ms;warp_measure_api_sum_ms=$warp.row.measure_api_sum_ms;hardware_to_warp_ratio=$ratio;comparison_scope='four_measured_iterations_CPU_leaf_API_intervals_not_GPU_busy';reason='同profile大小/写入量/迭代与shader身份匹配；冷轮/warmup另列'})
            }
        }
    } catch {$ms.status='partial';$ms.reason=$_.Exception.Message;Add-Issue $ms.reason}
    finally {Save-AnalysisCsv (Join-Path $Root 'resource-probe-comparison.csv') @('case_id','profile_id','requested_backend','status','reason','identity_verified','resource_bytes','write_bytes','draws_per_iteration','update_method','constant_write_method','cold_api_sum_ms','warmup_api_sum_ms','measure_api_sum_ms','drain_api_sum_ms','thread_cpu_ms','process_cpu_ms','max_measure_call_ms','max_measure_operation','max_measure_site','max_measure_resource_id','max_measure_resource_role','gpu_status','gpu_ready_draw_samples','gpu_ready_samples','gpu_nonready_samples','gpu_evidence_reason','hardware_to_warp_measure_ratio') $csvRows}
    return $ms
}

function Append-ResourceMatrixReport($Builder,$Matrix) {
    [void]$Builder.AppendLine('')
    [void]$Builder.AppendLine('独立矩阵可比较数值（CPU leaf API 区间合计，冷轮/warmup 与 drain 分开）：')
    [void]$Builder.AppendLine('')
    [void]$Builder.AppendLine('| 固定 profile | HW measure ms | WARP measure ms | HW/WARP |')
    [void]$Builder.AppendLine('|---|---:|---:|---:|')
    foreach ($pair in $Matrix.pairs) {[void]$Builder.AppendLine('| '+$pair.profile_id+' | '+(Display-Number $pair.hardware_measure_api_sum_ms)+' | '+(Display-Number $pair.warp_measure_api_sum_ms)+' | '+(Display-Number $pair.hardware_to_warp_ratio)+' |')}
    if ($Matrix.pairs.Count -eq 0) {[void]$Builder.AppendLine('| 没有满足身份/工作量/完整计量的成对结果 | 不可用 | 不可用 | 不可用 |')}
    [void]$Builder.AppendLine('')
    [void]$Builder.AppendLine('| Case | 状态 | 冷轮 ms | warmup ms | measure ms | 最长 measure 单调用 ms | 操作/位置/资源角色 | drain ms | GPU ready Draw/全部 ready |')
    [void]$Builder.AppendLine('|---|---|---:|---:|---:|---:|---|---:|---|')
    foreach ($case in $Matrix.cases) {
        $gpu='不可用';if ($case.gpu_coverage) {$gpu=$case.gpu_coverage.ready_draw_samples.ToString()+'/'+$case.gpu_coverage.ready_samples}
        $operation=[string]$case.max_measure_operation;if ($case.max_measure_site) {$operation+=' @ '+$case.max_measure_site};if ($case.max_measure_resource_role) {$operation+=' / '+$case.max_measure_resource_role}
        [void]$Builder.AppendLine('| '+$case.case_id+' | '+$case.status+' | '+(Display-Number $case.cold_api_sum_ms)+' | '+(Display-Number $case.warmup_api_sum_ms)+' | '+(Display-Number $case.measure_api_sum_ms)+' | '+(Display-Number $case.max_measure_call_ms)+' | '+$operation+' | '+(Display-Number $case.drain_api_sum_ms)+' | '+$gpu+' |')
    }
    [void]$Builder.AppendLine('')
    [void]$Builder.AppendLine('矩阵 GPU 只把实际 Draw、ready、有效频率/单调端点、完整 marker 的行列为 Draw 证据。CPU-only 的 not_started 合法且没有 GPU 耗时；clear/copy/readback 的 ready 窗口没有 Draw，也不能作为 shader 证据。query_failed/pending/disjoint/缺失保持不可用，逐 case 状态分母及区间分布在 summary.json/resource_matrix/cases/*/gpu_coverage。矩阵专有 Flush/有界 drain 已单列，它们不代表生产路径或纯 shader 忙时。')
    foreach ($case in $Matrix.cases) {
        if ($case.status -ne 'complete') {[void]$Builder.AppendLine('- '+$case.case_id+'：'+$case.status+'；'+$case.reason)}
        elseif ($case.gpu_coverage -and $case.gpu_coverage.ready_draw_samples -eq 0) {[void]$Builder.AppendLine('- '+$case.case_id+' GPU：'+$case.gpu_coverage.evidence_reason)}
    }
    foreach ($issue in $Matrix.issues) {[void]$Builder.AppendLine('- 矩阵原始协议校验：'+$issue)}
}

$launch=$null; $completion=$null; $collector=$null; $captureIdentityVerified=$false
try {
    # 离线结果允许移动/改名；按记录中的原始目录及同会话身份核对，不拿分析机路径替换现场身份。
    $launch=Read-PerfJson (Join-Path $CaptureRoot 'launch.json'); Test-PerfIdentity $launch $null $CaptureRoot $true
    if ($launch['status'] -ne 'started') { throw 'launch status 不符' }
    $completion=Read-PerfJson (Join-Path $CaptureRoot 'completion.json'); Test-PerfIdentity $completion $launch $CaptureRoot $true
    [void](Get-PerfJsonInteger $completion 'exit_code' -2147483648 2147483647)
    if ([IO.File]::Exists((Join-Path $CaptureRoot 'collector-session.json'))) {
        $identityRecord=Read-PerfJson (Join-Path $CaptureRoot 'collector-session.json')
        if ((Get-PerfJsonString $identityRecord 'session_id') -cne $launch['session_id'] -or (Get-PerfJsonString $identityRecord 'exe_path') -cne $launch['exe_path']) { throw 'collector / 最终会话身份不符' }
    }
    $captureIdentityVerified=$true
    if ($completion['status'] -ne 'completed' -or $completion['export_succeeded'] -isnot [bool] -or $completion['capture_complete'] -isnot [bool] -or $completion['export_succeeded'] -ne $true -or $completion['capture_complete'] -ne $true) { Add-Issue ('软件未完整完成采集: ' + $completion['status'] + ' / ' + $completion['reason']) }
    if ([IO.File]::Exists((Join-Path $CaptureRoot 'collector-status.json'))) {
        $collector=Read-PerfJson (Join-Path $CaptureRoot 'collector-status.json')
        if ($collector['status'] -ne 'completed') { Add-Issue ('采集器未完成: ' + $collector['reason']) }
    }
} catch { Add-Issue ('启动/完成握手不可验证: ' + $_.Exception.Message) }

$hostDirectories=@(Get-ChildItem -LiteralPath $CaptureRoot -ErrorAction SilentlyContinue | Where-Object { $_.PSIsContainer -and $_.Name -match '^host-[0-9]{4,}$' } | Sort-Object Name)
if ($hostDirectories.Length -gt $hostBudget) { Add-Issue 'Host 数超过 16，后续未分析'; $totals.analysis_truncated++; $hostDirectories=@($hostDirectories | Select-Object -First $hostBudget) }
foreach ($directory in $hostDirectories) {
    $hostName=$directory.Name; $hostSummary=@{host=$hostName;session=$null;initialization=$null;frames_valid=0;frames_invalid=0;inputs_valid=0;inputs_invalid=0;coverage_ms=$null}; $session=$null
    try {
        $session=Read-PerfJson (Join-Path $directory.FullName 'session.json')
        $hostSummary.session=$session; $hostAvailable=$true
        if ($session.ContainsKey('unavailable') -and $session['unavailable'] -isnot [bool]) { throw 'session unavailable 类型非法' }
        if ($session['unavailable'] -eq $true -or $session['status'] -eq 'unavailable') { $hostAvailable=$false; Add-Issue ($hostName + ': Host 性能采集明确 unavailable，不是有效空数据') }
        $hostSchema=Get-PerfJsonInteger $session 'schema_version' 1 2; if ($hostSchema -gt $captureSchema) {$captureSchema=$hostSchema}; [void](Get-PerfJsonInteger $session 'qpc_frequency' 1)
        $frequency=[double]$session['qpc_frequency']; $hostSummary.session=$session
        if ($session['exported'] -isnot [bool] -or $session['exported'] -ne $true) { Add-Issue ($hostName + ': 导出未完成') }
        if (@('complete','startup_failed','run_failed','unavailable') -notcontains [string]$session['status']) { throw 'session status 不支持' }
        foreach ($kind in @('frames','inputs','receipts')) {
            $seen=Get-PerfJsonInteger $session ($kind+'_seen'); $retained=Get-PerfJsonInteger $session ($kind+'_retained'); $dropped=Get-PerfJsonInteger $session ($kind+'_dropped')
            if ($seen -ne ($retained+$dropped)) { Add-Issue ($hostName + ': ' + $kind + ' 分母 seen != retained+dropped') }
            $totals[$kind+'_seen']+=$seen; $totals[$kind+'_retained']+=$retained; $totals[$kind+'_dropped']+=$dropped
        }
        $totals.proof_overflow+=(Get-PerfJsonInteger $session 'proof_overflow'); $totals.invalid_intervals+=(Get-PerfJsonInteger $session 'invalid_intervals')
    } catch { Add-Issue ($hostName + ': ' + $_.Exception.Message); [void]$hosts.Add($hostSummary); continue }
    try {
        $init=Read-PerfJson (Join-Path $directory.FullName 'initialization.json'); [void](Get-PerfJsonInteger $init 'schema_version' 1 1); $hostSummary.initialization=$init
        foreach ($event in $init['events']) {
            try {
                $a=Number $event 'start_qpc'; $b=Number $event 'end_qpc'; [void](Signed $event 'hresult')
                if (-not [string]$event['stage'] -or $event['succeeded'] -isnot [bool] -or ($a -gt 0 -and $b -gt 0 -and $b -lt $a)) { throw '初始化事件字段/时间非法' }
            } catch { Add-Issue ($hostName + ' initialization: ' + $_.Exception.Message) }
        }
    } catch { Add-Issue ($hostName + ': ' + $_.Exception.Message) }
    if ($hostSchema -eq 2) {
        if ($graphics.coverage -eq 'not_collected_by_v1') {$graphics.coverage='collected_v2'}
        try {
            $raw=@{'session.json'=2;'initialization.json'=1;'frames.csv'=2;'input-events.csv'=1;'gpu.csv'=2;'resources.csv'=1;'graphics-calls.csv'=1;'slow-graphics-calls.csv'=1;'graphics-groups.csv'=1}
            foreach ($name in $raw.Keys) { [void](Get-PerfJsonInteger $session['raw_schemas'] $name $raw[$name] $raw[$name]) }
        } catch { Add-Issue ($hostName+' raw_schemas: '+$_.Exception.Message) }
    }
    $state=@{previous=$null;previous_present=$null;first=$null;last=$null}
    $frameAction={param($row)
        if ($null -eq $row) { $totals.frames_invalid++; $hostSummary.frames_invalid++; $state.previous=$null; $state.previous_present=$null; return }
        $serial=Number $row 'frame_serial'; $start=Number $row 'start_qpc'; $end=Number $row 'end_qpc'; $chain=Number $row 'active_chain'
        foreach ($field in @('physical_before','physical_after','width','height','reason_flags','command_count','down_count','consumed_count','terminal_count','tool_mask','present_mode','content_revision','upload_bytes','map_failures','draw_calls','map_calls','discard_maps','no_overwrite_maps')) { [void](Number $row $field) }
        foreach ($field in @('dirty_left','dirty_top','dirty_right','dirty_bottom')) { [void](Signed $row $field) }
        [void](Flag $row 'full_present')
        $threadCpu=$null
        if ([string]$row.thread_cpu_ms -ne '') { $threadCpu=0.0; if (-not [double]::TryParse([string]$row.thread_cpu_ms,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$threadCpu) -or [double]::IsNaN($threadCpu) -or [double]::IsInfinity($threadCpu) -or $threadCpu -lt 0) { throw 'thread_cpu_ms 非法' } }
        if ($serial -eq 0 -or $start -eq 0 -or $end -lt $start) { throw '帧序号/时间非法' }
        $key=$hostName+':'+$serial; if ($frameIndex.ContainsKey($key)) { throw '重复帧身份' }
        if ($frameIndex.Count -ge $frameBudget) { $totals.analysis_truncated++; return }
        $api=Flag $row 'present_api_attempted'; $attempt=Flag $row 'present_attempted'; $success=Flag $row 'present_succeeded'; $hr=Signed $row 'present_hresult'; $present=Number $row 'present_return_qpc'
        $idle=Flag $row 'idle_boundary'; $target=Number $row 'output_target'; $revision=Number $row 'output_revision'; $epoch=Number $row 'device_epoch'
        if ($target -gt 1 -or (Number $row 'present_mode') -gt 1) { throw 'present mode/target 不支持' }
        if ($success -and (-not $api -or -not $attempt -or $hr -ne 0 -or $present -lt $start -or $present -gt $end)) { throw '成功 Present 缺少有效 API/S_OK/时间证据' }
        $observed=Number $row 'stage_observed_mask'; $invalid=Number $row 'stage_invalid_mask'
        $stages=@{}; for ($bit=0; $bit -lt $stageNames.Length; $bit++) {
            $name=$stageNames[$bit]; $ticks=Number $row ($name+'_qpc'); $flagBit=[long][Math]::Pow(2,$bit)
            $stages[$name]=$null
            if (($observed -band $flagBit) -ne 0 -and ($invalid -band $flagBit) -eq 0) { $stages[$name]=[double]$ticks*1000.0/$frequency }
        }
        $priorStages=@{}; foreach ($name in $priorNames) {$priorStages[$name]=$null}
        if ($row.ContainsKey('prior_stage_observed_mask')) {
            $priorObserved=Number $row 'prior_stage_observed_mask'; $priorInvalid=Number $row 'prior_stage_invalid_mask'
            foreach ($name in $priorNames) {
                $ticks=Number $row ('prior_'+$name+'_qpc'); $bit=[Array]::IndexOf($stageNames,$name); $flagBit=[long][Math]::Pow(2,$bit)
                if (($priorObserved -band $flagBit) -ne 0 -and ($priorInvalid -band $flagBit) -eq 0) { $priorStages[$name]=[double]$ticks*1000.0/$frequency }
            }
            foreach ($name in @('upload_bytes','draw_calls','map_calls','down_count','consumed_count','tool_mask')) { [void](Number $row ('prior_'+$name)) }
            if ($priorInvalid -gt 0) { Add-Issue ($hostName + ': 前置阶段存在非法时间区间') }
        }
        $work=[double]($end-$start)*1000.0/$frequency; $interval=$null; $presentInterval=$null
        if ($state.previous) {
            if ($serial -le $state.previous.serial -or $start -lt $state.previous.start) { throw '帧序号/起点逆序' }
            if ($chain -gt 0 -and $chain -eq $state.previous.chain -and -not $state.previous.idle) { $interval=[double]($start-$state.previous.start)*1000.0/$frequency }
        }
        if ($success -and $state.previous_present -and $chain -gt 0 -and $chain -eq $state.previous_present.chain -and $target -eq $state.previous_present.target -and $revision -eq $state.previous_present.revision -and $epoch -eq $state.previous_present.epoch) {
            if ($present -lt $state.previous_present.qpc) { throw '成功呈现间隔逆序' }; $presentInterval=[double]($present-$state.previous_present.qpc)*1000.0/$frequency
        }
        $drawCalls=Number $row 'draw_calls'; $mapCalls=Number $row 'map_calls'
        $facts=@{host=$hostName;serial=$serial;start=$start;end=$end;present=$present;success=$success;api=$api;target=$target;revision=$revision;epoch=$epoch;chain=$chain;idle=$idle;draw_calls=$drawCalls;map_calls=$mapCalls;work_ms=$work;thread_cpu_ms=$threadCpu;consumed_count=(Number $row 'consumed_count');tool_mask=(Number $row 'tool_mask');call_intervals=(New-Object Collections.ArrayList);top_calls=(New-Object Collections.ArrayList)}
        if ($hostSchema -eq 2) {
            $frameSummaryValid=$true
            foreach ($prefix in @('graphics_','prior_graphics_')) {
                foreach ($part in @('leaf_qpc','call_count','invalid_count','observed_mask','invalid_mask')) { [void](Number $row ($prefix+$part) ($part -eq 'leaf_qpc')) }
                foreach ($operation in $graphicsOperations) { [void](Number $row ($prefix+$operation+'_qpc')); [void](Number $row ($prefix+$operation+'_count')) }
                $ticksSum=[decimal]0;$countSum=[decimal]0;$observed=Number $row ($prefix+'observed_mask');$invalid=Number $row ($prefix+'invalid_mask');$countsMask=0L
                for ($operationIndex=0;$operationIndex -lt $graphicsOperations.Length;$operationIndex++) {$operation=$graphicsOperations[$operationIndex];$ticksSum+=(Number $row ($prefix+$operation+'_qpc'));$operationCount=Number $row ($prefix+$operation+'_count');$countSum+=$operationCount;if ($operationCount -gt 0) {$countsMask=$countsMask -bor [long][Math]::Pow(2,$operationIndex)}}
                $leafValue=Number $row ($prefix+'leaf_qpc') $true
                if ($observed -gt 1023 -or $invalid -gt 1023 -or ($invalid -band $observed) -ne $invalid -or $countsMask -ne $observed -or $countSum -ne (Number $row ($prefix+'call_count')) -or $null -eq $leafValue -or $ticksSum -ne $leafValue -or (Number $row ($prefix+'invalid_count')) -gt $countSum) {$frameSummaryValid=$false;Add-Issue ($hostName+' frame '+$serial+' '+$prefix+'摘要和/掩码不符，残差不可用')}
            }
            $leafTicks=Number $row 'graphics_leaf_qpc' $true;$facts.graphics_leaf_ms=$null;if ($frameSummaryValid -and $null -ne $leafTicks) {$facts.graphics_leaf_ms=[double]$leafTicks*1000.0/$frequency}
            $facts.graphics_summary_valid=$frameSummaryValid;$facts.graphics_call_count=Number $row 'graphics_call_count'; $facts.graphics_invalid_count=Number $row 'graphics_invalid_count'; $facts.graphics_invalid_mask=Number $row 'graphics_invalid_mask'
            $facts.graphics_residual_ms=$null
            if ($null -ne $facts.graphics_leaf_ms -and $facts.graphics_leaf_ms -gt $work+0.000001) { Add-Issue ($hostName+' frame '+$serial+': leaf 计量超过帧工作区间，不能解释为精确覆盖') }
            elseif ($null -ne $facts.graphics_leaf_ms -and $facts.graphics_invalid_count -eq 0 -and $facts.graphics_invalid_mask -eq 0) { $facts.graphics_residual_ms=$work-$facts.graphics_leaf_ms }
            if ($null -ne $facts.graphics_leaf_ms) {Add-Metric 'graphics_frame_leaf' $facts.graphics_leaf_ms}
            if ($null -ne $facts.graphics_residual_ms) { Add-Metric 'graphics_frame_unattributed' $facts.graphics_residual_ms }
        }
        if ($drawCalls -gt 0) {
            $gpuCoverage.draw_frames++; Add-Metric 'render_frame_work' $work
            if ($target -eq 0) {$gpuCoverage.primary_draw_frames++;Add-Metric 'primary_render_frame_work' $work;if ($null -ne $threadCpu) {Add-Metric 'primary_render_owner_thread_cpu' $threadCpu};foreach ($stageName in $stageNames) {if ($null -ne $stages[$stageName]) {Add-Metric ('primary_render_cpu_'+$stageName) $stages[$stageName]}}} else {$gpuCoverage.selection_draw_frames++;Add-Metric 'selection_render_frame_work' $work}
            if (($facts.tool_mask -band 487) -ne 0) {$gpuCoverage.ink_draw_frames++;Add-Metric 'ink_render_frame_work' $work}
            if ($null -ne $threadCpu) { Add-Metric 'render_owner_thread_cpu' $threadCpu }
            if ($null -ne $interval) { Add-Metric 'render_active_interval' $interval }
            if ($null -ne $presentInterval) { Add-Metric 'render_successful_present_interval' $presentInterval }
            foreach ($name in $stageNames) { if ($null -ne $stages[$name]) { Add-Metric ('render_cpu_'+$name) $stages[$name] } }
            $groupKey='target='+$target+'/tool_mask='+$facts.tool_mask+'/epoch='+$epoch
            if (-not $renderGroups.ContainsKey($groupKey) -and $renderGroups.Count -lt 128) { $renderGroups[$groupKey]=@{output_target=$target;tool_mask=$facts.tool_mask;device_epoch=$epoch;frames=0L;work=(New-Object 'Collections.Generic.List[double]');owner_cpu=(New-Object 'Collections.Generic.List[double]');active=(New-Object 'Collections.Generic.List[double]');present=(New-Object 'Collections.Generic.List[double]')} }
            if ($renderGroups.ContainsKey($groupKey)) { $g=$renderGroups[$groupKey]; $g.frames++; $g.work.Add($work); if ($null -ne $threadCpu) {$g.owner_cpu.Add($threadCpu)}; if ($null -ne $interval) {$g.active.Add($interval)}; if ($null -ne $presentInterval) {$g.present.Add($presentInterval)} }
            else { $totals.analysis_truncated++ }
        }
        if ($api) { Add-Metric 'present_api_frame_work' $work }
        Add-Metric 'frame_consumed_batches' ([double]$facts.consumed_count)
        $frameIndex[$key]=$facts; $state.previous=$facts
        if ($success) { $state.previous_present=@{qpc=$present;chain=$chain;target=$target;revision=$revision;epoch=$epoch} }
        if ($idle) { $state.previous_present=$null; $totals.idle_boundaries++ }
        if ($null -eq $state.first) { $state.first=$start }; $state.last=$end
        Add-Metric 'frame_work' $work
        if ($null -ne $threadCpu) { Add-Metric 'owner_thread_cpu' $threadCpu }
        foreach ($name in $stageNames) { if ($null -ne $stages[$name]) { Add-Metric ('cpu_'+$name) $stages[$name] } }
        foreach ($name in $priorNames) { if ($null -ne $priorStages[$name]) { Add-Metric ('cpu_prior_'+$name) $priorStages[$name] } }
        if ($null -ne $interval) { Add-Metric 'active_interval' $interval; $totals.active_intervals++ }
        if ($null -ne $presentInterval) { Add-Metric 'successful_present_interval' $presentInterval }
        if ($attempt) { $totals.present_attempted++ }
        if ($api) { $totals.present_api_attempted++; if (-not $success) { $totals.present_failure++; if ($hr -ge 0) { $totals.present_non_s_ok_status++ } } }
        elseif ($attempt -and $hr -lt 0) { $totals.present_preparation_failure++ }
        elseif ($attempt) { $totals.present_noop++ }
        if ($success) { $totals.present_success++ }
        $totals.frames_valid++; $hostSummary.frames_valid++
        $entry=@{host=$hostName;frame_serial=$serial;work_ms=$work;active_interval_ms=$interval;successful_present_interval_ms=$presentInterval;geometry_ms=$stages.geometry;present_ms=$stages.present;control_ms=$stages.control;model_update_ms=$stages.model_update;model_predict_ms=$stages.model_predict;ulw_map_ms=$stages.ulw_map;ulw_rows_ms=$stages.ulw_rows;ulw_update_ms=$stages.ulw_update;present_succeeded=$success;present_hresult=$hr;present_mode=$row.present_mode;output_target=$target;reason_flags=$row.reason_flags;tool_mask=$row.tool_mask;physical_before=$row.physical_before;physical_after=$row.physical_after;consumed_count=$row.consumed_count;command_count=$row.command_count;width=$row.width;height=$row.height;upload_bytes=$row.upload_bytes;draw_calls=$drawCalls;map_calls=$mapCalls;upload_map_ms=$stages.upload_map;upload_copy_ms=$stages.upload_copy;draw_submit_ms=$stages.draw_submit;map_failures=$row.map_failures;device_epoch=$epoch}
        $facts.entry=$entry
        # 保留低工作成本但活动/呈现间隔异常的帧；峰值只是排序依据，不能当作可相加的总耗时。
        $entry.diagnostic_peak_ms=$work; $entry.prior_peak_ms=$null
        foreach ($name in $priorNames) { $entry['prior_'+$name+'_ms']=$priorStages[$name]; if ($null -ne $priorStages[$name]) { $entry.diagnostic_peak_ms=[Math]::Max($entry.diagnostic_peak_ms,$priorStages[$name]); if ($null -eq $entry.prior_peak_ms -or $priorStages[$name] -gt $entry.prior_peak_ms) {$entry.prior_peak_ms=$priorStages[$name]} } }
        foreach ($gap in @($interval,$presentInterval)) { if ($null -ne $gap) {$entry.diagnostic_peak_ms=[Math]::Max($entry.diagnostic_peak_ms,$gap)} }
        $entry.deadline_wait_ms=$stages.deadline_wait; $entry.idle_wait_ms=$stages.idle_wait
        foreach ($name in @('upload_bytes','draw_calls','map_calls','down_count','consumed_count','tool_mask')) {$entry['prior_'+$name]=$row['prior_'+$name]}
        if ($longFrames.Count -lt 100) { [void]$longFrames.Add($entry) }
        else { $minimum=0; for ($j=1;$j -lt $longFrames.Count;$j++) { if ($longFrames[$j].diagnostic_peak_ms -lt $longFrames[$minimum].diagnostic_peak_ms) { $minimum=$j } }; if ($entry.diagnostic_peak_ms -gt $longFrames[$minimum].diagnostic_peak_ms) { $longFrames[$minimum]=$entry } }
    }
    $frameRequired=@('frame_serial','start_qpc','end_qpc','present_return_qpc','active_chain','idle_boundary','physical_before','physical_after','width','height','reason_flags','command_count','down_count','consumed_count','terminal_count','tool_mask','dirty_left','dirty_top','dirty_right','dirty_bottom','full_present','thread_cpu_ms','draw_calls','map_calls','discard_maps','no_overwrite_maps','present_attempted','present_api_attempted','present_succeeded','present_hresult','present_mode','output_target','output_revision','content_revision','stage_observed_mask','stage_invalid_mask','upload_bytes','map_failures','device_epoch')
    foreach ($name in $stageNames) { $frameRequired+=($name+'_qpc') }
    if ($hostSchema -eq 2) { foreach ($prefix in @('graphics_','prior_graphics_')) { foreach ($part in @('leaf_qpc','call_count','invalid_count','observed_mask','invalid_mask')) {$frameRequired+=($prefix+$part)}; foreach ($name in $graphicsOperations) {$frameRequired+=($prefix+$name+'_qpc');$frameRequired+=($prefix+$name+'_count')} } }
    $frameRows=Csv-Rows (Join-Path $directory.FullName 'frames.csv') $frameRequired $frameAction ([Math]::Max(0,$frameBudget-[int]$totals.frames_csv_rows))
    $totals.frames_csv_rows+=$frameRows
    if ($frameRows -ne [long]$session['frames_retained']) { Add-Issue ($hostName + ': frames CSV 行数与 retained 不符') }
    if ($null -ne $state.first) { $hostSummary.coverage_ms=[double]($state.last-$state.first)*1000.0/$frequency; $hostSummary.first_frame_qpc=$state.first; $hostSummary.last_frame_qpc=$state.last }
    $seenTokens=@{}; $publishedKeys=@{}; $inputState=@{last=$null}
    $inputAction={param($row)
        if ($null -eq $row) { $totals.input_invalid++; $hostSummary.inputs_invalid++; return }
        $token=Number $row 'callback_token'; $phaseNumber=Number $row 'phase'; if ($token -eq 0 -or $phaseNumber -gt 3 -or $seenTokens.ContainsKey([string]$token)) { throw '输入 callback/phase 身份非法' }
        foreach ($field in @('tablet_context_id','contact_id','device_type','packet_count','callback_entry_qpc','source_qpc','published_qpc','callback_end_qpc','record_id','generation','sequence','admission_revision','consume_qpc','process_end_qpc','adopt_qpc','raster_qpc','stored_qpc','present_return_qpc','frame_serial','workspace_id','page_id','scene_generation','raster_generation','output_generation','output_target','output_revision','content_token','item_token','proof_kind')) { [void](Number $row $field) }
        if (@('published','publish_rejected','state_busy','decoder_missing','decode_failed','touchpad_filtered','no_binding','cancelled','invalid_argument','unknown') -notcontains [string]$row.callback_outcome -or -not [string]$row.outcome) { throw 'callback/outcome 枚举缺失或非法' }
        $seenTokens[[string]$token]=$true
        $phaseName=@('Down','Move','Up','Cancelled')[[int]$phaseNumber]
        $tool='unknown'; if ($row.ContainsKey('effective_tool')) { $toolCode=Number $row 'effective_tool'; if ($toolCode -lt $toolNames.Length) { $tool=$toolNames[[int]$toolCode] } elseif ($toolCode -ne 4294967295) { throw 'effective_tool 非法枚举' } }
        $toolKey=$tool+':'+$phaseName
        if (-not $toolPhases.ContainsKey($toolKey)) { $toolPhases[$toolKey]=@{tool=$tool;phase=$phaseName;callbacks=0L;consumed=0L;confirmed=0L;unsupported=0L;unconfirmed=0L} }
        $toolPhase=$toolPhases[$toolKey]; $toolPhase.callbacks++
        if (-not $phases.ContainsKey($phaseName)) { $phases[$phaseName]=@{callbacks=0L;packets=0L;published=0L;consumed=0L;confirmed=0L;unconfirmed=0L;unsupported=0L;invalid_time=0L;outcomes=@{}} }
        $phase=$phases[$phaseName]; $phase.callbacks++; $phase.packets+=(Number $row 'packet_count'); Count-Key $phase.outcomes ([string]$row.outcome)
        $entry=Number $row 'callback_entry_qpc'; if ($entry -eq 0) { throw '缺少 callback 起点' }
        if ($null -ne $inputState.last -and $entry -ge $inputState.last) { Add-Metric 'input_callback_gap' ([double]($entry-$inputState.last)*1000.0/$frequency) }; $inputState.last=$entry
        $prefix='input_'+$phaseName+'_'
        $published=Flag $row 'publication_succeeded'; $supported=Flag $row 'proof_supported'; $validProof=Flag $row 'proof_valid'
        $consume=Number $row 'consume_qpc'; $raster=Number $row 'raster_qpc'; $present=Number $row 'present_return_qpc'; $frameSerial=Number $row 'frame_serial'
        $record=Number $row 'record_id'; $generation=Number $row 'generation'; $sequence=Number $row 'sequence'; $source=Number $row 'source_qpc'; [void](Number $row 'admission_revision')
        $timeValid=$true
        try {
            [void](Add-Delta $row 'callback_entry_qpc' 'callback_end_qpc' ($prefix+'callback_total') $frequency)
            if ($published) {
                if ($record -eq 0 -or $generation -eq 0 -or $sequence -eq 0 -or ($sequence % 2) -ne 0 -or $source -eq 0 -or (Number $row 'published_qpc') -eq 0) { throw '发布输入身份不完整/奇数 sequence' }
                $publicationKey=[string]$record+':'+$generation+':'+$sequence+':'+$source+':'+$row.admission_revision
                if ($publishedKeys.ContainsKey($publicationKey)) { throw '重复 publication 身份' }; $publishedKeys[$publicationKey]=$true
                [void](Add-Delta $row 'callback_entry_qpc' 'published_qpc' ($prefix+'callback_publish') $frequency); $phase.published++
                [void](Add-Delta $row 'source_qpc' 'published_qpc' ($prefix+'source_publish') $frequency)
                if ($consume -gt 0) {
                    [void](Add-Delta $row 'published_qpc' 'consume_qpc' ($prefix+'publish_consume') $frequency)
                    [void](Add-Delta $row 'callback_entry_qpc' 'consume_qpc' ($prefix+'callback_consume') $frequency); $phase.consumed++
                    [void](Add-Delta $row 'source_qpc' 'consume_qpc' ($prefix+'source_consume') $frequency)
                    [void](Add-Delta $row 'consume_qpc' 'process_end_qpc' ($prefix+'consume_process_inclusive') $frequency)
                    [void](Add-Delta $row 'consume_qpc' 'adopt_qpc' ($prefix+'consume_adopt_inclusive') $frequency)
                }
            } elseif ($consume -gt 0 -or $validProof) { throw '未发布输入却带消费/成功证据' }
        } catch { $timeValid=$false; $phase.invalid_time++; Add-Issue ($hostName + ' input ' + $token + ': ' + $_.Exception.Message) }
        if (-not $supported) { $phase.unsupported++ }
        $landing=$false
        if ($validProof -and $timeValid) {
            try {
                if (-not $captureIdentityVerified -or -not $hostAvailable) { throw '最终进程/完成身份或 Host 可用性未验证，保留原始 proof 声明但不确认落地' }
                $proofKind=Number $row 'proof_kind'; $content=Number $row 'content_token'; $item=Number $row 'item_token'
                foreach ($field in @('workspace_id','page_id','scene_generation','raster_generation','output_generation')) { if ((Number $row $field) -eq 0) { throw "缺少 proof 身份: $field" } }
                foreach ($field in @('output_target','output_revision')) { [void](Number $row $field) }
                if (-not $supported -or -not $published -or $consume -eq 0 -or $phaseNumber -gt 2 -or $content -eq 0 -or @([long]1,[long]2) -notcontains $proofKind -or ($proofKind -eq 2 -and $item -eq 0) -or $row.outcome -ne 'confirmed_software_present') { throw '成功声明缺少真实内容 proof' }
                $facts=$frameIndex[$hostName+':'+$frameSerial]
                if (-not $facts -or -not $facts.success -or -not $facts.api -or $present -ne $facts.present -or $raster -gt $facts.end -or ($proofKind -eq 1 -and $raster -lt $facts.start) -or (Number $row 'output_target') -ne $facts.target -or (Number $row 'output_revision') -ne $facts.revision) { throw '过期/无贡献/失败/noop 或 frame/output 关联不符' }
                if ($present -eq 0 -or ($raster -gt 0 -and ($raster -lt $consume -or $present -lt $raster))) { throw 'landing 端点未知/逆序' }
                if ($proofKind -eq 1 -and $raster -eq 0) { throw 'Live 缺少 raster 端点' }
                if ($raster -gt 0) { $adopt=Number $row 'adopt_qpc'; if ($adopt -eq 0 -or $adopt -lt $consume -or $raster -lt $adopt) { throw 'raster 缺少同输入实际 Adopt 端点/顺序' } }
                if ($proofKind -eq 2) {
                    $stored=Number $row 'stored_qpc'
                    if ($stored -eq 0 -or $stored -lt $consume -or $stored -gt $present) { throw 'Stored 缺少实际 commit 端点/时间逆序' }
                    [void](Add-Delta $row 'consume_qpc' 'stored_qpc' ($prefix+'consume_store') $frequency)
                    [void](Add-Delta $row 'stored_qpc' 'present_return_qpc' ($prefix+'store_present') $frequency)
                }
                if ($raster -gt 0) {
                    [void](Add-Delta $row 'consume_qpc' 'raster_qpc' ($prefix+'consume_raster') $frequency)
                    [void](Add-Delta $row 'raster_qpc' 'present_return_qpc' ($prefix+'raster_present') $frequency)
                }
                [void](Add-Delta $row 'callback_entry_qpc' 'present_return_qpc' ($prefix+'callback_present') $frequency)
                [void](Add-Delta $row 'source_qpc' 'present_return_qpc' ($prefix+'source_present') $frequency)
                $landing=$true; $phase.confirmed++
            } catch { $phase.invalid_time++; Add-Issue ($hostName + ' input proof ' + $token + ': ' + $_.Exception.Message) }
        }
        if (-not $landing) { $phase.unconfirmed++ }
        $toolPrefix='tool_'+$tool+'_'+$phaseName+'_'
        if ($timeValid -and $published -and $consume -gt 0) { $toolPhase.consumed++; Add-Metric ($toolPrefix+'callback_consume') ([double]($consume-$entry)*1000.0/$frequency) }
        if ($landing) { $toolPhase.confirmed++; Add-Metric ($toolPrefix+'callback_present') ([double]($present-$entry)*1000.0/$frequency) } else {$toolPhase.unconfirmed++}
        if (-not $supported) { $toolPhase.unsupported++ }
        if (-not $timeValid -or ($validProof -and -not $landing)) { $totals.input_invalid++; $hostSummary.inputs_invalid++ }
        else { $totals.input_valid++; $hostSummary.inputs_valid++ }
    }
    $inputRequired=@('callback_token','phase','tablet_context_id','contact_id','device_type','packet_count','callback_entry_qpc','source_qpc','published_qpc','callback_end_qpc','publication_succeeded','callback_outcome','record_id','generation','sequence','admission_revision','consume_qpc','process_end_qpc','adopt_qpc','raster_qpc','stored_qpc','present_return_qpc','frame_serial','workspace_id','page_id','scene_generation','raster_generation','output_generation','output_target','output_revision','content_token','item_token','proof_kind','proof_supported','proof_valid','outcome')
    $inputRows=Csv-Rows (Join-Path $directory.FullName 'input-events.csv') $inputRequired $inputAction ([Math]::Max(0,$rowBudget-[int]$totals.input_csv_rows))
    $totals.input_csv_rows+=$inputRows
    if ($inputRows -ne [long]$session['inputs_retained']) { Add-Issue ($hostName + ': input CSV 行数与 retained 不符') }
    $gpuAction={param($row)
        if ($null -eq $row) { $totals.gpu_invalid++; return }
        foreach ($field in @('frame_serial','device_epoch','frequency','geometry_begin','geometry_end','composite_begin','composite_end','readback_begin','readback_end')) { [void](Number $row $field) }; [void](Signed $row 'hresult')
        $status=[string]$row.status; Count-Key $gpuStatuses $status
        if (@('ready','pending','disjoint','query_failed','invalid','owner_stopped','device_replaced') -notcontains $status) { throw 'GPU status 不支持' }
        if ($hostSchema -eq 2) {
            foreach ($name in @('sample_id','loop_serial','candidate_index','marker_observed_mask','draw_calls_in_sample','geometry_draw_calls','composite_draw_calls','readback_calls','work_mask','cpu_begin_qpc','cpu_end_qpc','marker_clipped_mask','domain_candidate_index')) { [void](Number $row $name) }; [void](Flag $row 'frame_retained')
            if (@('first_work','periodic_work') -cnotcontains [string]$row.sampling_reason) { throw 'GPU sampling_reason 不支持' }
            if (@('frame','prior') -cnotcontains [string]$row.sampling_domain) {throw 'GPU sampling_domain 不支持'}
            if ($row.sampling_domain -eq 'prior') {
                if ((Number $row 'frame_serial') -ne 0 -or (Number $row 'loop_serial') -ne 0) {throw 'GPU prior伪绑定frame/loop'}
                $gpuCoverage.prior_samples++
                if ($status -eq 'ready') {
                    $priorFrequency=Number $row 'frequency';if ($priorFrequency -le 0) {throw 'prior GPU frequency无效'}
                    $priorDraw=$false
                    foreach ($stage in @('geometry','composite','readback')) {
                        $countName=$stage+'_draw_calls';if ($stage -eq 'readback') {$countName='readback_calls'};$pair=3;if ($stage -eq 'composite') {$pair=12};if ($stage -eq 'readback') {$pair=48}
                        if ((Number $row $countName) -gt 0 -and ((Number $row 'marker_observed_mask') -band $pair) -eq $pair) {if (Add-Delta $row ($stage+'_begin') ($stage+'_end') ('gpu_prior_'+$stage+'_command_interval') ([double]$priorFrequency)) {if ($stage -ne 'readback') {$priorDraw=$true}}}
                    }
                    if ($priorDraw) {$gpuCoverage.ready_prior_draw_segments++}
                }
                return
            }
        }
        if ($status -ne 'ready') { return }
        $freq=Number $row 'frequency'; $facts=$frameIndex[$hostName+':'+(Number $row 'frame_serial')]
        if ($freq -eq 0) { throw 'GPU frequency 无效' }
        if (-not $facts) {
            $hasMarker=$false; foreach ($name in @('geometry_begin','geometry_end','composite_begin','composite_end','readback_begin','readback_end')) { if ((Number $row $name) -gt 0) { $hasMarker=$true } }
            if (-not $hasMarker) { $totals.gpu_non_render_candidates++; $gpuCoverage.empty_work_samples++; return }
            $totals.gpu_unmatched++; $gpuCoverage.unmatched_samples++; Add-Issue ($hostName + ': GPU 有命令端点但没有匹配的保留帧'); return
        }
        if ((Number $row 'device_epoch') -ne $facts.epoch) { throw 'GPU frame/epoch 无效' }
        $actualDraws=$facts.draw_calls
        if ($hostSchema -eq 2) { $actualDraws=Number $row 'draw_calls_in_sample'; if (-not (Flag $row 'frame_retained')) { $gpuCoverage.unmatched_samples++; return }; if ((Number $row 'marker_clipped_mask') -gt 0) {$gpuCoverage.clipped_marker_samples++} }
        if ($actualDraws -eq 0 -or $facts.draw_calls -eq 0) { $gpuCoverage.empty_work_samples++ }
        $hasDrawMarker=$false
        foreach ($stage in @('geometry','composite','readback')) {
            $hasWork=$actualDraws -gt 0 -and $facts.draw_calls -gt 0
            if ($hostSchema -eq 2) {
                $countName=$stage+'_draw_calls'; if ($stage -eq 'readback') {$countName='readback_calls'}
                $hasWork=(Number $row $countName) -gt 0
                if ($stage -ne 'readback') {$hasWork=$hasWork -and $actualDraws -gt 0 -and $facts.draw_calls -gt 0}
                $pair=3; if ($stage -eq 'composite') {$pair=12}; if ($stage -eq 'readback') {$pair=48}
                if ($hasWork -and ((Number $row 'marker_observed_mask') -band $pair) -ne $pair) { $gpuCoverage.missing_marker_samples++; continue }
            }
            if (-not $hasWork) { $gpuCoverage.stage_without_work++; continue }
            if (Add-Delta $row ($stage+'_begin') ($stage+'_end') ('gpu_'+$stage+'_command_interval') ([double]$freq)) { $gpuCoverage[$stage+'_samples']++; if ($stage -ne 'readback') {$hasDrawMarker=$true} }
            else { $gpuCoverage.missing_marker_samples++ }
        }
        if ($hasDrawMarker) {
            $gpuCoverage.ready_draw_samples++
            if ($hostSchema -eq 1) {$gpuCoverage.legacy_inferred_samples++}
            $key=$hostName+':'+$facts.serial; if (-not $coveredGpuFrames.ContainsKey($key)) {$coveredGpuFrames[$key]=$true;$gpuCoverage.ready_draw_frames++}
            if ($null -eq $gpuCoverage.longest_covered_frame_work_ms -or $facts.work_ms -gt $gpuCoverage.longest_covered_frame_work_ms) {$gpuCoverage.longest_covered_frame_work_ms=$facts.work_ms}
        }
    }
    $gpuRequired=@('frame_serial','device_epoch','status','frequency','geometry_begin','geometry_end','composite_begin','composite_end','readback_begin','readback_end','hresult')
    if ($hostSchema -eq 2) {$gpuRequired+=@('sample_id','loop_serial','candidate_index','sampling_reason','marker_observed_mask','draw_calls_in_sample','geometry_draw_calls','composite_draw_calls','readback_calls','work_mask','cpu_begin_qpc','cpu_end_qpc','frame_retained','marker_clipped_mask','sampling_domain','domain_candidate_index')}
    $gpuRows=Csv-Rows (Join-Path $directory.FullName 'gpu.csv') $gpuRequired $gpuAction ([Math]::Max(0,$rowBudget-[int]$totals.gpu_rows))
    $totals.gpu_rows+=$gpuRows
    if ($hostSchema -eq 2) { Analyze-GraphicsHost $directory.FullName $hostName $session $frequency }
    [void]$hosts.Add($hostSummary)
}

if ($hosts.Count -eq 0) { Add-Issue '没有 Host 原始数据' }
foreach ($field in @('frames_dropped','inputs_dropped','receipts_dropped','proof_overflow','invalid_intervals','analysis_truncated','frames_invalid','input_invalid','gpu_invalid')) { if ($totals[$field] -gt 0) { Add-Issue ($field + '=' + $totals[$field] + '，结果为部分证据') } }
if ($completion -and $completion['export_status']) {
    foreach ($field in @('frames_seen','frames_retained','frames_dropped','inputs_seen','inputs_retained','inputs_dropped','receipts_seen','receipts_retained','receipts_dropped','proof_overflow')) {
        try { if ((Get-PerfJsonInteger $completion['export_status'] $field) -ne $totals[$field]) { Add-Issue ('completion 汇总不符: ' + $field) } } catch { Add-Issue $_.Exception.Message }
    }
    foreach ($field in @('graphics_calls_seen','graphics_calls_retained','graphics_calls_dropped','slow_graphics_calls_seen','slow_graphics_calls_retained','slow_graphics_calls_dropped','resources_seen','resources_retained','resources_dropped','graphics_group_calls_dropped','graphics_site_collisions')) {
        if ($completion['export_status'].ContainsKey($field)) {
            $mapped=$field.Replace('slow_graphics_calls','slow_calls').Replace('graphics_calls','calls').Replace('graphics_group_calls_dropped','group_calls_dropped').Replace('graphics_site_collisions','site_collisions')
            try {if ((Get-PerfJsonInteger $completion['export_status'] $field) -ne $graphics[$mapped]) {Add-Issue ('completion 图形明细汇总不符: '+$field)}} catch {Add-Issue $_.Exception.Message}
        }
    }
    try { if ((Get-PerfJsonInteger $completion['export_status'] 'host_runs') -ne $hosts.Count -or (Get-PerfJsonInteger $completion['export_status'] 'exported_host_runs') -ne $hosts.Count) { Add-Issue 'completion Host 导出计数不符' } } catch { Add-Issue $_.Exception.Message }
}
$distributions=@{}
foreach ($name in @('frame_work','render_frame_work','primary_render_frame_work','primary_render_owner_thread_cpu','selection_render_frame_work','ink_render_frame_work','present_api_frame_work','render_owner_thread_cpu','render_active_interval','render_successful_present_interval','graphics_frame_leaf','graphics_frame_unattributed','input_callback_gap','active_interval','successful_present_interval','owner_thread_cpu')) { $distributions[$name]=Distribution $metrics[$name] }
foreach ($name in $stageNames) { $distributions['cpu_'+$name]=Distribution $metrics['cpu_'+$name]; $distributions['render_cpu_'+$name]=Distribution $metrics['render_cpu_'+$name];$distributions['primary_render_cpu_'+$name]=Distribution $metrics['primary_render_cpu_'+$name] }
foreach ($name in $priorNames) { $distributions['cpu_prior_'+$name]=Distribution $metrics['cpu_prior_'+$name] }
foreach ($group in $toolPhases.Values) { $prefix='tool_'+$group.tool+'_'+$group.phase+'_'; $group.callback_consume=Distribution $metrics[$prefix+'callback_consume']; $group.callback_present=Distribution $metrics[$prefix+'callback_present'] }
foreach ($name in @('geometry','composite','readback')) { $distributions['gpu_'+$name+'_command_interval']=Distribution $metrics['gpu_'+$name+'_command_interval'];$distributions['gpu_prior_'+$name+'_command_interval']=Distribution $metrics['gpu_prior_'+$name+'_command_interval'] }
foreach ($phaseName in @('Down','Move','Up','Cancelled')) {
    if (-not $phases.ContainsKey($phaseName)) { $phases[$phaseName]=@{callbacks=0L;packets=0L;published=0L;consumed=0L;confirmed=0L;unconfirmed=0L;unsupported=0L;invalid_time=0L;outcomes=@{}} }
    foreach ($part in @('callback_total','callback_publish','publish_consume','callback_consume','source_publish','source_consume','source_present','consume_process_inclusive','consume_adopt_inclusive','consume_raster','raster_present','consume_store','store_present','callback_present')) { $key='input_'+$phaseName+'_'+$part; $distributions[$key]=Distribution $metrics[$key] }
    $phases[$phaseName].consumed_of_published=$null; if ($phases[$phaseName].published -gt 0) {$phases[$phaseName].consumed_of_published=[double]$phases[$phaseName].consumed/$phases[$phaseName].published}
    $phases[$phaseName].published_not_consumed=$phases[$phaseName].published-$phases[$phaseName].consumed
    $phases[$phaseName].software_latency_reason=$null
    if ($phases[$phaseName].confirmed -eq 0) { $phases[$phaseName].software_latency_reason='没有可证明的真实内容成功 Present；查看 outcomes/unsupported/invalid_time；不能用 0 ms 替代' }
}
$status='complete'
if ($issues.Count -gt 0) { $status='incomplete' }
if ($completion -and $completion['status'] -eq 'failed') { $status='failed' }
if ($totals.frames_valid -eq 0 -and $status -eq 'complete') { $status='no_data' }
$hardware=$null
try { $hardware=Read-PerfJson (Join-Path $CaptureRoot 'hardware.json') } catch { $hardware=@{status='unavailable';reason='没有采集器硬件快照；软件实际环境见各 Host 初始化文件'} }
$recordedSession=[IO.Path]::GetFileName($CaptureRoot); if ($launch) { $recordedSession=[string]$launch['session_id'] }
$renderDistributions=@{}; foreach ($key in $renderGroups.Keys) {$g=$renderGroups[$key];$renderDistributions[$key]=@{output_target=$g.output_target;tool_mask=$g.tool_mask;device_epoch=$g.device_epoch;frames=$g.frames;work=Distribution $g.work;owner_cpu=Distribution $g.owner_cpu;active_interval=Distribution $g.active;successful_present_interval=Distribution $g.present}}
$matrixSummary=Analyze-ResourceMatrix $CaptureRoot $launch
$graphicsSummary=@{};foreach ($key in $graphics.Keys) {$graphicsSummary[$key]=$graphics[$key]};$graphicsSummary.groups=@($graphics.groups | Sort-Object -Property @{Expression={$_.total_ms};Descending=$true} | Select-Object -First 256);$graphicsSummary['groups_in_csv']=$graphics.groups.Count;$graphicsSummary['groups_json_limit']=256
$batchDistribution=Distribution $metrics['frame_consumed_batches'];$inputBatchCounts=@{unit='consumed_callback_batches_per_retained_owner_frame';frames=$batchDistribution.count;mean_batches=$batchDistribution.mean_ms;p50_batches=$batchDistribution.p50_ms;p95_batches=$batchDistribution.p95_ms;p99_batches=$batchDistribution.p99_ms;max_batches=$batchDistribution.max_ms;p99_reason=$batchDistribution.p99_reason}
$summary=@{schema_version=2;capture_schema=$captureSchema;analyzer_revision=2;input_batch_counts=$inputBatchCounts;graphics=$graphicsSummary;gpu_coverage=$gpuCoverage;render_groups=$renderDistributions;resource_matrix=$matrixSummary;status=$status;session_id=$recordedSession;capture_identity_verified=$captureIdentityVerified;generated_utc=[DateTime]::UtcNow.ToString('o');totals=$totals;distributions=$distributions;input_phases=$phases;input_tool_phases=$toolPhases;gpu_statuses=$gpuStatuses;hardware=$hardware;hosts=$hosts;issues=$issues;analysis_limits=@{hosts=$hostBudget;frames=$frameBudget;input_rows=$rowBudget;gpu_rows=$rowBudget;values_per_distribution=$metricBudget;tool_phase_groups=40;csv_bytes=67108864;csv_line_chars=16384;issue_details=256;top_frames=100};percentile_method='nearest_rank_ceiling_n_times_p';p99_minimum_samples=1000;latency_boundary='RTS_callback_to_real_content_software_Present_return';stage_relationship='inclusive_nested_do_not_sum';normal_mode_evidence='disabled_path_contract 为静态声明；本报告不是正常模式性能实测'}
Write-PerfJson (Join-Path $CaptureRoot 'summary.json') $summary

$report=New-Object Text.StringBuilder
[void]$report.AppendLine('# Draw3 性能诊断包 2 分析报告（兼容 v1）')
[void]$report.AppendLine('')
[void]$report.AppendLine('采集完整性：**' + $status + '**。这是数据完整性状态，不是性能 PASS。')
[void]$report.AppendLine('帧分母 seen/retained/dropped：' + $totals.frames_seen + '/' + $totals.frames_retained + '/' + $totals.frames_dropped + '；CSV 有效/非法：' + $totals.frames_valid + '/' + $totals.frames_invalid + '。')
[void]$report.AppendLine('RTS callback 批次 seen/retained/dropped：' + $totals.inputs_seen + '/' + $totals.inputs_retained + '/' + $totals.inputs_dropped + '；receipt：' + $totals.receipts_seen + '/' + $totals.receipts_retained + '/' + $totals.receipts_dropped + '。一个 callback 可含多个原始包，生产既有策略只使用其最后包；包数在下表单列。')
[void]$report.AppendLine('软件 Present 尝试/API 尝试/S_OK：'+$totals.present_attempted+'/'+$totals.present_api_attempted+'/'+$totals.present_success+'；API 未成功 S_OK：'+$totals.present_failure+'（含非失败 HRESULT 的非 S_OK 状态 '+$totals.present_non_s_ok_status+'）；API 前准备失败：'+$totals.present_preparation_failure+'；无 API 且非失败的 noop：'+$totals.present_noop+'。')
[void]$report.AppendLine('')
[void]$report.AppendLine('| 指标 | 有效样本 | P50 ms | P95 ms | P99 ms | 最大 ms |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|')
foreach ($name in @('frame_work','render_frame_work','primary_render_frame_work','primary_render_owner_thread_cpu','selection_render_frame_work','ink_render_frame_work','present_api_frame_work','render_owner_thread_cpu','render_active_interval','render_successful_present_interval','graphics_frame_leaf','graphics_frame_unattributed','input_callback_gap','active_interval','successful_present_interval','owner_thread_cpu') + @($stageNames | ForEach-Object {'cpu_'+$_}) + @($priorNames | ForEach-Object {'cpu_prior_'+$_})) { $d=$distributions[$name]; [void]$report.AppendLine('| '+$name+' | '+$d.count+' | '+(Display-Number $d.p50_ms)+' | '+(Display-Number $d.p95_ms)+' | '+(Display-Number $d.p99_ms)+' | '+(Display-Number $d.max_ms)+' |') }
[void]$report.AppendLine('')
[void]$report.AppendLine('frame_work 是起止间工作墙钟时间，owner_thread_cpu 只含帧 owner 的 CPU 时间（不含所有 WARP worker），wait 是工作结束后的等待。geometry 包含 Modeler/上传/提交；ingress 与 model 段也可能嵌套；present 包含 ULW 子段。各段不可求和，未观察或非法段使用 null。活动/成功呈现间隔只连接同一 active_chain 的相邻有效帧，idle 后首帧断链，目标/epoch 改变也断开成功呈现间隔。')
[void]$report.AppendLine('分位数采用最近秩 ceil(N×p)，P99 只在至少 1000 个有效样本时显示。覆盖时长包括会话内部空闲，不用于把 idle 当作低 FPS；无输入或无成功呈现时没有相应延迟结论。')
[void]$report.AppendLine('cpu_prior_* 是 frame.start 之前的初始化/首笔前置工作，独立于 frame_work 与 owner_thread_cpu，不能加入帧总耗时。down_count/consumed_count 已含携入 prior 计数，不能再次求和。top-long-frames.csv 按 max(frame_work, 各有效 prior 阶段, 活动间隔, 成功呈现间隔)选最慢 100 个诊断样本，diagnostic_peak_ms 只是排序值，不是总耗时。')
[void]$report.AppendLine('')
[void]$report.AppendLine('| RTS 阶段 | callback 批次 | 原始包 | 发布 | 消费 | 确认内容呈现 | 未确认 | 不支持 proof | 非法时间/proof |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
foreach ($phaseName in @('Down','Move','Up','Cancelled')) { $p=$phases[$phaseName]; [void]$report.AppendLine('| '+$phaseName+' | '+$p.callbacks+' | '+$p.packets+' | '+$p.published+' | '+$p.consumed+' | '+$p.confirmed+' | '+$p.unconfirmed+' | '+$p.unsupported+' | '+$p.invalid_time+' |') }
[void]$report.AppendLine('')
[void]$report.AppendLine('| 实际工具 | 阶段 | 批次 | 消费 | 确认呈现 | 消费 P95 ms | 成功呈现 P95 ms |')
[void]$report.AppendLine('|---|---|---:|---:|---:|---:|---:|')
foreach ($group in @($toolPhases.Values | Sort-Object tool,phase)) { [void]$report.AppendLine('| '+$group.tool+' | '+$group.phase+' | '+$group.callbacks+' | '+$group.consumed+' | '+$group.confirmed+' | '+(Display-Number $group.callback_consume.p95_ms)+' | '+(Display-Number $group.callback_present.p95_ms)+' |') }
[void]$report.AppendLine('')
[void]$report.AppendLine('| 输入延迟口径 | 有效样本 | P50 ms | P95 ms | P99 ms | 最大 ms |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|')
foreach ($phaseName in @('Down','Move','Up')) { foreach ($part in @('callback_total','callback_publish','publish_consume','callback_consume','source_publish','source_consume','source_present','consume_process_inclusive','consume_adopt_inclusive','consume_raster','raster_present','consume_store','store_present','callback_present')) { $key='input_'+$phaseName+'_'+$part; $d=$distributions[$key]; [void]$report.AppendLine('| '+$key+' | '+$d.count+' | '+(Display-Number $d.p50_ms)+' | '+(Display-Number $d.p95_ms)+' | '+(Display-Number $d.p99_ms)+' | '+(Display-Number $d.max_ms)+' |') } }
[void]$report.AppendLine('')
[void]$report.AppendLine('callback_total 为回调整段墙钟（包含等待/解码/发布），不是纯解码成本；source_* 为现有 source QPC 的流年龄口径，与 callback_* 分开，不能外推 RTS 之前的硬件时间。消费延迟可用于没有真实几何 proof 的工具；Laser/橡皮等不支持项不能补成 0 ms。process_end 可能晚于 adopt，它们是从 consume 起的独立包含段，不能串行相加。成功延迟只基于真实输入身份、内容 proof、同帧/输出事实及实际 API S_OK；失败/noop、过期身份、无贡献和缺失端点不算落地。Present 返回是软件完成边界，不是光学可见、屏幕扫描完成或触笔到光子延迟。生产 proof_valid 是内容链证据；CSV 还会复核可导出的 frame/output 身份，未导出的内部对象状态不能离线重建。')
[void]$report.AppendLine('')
[void]$report.AppendLine('真实绘制帧（draw_calls>0）：'+$gpuCoverage.draw_frames+'，工作 P50/P95/max ms='+ (Display-Number $distributions.render_frame_work.p50_ms)+' / '+(Display-Number $distributions.render_frame_work.p95_ms)+' / '+(Display-Number $distributions.render_frame_work.max_ms)+'；主画布/选择辅助/墨迹工具 Draw 帧='+$gpuCoverage.primary_draw_frames+'/'+$gpuCoverage.selection_draw_frames+'/'+$gpuCoverage.ink_draw_frames+'。全部 loop 的低成本分位数不能当实际绘制帧率。按输出目标/工具 mask/epoch 的分组及 owner CPU 见 summary.json/render_groups。render_active_interval 是当前 Draw 帧距前一个保留 owner 帧的条件分布；实际成功呈现间隔另列，均不直接命名 FPS。')
$mainWork=$distributions.primary_render_frame_work;$mainMap=$distributions.primary_render_cpu_upload_map;$mainCpu=$distributions.primary_render_owner_thread_cpu
if ($mainWork.count -gt 0) {
    $workTotal=$mainWork.mean_ms*$mainWork.count;$mapTotal=$null;$cpuTotal=$null
    if ($mainMap.count -gt 0) {$mapTotal=$mainMap.mean_ms*$mainMap.count};if ($mainCpu.count -gt 0) {$cpuTotal=$mainCpu.mean_ms*$mainCpu.count}
    [void]$report.AppendLine('主画布真实 Draw 帧工作合计 '+(Display-Number $workTotal)+' ms（'+$mainWork.count+' 帧）；已观察 UploadMap 合计 '+(Display-Number $mapTotal)+' ms（有效阶段 '+$mainMap.count+'）；owner CPU 合计 '+(Display-Number $cpuTotal)+' ms（有效 CPU '+$mainCpu.count+'）。未知阶段/CPU 未填零；可观察分母不同则不能称精确比例。')
    if ($workTotal -gt 0 -and $mainCpu.count -eq $mainWork.count) {[void]$report.AppendLine('同分母 owner CPU/工作墙钟比例 '+(Display-Number ($cpuTotal*100.0/$workTotal))+'%；它可提示调用期间 CPU 消耗，但不包含所有 WARP worker，也不能直接区分驱动内忙等、内存写入或调度。')}
}
[void]$report.AppendLine('GPU 有效 Draw 样本/不同绘制帧：'+$gpuCoverage.ready_draw_samples+'/'+$gpuCoverage.ready_draw_frames+'；空工作样本：'+$gpuCoverage.empty_work_samples+'；缺 marker：'+$gpuCoverage.missing_marker_samples+'；惰性开启截取 marker：'+$gpuCoverage.clipped_marker_samples+'；最慢已覆盖绘制帧工作 ms：'+(Display-Number $gpuCoverage.longest_covered_frame_work_ms)+'。')
if ($gpuCoverage.ready_draw_samples -eq 0) {[void]$report.AppendLine('**没有可验证的真实 Draw GPU 覆盖，不能评价 GPU 绘制成本或排除 shader。空循环的微小 timestamp 区间不进入 Draw 成本分布。**')}
[void]$report.AppendLine('GPU v2 每代优先 frame 域前 8 个真实图形工作候选，再每 8 项采样；prior 域前 4 项、再每 16 项采样，不消耗 frame 首批机会；v1 每 32 loop 采样，仅以 CPU draw_calls 对空循环过滤，缺少逐阶段实际 Draw 计数。两版均为 immediate-context 命令流区间，含 CPU 提交空隙；clipped marker 表示只观察到惰性开启后的截取段，不是 shader 纯执行。pending/disjoint/query_failed/缺失端点保持不可用。')
[void]$report.AppendLine('')
[void]$report.AppendLine('图形调用细项覆盖：'+$graphics.coverage+'；全部调用 seen/前缀 retained/dropped='+$graphics.calls_seen+'/'+$graphics.calls_retained+'/'+$graphics.calls_dropped+'；独立慢前缀 seen/retained/dropped='+$graphics.slow_calls_seen+'/'+$graphics.slow_calls_retained+'/'+$graphics.slow_calls_dropped+'；两份明细去重后='+$graphics.unique_detail_calls+'。')
[void]$report.AppendLine('')
[void]$report.AppendLine('| 调用位置 | 资源角色 | 操作 | 归属 | 全程调用/有效 | 总 ms | 最大 ms | 失败 | 明细样本 | 明细 P95 ms |')
[void]$report.AppendLine('|---|---|---|---|---:|---:|---:|---:|---:|---:|')
foreach ($g in @($graphics.groups | Sort-Object -Property @{Expression={$_.total_ms};Descending=$true} | Select-Object -First 20)) {[void]$report.AppendLine('| '+$g.site+' | '+$g.resource_role+' | '+$g.operation+' | '+$g.attribution+' | '+$g.calls_seen+'/'+$g.valid_calls+' | '+(Display-Number $g.total_ms)+' | '+(Display-Number $g.max_ms)+' | '+$g.failed_calls+' | '+$g.detail_samples+' | '+(Display-Number $g.detail_p95_ms)+' |')}
[void]$report.AppendLine('')
[void]$report.AppendLine('graphics-call-summary.csv 给出全程固定分组 count/total/max/fail/bytes，明细 P50/P95 仅针对有限前缀+慢前缀的去重样本，带慢样本选择偏向，不能当全会话分位数。frame-graphics-summary.csv 用逐帧固定 leaf 计量和原始区间并集分别解释覆盖；parent/nested、非法与 prior 不重复相加，缺测保持 null。top-long-graphics-calls.csv 保留最慢 100 调用、资源角色/代次/Map flags、原始行和关联帧。')
if ($graphics.coverage -eq 'not_collected_by_v1') {[void]$report.AppendLine('旧 v1 未采集 per-resource/Unmap/绑定/常量写入细项；可定位到已有阶段，不能从残差指定某个未测 API。')}
[void]$report.AppendLine('输入已发布未消费是生产覆盖/准入/合并链的观测，不等于诊断丢包。Down/Move/Up 的 consumed_of_published、published_not_consumed、callback 间隙及帧内实际消费批次数在 summary.json 中；采集掉样另列。')
[void]$report.AppendLine('保留 owner 帧内实际消费批次数 P50/P95/max='+ (Display-Number $inputBatchCounts.p50_batches)+' / '+(Display-Number $inputBatchCounts.p95_batches)+' / '+(Display-Number $inputBatchCounts.max_batches)+' 批/帧，分母 '+$inputBatchCounts.frames+'（包含已保留空循环）；这是计数，不是毫秒。')
[void]$report.AppendLine('')
[void]$report.AppendLine('独立资源对照矩阵：'+$matrixSummary.status+'；'+$matrixSummary.reason+'。成功/不可用/失败/超时等逐项见 resource-probes/matrix.json 和 resource-probe-comparison.csv。该矩阵在正式产品退出后单独离屏运行，不是生产路径重放。')
[void]$report.AppendLine('仅同 profile/资源大小/写入量/迭代且身份验证成功的 HW/WARP measure 样本可比较。48B 常量的 production_field_stores 实际写 36B，memcpy/volatile 写 48B（含 padding），跨写法不能当成等写入量对照；冷轮与 warmup 分开保留。WARP 更快只能作为 backend 敏感候选，不能单凭它认定驱动 bug 或替换产品策略。')
Append-ResourceMatrixReport $report $matrixSummary
[void]$report.AppendLine('')
[void]$report.AppendLine('可优先检查的线索（相关因素，不直接认定硬件型号为原因）：')
$slow=@($longFrames | Where-Object {$_.work_ms -ge 50})
$priorSlow=@($longFrames | Where-Object {$null -ne $_.prior_peak_ms -and $_.prior_peak_ms -ge 50 -and $_.prior_peak_ms -gt $_.work_ms})
$gapSlow=@($longFrames | Where-Object {($null -ne $_.active_interval_ms -and $_.active_interval_ms -ge 50 -and $_.active_interval_ms -gt $_.work_ms) -or ($null -ne $_.successful_present_interval_ms -and $_.successful_present_interval_ms -ge 50 -and $_.successful_present_interval_ms -gt $_.work_ms)})
if ($priorSlow.Length -gt 0) { [void]$report.AppendLine('- 有 '+$priorSlow.Length+' 个样本的首笔/前置阶段超过工作帧成本且 ≥50 ms。优先查看 prior Modeler Reset/Update/Ingress、输入 callback/consume/Present 链；不要把首笔初始化时间遗漏或加到其他帧。') }
if ($gapSlow.Length -gt 0) { [void]$report.AppendLine('- 有 '+$gapSlow.Length+' 个保留候选的活动/成功呈现间隔 ≥50 ms 且大于该帧工作时间。对照前后帧工作、输入与原始等待阶段；deadline/idle wait 归工作结束后的前一帧，间隔异常不能单凭当前帧推定为调度或某个硬件型号的原因。') }
if ($distributions.frame_work.max_ms -lt 50 -or $null -eq $distributions.frame_work.max_ms) { [void]$report.AppendLine('- 没有工作时间达到 50 ms 的有效帧；这不保证尾延迟稳定，请同时看样本量、活动间隔和输入 P95。') }
else {
    $presentHeavy=@($slow | Where-Object {$null -ne $_.present_ms -and $_.present_ms -ge $_.work_ms*0.5}).Length
    $geometryHeavy=@($slow | Where-Object {$null -ne $_.geometry_ms -and $_.geometry_ms -ge $_.work_ms*0.5}).Length
    [void]$report.AppendLine('- 最慢 100 个诊断候选中的工作时间 ≥50 ms 帧：'+$slow.Length+'；present 至少占工作时间一半：'+$presentHeavy+'；geometry 至少占一半：'+$geometryHeavy+'。用 top-long-frames.csv 对照 ULW Map/行拷贝、Modeler、命令、输入数量及设备 epoch。50 ms 是描述阈值。')
}
foreach ($hostRecord in $hosts) {
    $hostStatus='unavailable'; if ($hostRecord.session) { $hostStatus=[string]$hostRecord.session['status'] }
    [void]$report.AppendLine('- '+$hostRecord.host+' 保留前缀覆盖墙钟 ms：'+(Display-Number $hostRecord.coverage_ms)+'；初始化 status：'+$hostStatus+'。')
    if ($hostRecord.initialization) {
        [void]$report.AppendLine('  实际 driver_type='+$hostRecord.initialization['actual_driver_type']+'，presenter_mode='+$hostRecord.initialization['presenter_mode']+'，adapter_available='+$hostRecord.initialization['adapter_available']+'，adapter='+$hostRecord.initialization['adapter_description']+'，feature_level='+$hostRecord.initialization['feature_level']+'。实际设备来自软件；hardware.json 的 WMI 是测试前快照。')
        $dpi=$hostRecord.initialization['target_effective_dpi']; if ($null -eq $dpi -or [double]$dpi -le 0) { $dpi=$null }
        [void]$report.AppendLine('  实际目标 DPI='+ (Display-Number $dpi)+'，显示快照 generation='+$hostRecord.initialization['display_snapshot_generation']+'，topology='+$hostRecord.initialization['display_topology']+'，像素尺寸='+$hostRecord.initialization['display_pixel_width']+'×'+$hostRecord.initialization['display_pixel_height']+'，Modeler DPI scale='+$hostRecord.initialization['model_dpi_scale']+'，目标 FPS='+$hostRecord.initialization['target_fps']+'。这是输出的实际上下文快照，不能证明每个历史帧配置相同。')
        [void]$report.AppendLine('')
        [void]$report.AppendLine('| 初始化阶段 | CPU 墙钟 ms | succeeded | HRESULT |')
        [void]$report.AppendLine('|---|---:|---|---:|')
        foreach ($event in $hostRecord.initialization['events']) {
            $duration=$null
            try { $a=Number $event 'start_qpc'; $b=Number $event 'end_qpc'; if ($a -gt 0 -and $b -ge $a) { $duration=[double]($b-$a)*1000.0/[double]$hostRecord.session['qpc_frequency'] } } catch { }
            [void]$report.AppendLine('| '+[string]$event['stage']+' | '+(Display-Number $duration)+' | '+[string]$event['succeeded']+' | '+[string]$event['hresult']+' |')
        }
    }
}
[void]$report.AppendLine('')
[void]$report.AppendLine('硬件快照查询状态：')
if ($hardware['queries']) { foreach ($query in $hardware['queries']) { [void]$report.AppendLine('- '+$query['query']+'：'+$query['status']+'；字段错误数 '+$query['errors'].Count+'，逐项原因见 hardware.json。') } }
else { [void]$report.AppendLine('- '+[string]$hardware['reason']) }
[void]$report.AppendLine('- 未导出的软件实际刷新率/显示字段保持不可用，不能用 WMI 逻辑 DPI/控制器模式补成实际目标；软件已有字段见各 Host 初始化快照，逐帧画布宽高见 frames.csv。')
[void]$report.AppendLine('')
[void]$report.AppendLine('完整性与限制：')
if ($issues.Count -eq 0) { [void]$report.AppendLine('- 原始文件和计数验证完成；无需据此推断真实 Win7/兆芯或其他目标设备已在本机验证。') }
foreach ($issue in $issues) { [void]$report.AppendLine('- '+[string]$issue) }
[void]$report.AppendLine('- 分析最多 16 Host、65536 帧、262144 输入/GPU 行及每分布 262144 数值，逐行读取 CSV；达到界限会标部分证据。采集容量耗尽或进程崩溃可能丢失内存中未导出的样本。')
[void]$report.AppendLine('- 统计仅代表保留的有效采样前缀。若容量已满，后半程帧与输入没有完整覆盖，掉样后的间歇卡顿不能由这里的分位数排除。Stored Up 主链为 consume→stored commit→Present；它可没有本输入的 Live raster，不能用旧 Down/Move 的 raster 补值。')
[void]$report.AppendLine('- normal_mode/disabled_path_contract 字段只是静态合同声明；本报告没有正常模式实际性能观测。')
[IO.File]::WriteAllText((Join-Path $CaptureRoot 'report.md'),$report.ToString(),(New-Object Text.UTF8Encoding($true)))
$topPath=Join-Path $CaptureRoot 'top-long-frames.csv'
$columns=@('host','frame_serial','work_ms','diagnostic_peak_ms','prior_peak_ms','active_interval_ms','successful_present_interval_ms','geometry_ms','present_ms','control_ms','model_update_ms','model_predict_ms','ulw_map_ms','ulw_rows_ms','ulw_update_ms','deadline_wait_ms','idle_wait_ms','present_succeeded','present_hresult','present_mode','output_target','reason_flags','tool_mask','physical_before','physical_after','consumed_count','command_count','width','height','upload_bytes','draw_calls','map_calls','upload_map_ms','upload_copy_ms','draw_submit_ms','map_failures','device_epoch','graphics_leaf_ms','graphics_unattributed_ms','detail_union_ms','top_graphics_calls')
foreach ($name in $priorNames) {$columns+=('prior_'+$name+'_ms')}
foreach ($name in @('upload_bytes','draw_calls','map_calls','down_count','consumed_count','tool_mask')) {$columns+=('prior_'+$name)}
$writer=New-Object IO.StreamWriter($topPath,$false,(New-Object Text.UTF8Encoding($true)))
try { $writer.WriteLine(($columns -join ',')); foreach ($item in @($longFrames | Sort-Object -Property @{Expression={$_.diagnostic_peak_ms};Descending=$true})) { $cells=@(); foreach ($column in $columns) { $v=$item[$column]; if ($v -is [double]) { $v=$v.ToString('R',[Globalization.CultureInfo]::InvariantCulture) }; $cells+=(Csv-Cell $v) }; $writer.WriteLine(($cells -join ',')) } }
finally { $writer.Dispose() }
Write-GraphicsAnalysisFiles $CaptureRoot
Write-Host ('分析状态: '+$status+'；报告: '+(Join-Path $CaptureRoot 'report.md'))
if ($status -eq 'complete') { exit 0 }; exit 2
