# Draw3 Performance schema v1（冻结接口）

## 文件/公开 API

- HostStartOptions.performanceOutputDirectory: std::wstring，空关闭；绝对会话根，由 Host 创建全新的 host-NNNN 子目录。
- Draw3.Performance.h（传统 header）/Draw3.Performance.cpp，命名空间 Inkeys::Drawing::Draw3。
- int RunDraw3PerformanceProbe() noexcept; 纯 CPU，无 HWND/D3D/I/O。
- PerformanceExportStatus GetDraw3PerformanceExportStatus() noexcept; 含 enabled(bool)，hostRuns/exportedHostRuns/failedHostRuns/startupFailedHostRuns/runFailedHostRuns/unavailableHostRuns/framesSeen/framesRetained/framesDropped/inputsSeen/inputsRetained/inputsDropped/payloadBudgetBytes/receiptsSeen/receiptsRetained/receiptsDropped/proofOverflow(uint64_t)。全进程跨 Host 累计；Stop 后读取。
- host-NNNN/session.json、frames.csv、input-events.csv、gpu.csv、initialization.json；CREATE_NEW，导出在 RTS 停止和 owner join 后，失败亦导出。
- 本 schema 使用 snake_case；CSV 单行为一个帧、RTS callback 批次、GPU 样本。时间为原始 QPC，不填假零毫秒；未测 QPC=0，分析器须排除未知端点。

## session.json

schema_version=1, qpc_frequency, status (complete/startup_failed/run_failed/unavailable), exported (true only after all raw files succeeded), startup_failed, run_failed, payload_budget_bytes, allocated_payload_bytes, legacy_payload_upper_bound_bytes, frames_seen/frames_retained/frames_dropped, inputs_seen/inputs_retained/inputs_dropped, receipts_seen/receipts_retained/receipts_dropped, invalid_intervals, proof_overflow, gpu_sampling_interval=32, gpu_status, gpu_query_hresult, gpu_ring_full, gpu_pending_at_stop, callbacks_bundled_packets, publication_clock_boundary="before_release_commit", latency_boundary="real_content_software_present_return", stage_relationship="inclusive_nested_do_not_sum", disabled_path_contract（静态关闭路径合同，不是伪造的运行观测计数）, diagnostic_overhead, process_span_relationship, prior_work_relationship, unavailable(bool)。同时含 frame_capacity/input_capacity/receipt_capacity、first_frame_qpc/last_retained_frame_qpc/first_input_qpc/last_retained_input_qpc、export_start_qpc/export_end_qpc。
完整性由 completion、所有文件成功、status 和 seen/retained/dropped 共同判断；容量掉样不是未采样。崩溃未导出时不能生成 complete。

## frames.csv

frame_serial,start_qpc,end_qpc,present_return_qpc,active_chain,idle_boundary,physical_before,physical_after,width,height,reason_flags,command_count,down_count,consumed_count,terminal_count,tool_mask,dirty_left,dirty_top,dirty_right,dirty_bottom,present_attempted,present_api_attempted,present_succeeded,present_hresult,present_mode,output_target,output_revision,content_revision,full_present,thread_cpu_ms,control_qpc,ingress_qpc,model_reset_qpc,model_update_qpc,model_predict_qpc,geometry_qpc,snapshot_qpc,composite_qpc,upload_map_qpc,upload_copy_qpc,draw_submit_qpc,present_qpc,ulw_copy_submit_qpc,ulw_map_qpc,ulw_rows_qpc,ulw_update_qpc,deadline_wait_qpc,idle_wait_qpc,stage_observed_mask,stage_invalid_mask,upload_bytes,draw_calls,map_calls,map_failures,discard_maps,no_overwrite_maps,device_epoch,prior_ingress_qpc,prior_model_reset_qpc,prior_model_update_qpc,prior_model_predict_qpc,prior_upload_map_qpc,prior_upload_copy_qpc,prior_draw_submit_qpc,prior_stage_observed_mask,prior_stage_invalid_mask,prior_upload_bytes,prior_draw_calls,prior_map_calls,prior_down_count,prior_consumed_count,prior_tool_mask

- 所有 *_qpc 段为累加 tick duration，不是端点；仅 stage_observed_mask 对应 bit 存在且 stage_invalid_mask 无 bit 时有效。
- stage bit 顺序：control(0),ingress(1),model_reset(2),model_update(3),model_predict(4),geometry(5),snapshot(6),composite(7),upload_map(8),upload_copy(9),draw_submit(10),present(11),ulw_copy_submit(12),ulw_map(13),ulw_rows(14),ulw_update(15),deadline_wait(16),idle_wait(17)。
- control 覆盖 geometry 前恢复/导航/命令；ingress/model/上传/Draw 可嵌套；geometry 含 model/upload/submit；present 含 ULW 子段。不可把段求和。thread_cpu_ms 来自旧帧 owner CPU（unavailable 为空），不代表所有 WARP worker。
- wait 在工作 end_qpc 后计入该帧；idle_boundary 标记该帧之后进入 idle，下一激活推进 active_chain。即使仍按住已冻结橡皮或等待断笔续接（physical_after>0），主动等待也有 idle_boundary；等待总 tick 独立于 frame_work。chain=0 为初始/未确立。闲置后的间隔不能判卡顿。失败帧的有限恢复退避保留原 chain，以保留真实失败 stall 证据。
- device_epoch 表示该帧实际使用设备的代次；若 Begin 后发生实际 device/context 恢复，冷切换同步更新当前帧代次，旧已封口帧不变。
- present_attempted 表示调用软件 Present，present_api_attempted 表示确实调用 Present1/ULW；noop 不计成功新呈现。HRESULT 保存有符号数。present_mode 是 TransparentPresentMode numeric（ULW=0,DComp=1），target primary=0/selectionULW=1；target 才能判断真实 ULW。

## input-events.csv

callback_token,phase,tablet_context_id,contact_id,packet_count,device_type,callback_entry_qpc,source_qpc,published_qpc,callback_end_qpc,publication_succeeded,callback_outcome,record_id,generation,sequence,admission_revision,consume_qpc,process_end_qpc,adopt_qpc,raster_qpc,stored_qpc,present_return_qpc,frame_serial,workspace_id,page_id,scene_generation,raster_generation,output_generation,output_target,output_revision,content_token,item_token,proof_kind,proof_supported,proof_valid,outcome,effective_tool

- 一行一个 RTS Down/Move/Up callback 批次（phase Down=0,Move=1,Up=2,Cancelled=3）；Packets bundle 仅选最后一个包是生产既有策略，packet_count 保留分母。所有 callback 保留至固定容量。
- record_id 为会话匿名编号，不导出指针；generation/偶数 sequence 在 coordinator 真实 writer 发布边界捕获，不在 Publish 返回后读 snapshot。
- device_type 沿实际 InputDeviceType：Touch=0、Pen=1、MouseLeft=2、MouseRight=3；UINT32_MAX(4294967295)=未解码/unknown。没有成功进入publication的拒绝回调不能以默认0归因给Touch。
- publication_succeeded 与 callback_outcome 独立。callback_outcome 枚举字符串：published,publish_rejected,state_busy,decoder_missing,decode_failed,touchpad_filtered,no_binding,cancelled,invalid_argument,unknown。
- owner receipt 按完全相同 record/generation/sequence 与 source_qpc/admission join。找不到不是成功；分别 published_not_consumed、consumed_no_geometry、adopted_no_raster、rastered_no_present、present_failed、proof_unsupported、cancelled、confirmed_software_present；旧身份/被覆写/采集drop都不能补成功。
- proof_kind Live=1,Stored=2；proof_supported=false（例如 Laser、橡皮、无visible几何）的延迟仅报告 callback/publication/consume。普通 Pen/Shape/Highlighter 为真实内容软件 Present，不能称原始坐标像素或触笔到光子。
- proof_valid 仅源身份、实际非预测几何、有效当前栅格/Stored权威链、实际合成、同frame/content/output以及真实API成功返回全部匹配时 true。失败API/noop/旧frame/旧output不会确认。
- process_end_qpc 是实际 consumeLatestSnapshot/initializeStroke 返回点，可能晚于 Adopt；不是整个帧耗时；adopt/raster/stored 是真实步骤边界而非GPU completion。

## gpu.csv

frame_serial,device_epoch,status,frequency,geometry_begin,geometry_end,composite_begin,composite_end,readback_begin,readback_end,hresult

status: ready,pending,disjoint,query_failed,invalid,owner_stopped,device_replaced。稀疏 immediate-context timestamp/disjoint，GetData DONOTFLUSH 单轮非阻塞，不 Flush/等待。成功且 Frequency>0且端点单调时才能转毫秒。是命令流区间含 CPU 空隙，不等于 shader 纯执行；WARP 必须按 software backend 解读。

## initialization.json

schema_version=1, events array，每条 stage(string),start_qpc,end_qpc,hresult(signed),succeeded(bool),detail0/detail1/detail2/detail3(uint64)。固定容量，不热格式化。另含 actual_driver_type/feature_level/adapter_available/adapter_description/vendor_id/device_id/subsys_id/revision/dedicated_video_memory/dedicated_system_memory/shared_system_memory/driver_version_available/driver_version/driver_version_hresult，以及实际 presenter_mode/width/height/map_no_overwrite_supported。无法查询字段 availability=false。


## 最终实施补充（2026-10-15）

- failedHostRuns 专指导出失败，不与业务 startupFailedHostRuns/runFailedHostRuns 混淆。unavailableHostRuns 同时包含已知 RuntimeMetrics/Controller 旁挂准备失败及 QPC frequency 不可用；session.unavailable=true/status=unavailable 时不能 capture_complete。
- receiptsSeen/receiptsRetained/receiptsDropped/proofOverflow 公开全进程累计，主握手需检查；callback drop 和 receipt drop 不合并、不重复计算一次实际消费。
- normal_mode_* 常量计数字段未导出。disabled_path_contract 是明确的静态合同；真实 off clock/query/allocator 与 producer 固定容量/并发检查在无窗口 probe 运行，不能从字符串推定测试已通过。
- stage Relationship 不变。prior_* 指 frame.start_qpc 之前初始化/预热/唤醒工作的累计 tick，单列且不属于 frame_work 或当前帧 owner CPU。跨内部 Begin 的 Ingress scope 按 QPC start/end 与 frame.start 拆分；prior masks 使用相同 bit 映射。deadline/idle wait 才附在已封口对应帧。down_count/consumed_count 含携入 prior 计数，prior_down_count/prior_consumed_count 可核查来源。
- effective_tool 及 tool_mask 位号沿实际 DrawingTool：Pen=0、HardPen=1、Highlighter=2、Eraser=3、Laser=4、SolidLine=5、DashedLine=6、OutlineRectangle=7、FilledRectangle=8；UINT32_MAX=未准入/未知。倒转/右键已经按实际有效工具解析。消费但无法证明的工具仍有事件与 consume 数据。
- 成功新呈现只接受实际 API + HRESULT S_OK。DXGI_STATUS_OCCLUDED 等非S_OK保留原结果且不确认；ULW失败仍保留 apiAttempted=true。业务原 SUCCEEDED 逻辑保持。
- receipt mutation 不覆盖已确认事件的首次时间/proof。Up 必须 Stored 权威终态链；deferred Up 的 Live 先行提交不当作 final stable Up 成功。
- 已确认 Stored Up 在旧 Down Session 结算后回收 pending 旁挂，但不删除/覆盖 receipt；失败 Stored 保留至实际 retry 或失效。未准入/导航/抑制 source 在处理结束释放未注册 pending。
- callback_outcome 与 publication_succeeded 独立；坏Up可能实际发布Cancelled=true且callback_outcome=cancelled。源被丢弃、clock逆序等分别 source_invalidated/invalid_interval。效验时仍按 phase/source/gen/seq/admission 对齐，不把 callback_outcome 强制要求为 published。
- 初始化细 HRESULT 若 start_qpc=0，仅记录实际结果，未测该细项完整 duration。实际 graphics/presenter/rts aggregate spans 有真实 start/end。冷信息补 model_dpi_scale/model_dpi/target_fps（实际 StrokeModelConfiguration）、target_effective_dpi/display_snapshot_generation/display_topology/display_pixel_width/display_pixel_height（Host已解析同一Display快照）。目标refresh rate明确 unavailable/not_measured_by_Draw3，不用WMI列表冒充真实目标。
- 默认 raw 容量 frames=16384、callbacks=32768、receipts=65536、GPU samples=2048。前缀耗尽不覆盖、不热扩容；全程 seen 与 drop 继续统计。240帧/s约68秒raw帧容量，采样覆盖以实际保留起止为准。GPU稀疏32 loop、8个query slot；候选 loop 可能合法无实际 render frame/无marker，不能凭孤立GPU row制造CPU帧缺失。
