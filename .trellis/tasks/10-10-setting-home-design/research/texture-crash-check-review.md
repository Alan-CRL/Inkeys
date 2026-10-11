# 第六轮 DX11 纹理崩溃独立审查

2026-10-11，trellis-check；Active task 为 .trellis/tasks/10-10-setting-home-design。已读取完整 hook 保存文件、check.jsonl 引用及 PRD/design/implement，优先使用 texture-crash-revision.md 和 research/texture-runtime-crash.md；大规范按相关 Setting 生命周期/错误资源章节补读。未启动 GUI/Computer Use，未 commit/push，未另建 agent。主会话统一实际构建和全套无窗口验证，审查不并行编译。

## Findings (fixed)

- 文件：Inkeys/additional/imgui/imgui_impl_dx11.cpp/.h
  - 问题：事务 Init 失败不发布 backend owner 是正确行为，但 GetLastResult 在无 owner 时只能返回 E_FAIL。factory QueryInterface/GetParent 的真实 HRESULT 会丢失，与本轮初始化失败保留诊断的要求不符。
  - 修复：经主会话明确确认范围后，保留原两参数 Init 入口并新增 long* result 输出重载；真实空 device/context 返回 E_INVALIDARG，COM factory 失败保留原值，成功返回 S_OK。不设置跨 context 全局错误槽、不发布失败 owner/capabilities。
- 文件：Inkeys/Inkeys/UI/Setting/Setting.cpp
  - 问题：初始化与 epoch 重建在 Init 返回 false 时仍读取无 owner 的 GetLastResult。
  - 修复：两处调用持有局部 backendInitResult；失败 Init 使用该值，已初始化 backend 的设备对象/提交失败继续读当前 context GetLastResult。设备原因读取仍在 lease 清理前发生。
- 文件：InkeysHeadlessTests/setting_home_warp_tests.cpp
  - 修复回归：无 backend 的真实无效 device/context Init 均断言 E_INVALIDARG/无 owner；首例同时断言名称及能力未发布。随后真实 WARP Init 断言 S_OK 和实际图集/主页再绘制。原两参数 Init 仍由 Offscreen::Initialize 调用验证兼容。factory COM 故障分支仅源码审计，未声称真实 factory 失败注入。

## Findings (not fixed)

- 初始共享设备移除的触发原因尚不确定；截图/实际日志证明 DeviceRemoved 先于最终空 Release，恢复期曾有 E_OUTOFMEMORY。未将品牌几何、字体图集或系统压力任一候选写成确定原因，也未据猜测改字体密度/尺寸、设备或架构。后续现场仍需观察修复版诊断和共享客户端日志；这不影响已确认的失败资源事务和空 Release 缺陷修复。
- 原 Setting.Base.cppm 的静态图片/presentation bool helper 仍不返回具体创建 HRESULT，调用方以 E_FAIL 和真实 device reason 分类。该旧接口不属于本次限定的动态 atlas/DX11 后端自修范围；若后续日志需要定位其中具体 OOM/API 失败，可单独为这些 helper 增加窄的结果输出。本次没有扩大模块接口范围。
- 未无损触发真实 WARP TDR/设备移除，也未运行完整 Setting HWND/coroutine 或 Win7 SP1+KB2670838 实机。测试中的 REMOVED/RESET 是显式一次 HRESULT 注入；真实 WARP 的非法尺寸失败、正常资源创建、更新、清理、再绘制与像素读回应分别记录。

## 审查确认

- CreateTexture2D/CreateShaderResourceView 任一步失败均撤销本次所有 COM 部分资源；TexID/BackendUserData/OK 只在完整成功后发布。失败保留 WantCreate，实际 DrawData 立即中止，避免无效 GetTexID 和残缺 Present。
- DestroyTexture 对 NULL texture/SRV 分别清理并清标识；重复 Invalidate 幂等。WantUpdates 丢失资源会撤销；UpdateSubresource 后 device reason 失败不将更新消费为 OK。
- VB/IB 创建、Map、constant-buffer Map 和所有 device-object 创建均保留失败结果。IB Map 失败归还已映射 VB；constant-buffer Map 失败跳过命令并走原状态恢复。清理不覆盖原失败 HRESULT。
- 初始化 false 不留下后端；重建失败不提交 epoch。Setting 在绘制失败前置阻止 Present，并将原 HRESULT/FAILED removed reason 路由到既有 Retry/DeviceLost；HUNG/INVALID_CALL 分类使用真实共用 helper，不反复使用失效旧 epoch。
- CheckColdEntry 仍早于新故障测试；未用故障测试的 Frame 预热掩盖旧冷启动验证。原主页动效、字号/三语言、Show/Hide/尺寸门禁和既有输入/滚动测试没有被本次自修改写。
- rendering-and-ui.md 新增事务、错误传播/去重、Map 清理和真实/注入证据区分与实现一致；已提醒主会话补充 Init 结果重载和 GetLastResult 的 owner 前提。

## Verification

- Lint/static：pass。git diff --check 退出0；Headless vcxproj XML与 check/implement jsonl 及引用文件可解析。
- 编码格式：pass。本轮7个产品/测试/工程文件对 HEAD 核查 UTF-8、BOM、全 CRLF 与末行格式不变；4个自修文件使用字节替换保持原格式。
- TypeCheck/实际编译：由主会话统一完整 InkeysRepo.sln Debug|ARM64；本审查没有单独或并行启动 MSBuild，写记录时尚未收到最终结果，不宣称通过。
- Tests：由主会话统一 InkeysHeadlessTests.exe --no-window；新断言已加入真实 WARP 回归，但写记录时尚未收到本轮最终结果。
- 自修源码已经冻结并通知主会话；无剩余阻塞代码发现。主会话可在最终统一验证完成后引用其独立结果。

## 首次统一验证失败后的夹具复核

主会话通知后，仅复核最新夹具/诊断/局部初始化修改；未改其它源码，未自行运行 build/test。已读取实际结果文件：build-texture-crash-verified-result.json 为 Exit0、458.284119秒；headless-texture-crash-verified-result.json 为 Exit3、17.4100798秒。stderr 显示本机 FL11.0 WARP 接受16385×1，原 E_INVALIDARG 假设失败；随后夹具手动修改状态但未撤销已创建 owner，导致 WantCreate 断言。第一次 headless 明确失败，不计为通过。

最新修正审查：
- 使用真实8×8 CPU像素后，仅给测试描述 Width=0，实际传给生产 CreateTexture2D；没有增加产品尺寸判断，也没有用注入返回值冒充此项真实 API 失败。
- fixture 复用前设置 WantDestroyNextFrame/UnusedFrames/WantDestroy 并调用生产 UpdateTexture，先撤销任何意外成功的 owner/TexID，再 DestroyPixels/新 Create；重复错误不会跳过资源生命周期清理。
- ExpectedResult 在失败时打印 actual 和 expected HRESULT，随后恢复 std::dec；正常结果不新增日志。
- vtx_resource/idx_resource 局部零初始化修复新故障注入控制流相关的 C4701，不改变正常 Map/Unmap 路径。
- 最新两文件 UTF-8/BOM/CRLF/末行对HEAD审计通过；git diff --check通过。没有发现需要新增源码修改的问题，已再次通知主会话源码保持冻结，可以重建。
- TypeCheck：首轮实际原生完整构建通过；最新修正后的完整构建待主会话复验。Tests：首次完整运行失败，修正后的实际完整运行待主会话复验；不将静态复核写作执行通过。
