# Draw3 telemetry implementation（2026-10-15）

## 实施范围及所有权

仅修改 Inkeys/Inkeys/Drawing/Draw3。未修改 Main、项目登记、实验配置/i18n、Scripts 或 spec；这些由主会话及其他实现代理负责。无 commit/push、无 GUI/Computer Use、无独立编译执行。

新增 Draw3.Performance.h/.cpp。接线文件为 Host.h/.cpp、DrawingController.cppm/.cpp、ContactInput.cppm/.cpp、RealtimeStylus.cppm/.cpp、GraphicsInitialization.cppm/.cpp、Renderer.cppm/.cpp/RendererPrimitives.cpp/RendererLaser.cpp/RendererSnapshot.cpp、TransparentPresentation.cppm/.cpp。保留旧 RuntimeMetrics 文件及 schema2；新采集不改业务参数、模型算法、输入 record/snapshot、帧策略、透明模式、Map flags、shader选择或正常日志开关。

## 实际生产链

- HostStartOptions.performanceOutputDirectory 空即关闭；CreateHost 空路径在访问全局状态/分配/IO前返回。非空才预分配采集器和启用旧指标 proof owner。
- 每代 Host 生成独立 create-new host-NNNN 目录。启动失败亦导出。Stop 按既有停止 RTS、退出保存屏障、真 owner join 流程封口；导出仅在 !joinable && !drawing 后，全部热写者停止。
- 公共 GetDraw3PerformanceExportStatus 累计所有 Host 的导出/业务失败/不可用及 frames/callbacks/receipts seen/retained/drop/proofOverflow。Main 在 StopProduct 后制作 completion；初始进程exit0不能替代导出证明。
- RTS Down/Up/Packets第一条诊断语句采 callback入口；真实 writer边界保存已分配generation/even sequence和Down admission。不用 Publish返回后 ReadSnapshot猜序号。
- Contact模块只调用 trace.publication 函数指针；独立 MTd Headless 不增加 JsonCpp/D3D采集链接依赖。Session绑定非捕获thunk，正式生产和轻量mock使用同一publication接缝。
- 每次实际 owner消费只登记一次；Register只补真实有效工具/支持属性，NoteConsumed不重复记分母。拒收、导航、取消、覆盖、无几何、失败等留前缀和outcome。
- 延伸原 ControllerContentMetrics strict校验至 Down确认后的 Move与Stored Up；真实非预测Adopt、当前live raster/权威stored链、合成/输出/frame identity匹配、实际API+S_OK返回后才能确认新输入。Source/generation/sequence/admission全部精确join。Up不用deferred Live提前宣称最终稳定；Laser/橡皮明确unsupported。
- 已确认首次时间/proof不被后续停笔推进改写。Stored Up完成并结算旧Down session后回收pending，但保留receipt；失败Stored继续待重试/失效。未注册/抑制source处理结束释放，不累积64槽泄漏。

## 帧/阶段及预算

- 默认固定 frames16384、callbacks32768、receipts65536、GPU2048，pending64、init128、query slots8。构造/编译均校验新CPU payload <=32MiB；与既有 RuntimeMetrics <=32MiB上限一起 <=64MiB。不覆盖、不热扩容；耗尽继续seen/drop分母。
- 帧、模型Reset/Update/Predict、geometry、snapshot、composite、上传Map/CPU copy/Draw submit、present以及ULW Copy提交/同步Map/行拷贝/Update细段读QPC。新细段不新增GetThreadTimes；帧CPU复用旧owner数据。旧RuntimeMetrics模型调用仍会读GetThreadTimes，session.diagnostic_overhead如实说明。
- 阶段inclusive，不可求和。idle后Down初始化/预热进独立fixed prior buffer；下一Begin写prior_*。跨initializeStroke内部Begin的Ingress scope按QPC边界拆分prior/current。wait单独附于对应已封口帧，不污染work。复用既有diagnostic active遍历及实际Register工具填tool mask。
- GPU 32 loop稀疏 timestamp/disjoint，GetData只DONOTFLUSH、每轮只查固定槽，无等待/Flush；未就绪/设备换代/失败均保留status。CaptureGraphics 的共享冷切换helper同步当前已打开恢复帧的device_epoch，防止恢复后成功Present沿用旧设备分组；旧帧不改。命令流区间含CPU空隙，不是shader纯执行成本，WARP不可称物理GPU执行。
- 软件直接记录 actual driver/feature/adapter/UMD version，D3D preferred/11_0 retry、DXGI/RTS/swapchain实际HRESULT及presenter结果。冷期复用已有Display目标DPI/拓扑/尺寸及model DPI/target FPS；目标refresh明确未测，不额外热查询。
- 结束导出固定64KiB批量写，不逐行flush；一次hash join避免O(N^2)。所有文件CREATE_NEW。原始覆盖窗口与capacity、export QPC在session中，超期/崩溃不能伪complete。

## 无窗口验证入口

新增 int RunDraw3PerformanceProbe() noexcept。实际collector和共享controller assembler fixtures，跳过旧proof入口的临时磁盘报告分支；无 HWND/D3D/文件 IO。

测试内容：
1. 空session/空CreateHost：新增clock/collector allocation/query为0（fake clock与实际constructor/query计数基线），固定容量/前缀保留和idle chain。
2. 阶段QPC嵌套、不合法逆序、跨Begin拆分、失败Present/no-op/occluded/旧sequence/generation/output拒绝。
3. 并发四producer callback固定槽、唯一prefix/index、容量seen/drop；producer全部join后检查。
4. 真实Coordinator及Controller Register/Adopt/Raster/Freeze/PresentReturned：Down确认后Move需要自己的当前贡献；failed raster/Present、output mismatch不能确认；Stored Up可跨失败帧和record回收完成。
5. 连续超过64次成功CPU存储/权威Stored软件证明及unsupported终态清理，后续正常笔仍可关联。
6. 实际initializer Scope+Register满receipt仅记一次消费；模型真实Reset/Update wrapper发生在Finish/idle后时，下一帧prior保留、旧帧不污染。
7. 失败ULW API尝试不被后续fail记录抹成noop。
8. 与CaptureGraphics共用实际冷设备代次切换helper，无D3D下旧已封口帧epoch1、当前恢复后成功帧epoch2、query创建0；实际CaptureUploadCapability仅记录后验结果，start为0不伪造duration。
9. 仍按住橡皮physical=1时IdleBoundary+原PerformanceCall等待：3000tick只入idle_wait，帧work保持10tick，下一连续活动chain推进。未知decoder/拒绝回调device字段为UINT32_MAX，不伪装Touch。

## 已执行与待主会话结果

- git diff --check -- Inkeys/Inkeys/Drawing/Draw3：通过。
- 17个跟踪修改文件：BOM与HEAD一致，working CRLF保留。Git HEAD存储LF与checkout CRLF可不同，不能把Git normalization误判格式drift。
- 新 Performance.h/.cpp：UTF-8 BOM + CRLF。
- 本代理未自行编译或运行测试，按调度由main统一验证。2026-10-15 main完整 Debug|ARM64 串行Solution构建exit0；新 --draw3-performance-test 自然exit0，recorder failures=0、production proof failures=0、Draw3ContentProof PASS。--draw3-performance-launch-test 与 --config-boundary-test 亦exit0。日志在 Build/Draw3PerformanceValidation 下。首轮parallel module scan退出1但0源码错误/警告属于构建方式问题，复用既有串行参数后通过，没有为此改源码。
- main首次 Release|ARM64 冷构建20分钟后超时(exit124)，最后为Modeler axis_predictor/kalman_filter，无源码错误；有界wrapper已停止编译进程。主会话随后放行最后一批诊断修正：仅Performance.cpp、GraphicsInitialization.cpp、DrawingController.cpp三文件补恢复帧epoch、post-call细项start0、unknown device及真实idle等待。再次局部diff/check和三文件BOM/CRLF通过；源码现已冻结交main增量Debug/Release及x64Release验证，新增断言待该轮实际结果。

## 剩余边界/风险

- Windows/RTS/D3D/ULW/GPU真机行为、低端/兆芯现场及物理触笔到光子均未运行。本包的软件Present回执不能证明光学可见。
- 默认raw前缀容量在240帧/s约68秒，长轮数据必须看实际覆盖及drop。GPU稀疏未就绪不能补假0duration。
- 新诊断关闭保留可预测空门/小scope，并承诺无新增计时、统计、分配、I/O/query/thread；不声称数学上机器指令完全相同。
- 崩溃会丢未导出的RAM采样，硬件/launch/普通日志需由包保留；出口既有15秒监督不为诊断延长。
