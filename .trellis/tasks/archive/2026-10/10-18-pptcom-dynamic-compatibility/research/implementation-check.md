# 独立实现审查（trellis-check）

- 日期：2026-10-18；任务：10-18-pptcom-dynamic-compatibility。
- 角色：主会话派发的独立检查者；没有再派生 implement/check 子代理，没有回退其他实施者修改。
- 已读：check.jsonl 全部入口、PRD/design/implement、implementation-review-brief、native-implementation、implementation-decisions，以及本次 COM/native/UI 所属规范。
- 范围：最终托管源码及测试、真实 IDispatch/连接点夹具、native 会话和 UI 发布/提交路径、必要规范与 DLL/TLB 交接。产品源码已冻结。
- 执行边界：本检查者只做静态检查及获授权的局部修复；所有 MSBuild/无窗口进程由主会话串行执行。没有 Office/GUI/Computer Use、commit/push。

## Findings (fixed)

### 本检查者直接完成的局部修复

1. **ComEventConnection.cs / ManagedCompatibilityTests.cs：断连分类不一致。** 根对象已把 RPC_E_SERVER_DIED（80010007）、RPC_E_SERVER_DIED_DNE（80010012）认定为明确断连，连接点却仍永久等待重试。补齐两个终态 HRESULT；回归覆盖全部五个终态，重复 TryDisconnect 只消费一次 CP acquisition，并覆盖根对象的同类断连。
2. **PptCOM.cs / ComEventConnection.cs：诊断异常能够阻断资源清理。** FullCleanup 的前后 Console.WriteLine 可能在控制台已关闭时抛出；retirement registry 的失败日志若先于所有权归还抛出，会令 CP/cookie 从队列丢失。两处清理诊断改为 best-effort；registry 先在锁内恢复失败 lease、递减 inFlight，再锁外写不可抛诊断。新增 throwing TextWriter 回归，实际调用生产 FullCleanup 和 Retry，验证根取得释放、失败 CP 仍被持有，以及后续成功重试恰好释放一次，无 GC。
3. **ppt_session_tests.cpp：生产 EndScreen 降级缺跨层回归。** 新增真实 wire parser → ResolvePptControlPage 的 Unavailable/EndScreen/currentPage=0/totalPage=3/HWND=0 用例；fresh raw=-1/3 可提供基础结束控制，raw total 不一致拒绝，ResolvePresentationTarget/ResolveEndScreenTarget 都不产生墨迹目标。
4. **IdtPlug-in.cpp / ppt_session_tests.cpp：旧结束页 envelope 可误开写入。** trustedEndScreen 原先只检查 stable topology 和 EndScreen，未要求当前 raw 页仍为结束状态，存在 descriptor 落后于 raw 正常页/总数变化的窗口。复用已计算的生产 ResolvePptControlPage，增加 controlPage.known && currentPage==-1；不新增协议或镜像算法。真实 parser+shared helper 回归验证 stable End topology 对 raw=-1/3 接受，对 raw=3/3、-1/4 拒绝。保留原同场 binding 条件。

以上现有文件保留 BOM/CRLF；未引入全文件格式化、依赖/CI/SDK/公开接口变更。

### 独立审查定位、由共享文件实施者完成并已复审

- **同 binding 的迟到 Begin/End 不能直接重放生命周期。** 回调现只推进失效屏障、观测提示及唤醒，owner 以 fresh Count/show identity/View 确认场次。旧 binding lease 直接忽略；同 binding 迟到 Begin/End 不再凭事件本身换场。
- **断连/最后文稿关闭不能无限保留旧根。** 明确 RPC/RCW 断连触发旧 binding 清理；ActivePresentation 普通失败时仅由独立 Presentations.Count=0 证明已无文稿。busy/成员失败仍是 Unknown，不猜 Inactive。
- **EndScreen 已取得的 fresh 标量不能因拓扑失败丢失。** 生产 reader 保留本次 Done/Count 等纯值，清空不完整 IDs；普通失败为 Unavailable/EndScreen，可达基础结束控制，busy 为 Unknown，不用旧数值伪造新有效页。
- **通用 CLR 事件包装不满足逐笔所有权要求。** 即使 handler 不保存 WnObj，通用 sink 仍会物化 VARIANT 参数为 RCW；普通 unsubscribe 抛错后还可能失去重试能力。最终使用内部 raw IDispatch sink，不物化参数，只读取 DISPID 提示；FindConnectionPoint 的独立 CP/cookie 取得由 owner 显式持有/解绑/释放。
- **raw CCW 必须真实 QI。** 最终 internal ComImport 接口保留四个原始 IDispatch 槽，ICustomQueryInterface 同时映射 EApplication source IID 与 IID_IDispatch；不是以 Advise 失败后轮询算事件验证通过。真实原生 CP fixture 已成功调用。
- **生产诊断污染线程 IErrorInfo。** Marshal.GetHRForException 会设置线程错误对象；真实夹具曾发出原始 busy HRESULT 8001010A，CLR 却重抛前序事件绑定 InvalidCastException，reader 因而错误分类为 Unavailable。产品改用 ComFailure.ErrorCode 纯读 ExternalException.ErrorCode，普通 managed 异常记未知 0；补原有/空 IErrorInfo 指针账本回归。保留夹具原始故障行为证明修前/修后，未靠反射放宽普通 COM 访问。该问题是实施验证捕获的新日志问题，不冒充 20261014a 历史实机根因。
- **ROT finally 独立收束。** enumMoniker/rot 分别使用安全释放 helper，避免一个释放异常跳过另一笔。
- **规范同步。** 旧 WPS FinalRelease/GC 条款、旧十秒 retry/HandleBusy 事实和“只用 InvokeMember”约束已按本任务用户要求更新；dynamic、事件/HWND 例外、owner 释放、基础控制/可信墨迹和实际测试边界一致。

## 最终代码路径复核

### dynamic、取得/释放与 owner

- 常规成员由生产 MarshalLateBoundComAccessor 有限 switch 分派到真正 C# dynamic；Item 为 dynamic.Item(index)，命令和可选 setter 也使用 dynamic。普通属性没有 InvokeMember 回退；反射只留已批准的 HWND 标量例外。
- root 由 service owner 持有；reader 和 typed HWND 标量只借用。View、View.Slide、Slides、Item、Navigation 等每次取得均具名并 finally 单次 ReleaseComObject；按子到父释放，不 foreach COM 集合，不链式遗失 temporary，不以 RCW identity 代替 acquisition 计数。
- ROT best 是列表 alias，获胜取得明确单次转移，落选/重复扫描/同 identity 返回仍消费各自取得；helper 及轮询中间返回可配平。
- owner mailbox 捕获 binding/session/generation，跨线程 getter 只读纯值。尚未开始的超时请求可取消且不会迟到执行；已开始的外部 COM 调用不能靠本地超时撤销，不自动重发有副作用命令。
- busy 快速复核与稳定维护间隔分开；失败不能把缓存 stable 当成新 Valid。Count 缺失时以 fresh show.Presentation/View 等能力降级，场次依赖 show identity，HWND0 不等于结束。
- 生产无 GC.Collect、WaitForPendingFinalizers、FinalReleaseComObject 或循环 Release 补漏。

### 事件连接点

- raw Invoke 不物化 WnObj/取消参数；其借用原始指针不被盲目 Marshal.Release。生命周期仅由 owner 确认，不缓存入站 COM root。
- FindConnectionPoint 取得只转移一次；Unadvise 成功或明确断连后消费 CP 取得。失败先 deactivate sink，再保留同一 CP/cookie 到静态 retirement registry，旧 server Reset 后下一 owner 仍可重试。
- COM 调用在 registry 锁外；PendingCount 包含 inFlight。新增订阅有 pending 上限，停止有三轮有界显式重试；仍失败返回 -1/数值诊断并保留 lease，不能称为已经释放。没有 GC、终结器、kill、无主后台线程或 sleep 等待自然退出。
- 真实 CP 夹具覆盖重复同 identity 事件参数、Advise/Unadvise、失败后 owner 重试与引用归零；这是受控 in-proc 证据，不是实际 Office 退出证明。

### native、UI 与旧目标隔离

- CanStartPptControlSession 要求 Active、非零 binding/show revision；HWND0 不进入 IsWindow 结束判据。历史非零窗口只有销毁或成功读取到其他 PID/已知不同创建时间才证明旧窗结束；查询失败是 Unknown，创建时间只在双方已知时增强 PID 复用判断。
- BasicControl 发布 fresh 页码并清除旧 Presentation target/PptInfoStateBuffer，不授予墨迹 ready；Pending 可保留同场旧显示但暂停写入，新场次不复用旧文稿目标。
- pageAckEpoch 随真实 PptState publication 进入 PageControl RenderSnapshot 与 Commit tracker，内部五参回执带该帧捕获值；Ppt 在 stateMutex 内校验模式/epoch 后转发原 native 四参。没有在回调时借当前 epoch 冒充旧帧，重复 publication 幂等。
- 已搜索新增枚举/结构/回调的全部调用点，.cppm/.cpp 对应，位置 aggregate 未发现错位。局部锁序复核未发现本轮引入反向环；没有据此宣称全系统并发已证明。
- 同场窗口锚提升只刷新 Bar 布局，不 End/Begin、不重复恢复位置。当前可信窗口未知时不 Focus/自动接管 Office 笔；同 owner/root/session 的对象定向 Exit 在当前 HWND0 时仍可用，非零不同窗口拒绝。

## Findings (not fixed) / 验证边界

以下不是用当前测试可以标为通过的项目，也没有在本轮擅自扩展设计：

1. **真实旧提供方与后台退出 NOT VERIFIED。** 未运行 PowerPoint2007、PowerPoint2010/MSO14、WPP2013、正常/损坏实机、真实进程退出后立即打开下一文稿。没有目标安装环境/GUI 授权；.NET4 目标在本机已安装 .NET Framework4.8.1 CLR 上的测试不等于 .NET4 RTM 或旧 Office 运行证明。WPP 版本也不能直接换算成微软 MSO 版本。
2. **损坏层级存在能力边界。** 真实 NoTypeInfo profile 的 Name/Item/Activate 使用生产 dynamic 成功，typed QI/事件不可用可降级；但返回 count=1 而 GetTypeInfo=TYPE_E_CANTLOADLIBRARY 的 TypeInfoFailure profile，生产 dynamic 明确报 80029C4A。不能承诺所有 IDispatch/类型信息/激活损坏均可恢复，不能为了让测试绿就给普通成员加反射。
3. **持续拒绝/阻塞外部 COM。** 重试账本防失主，但外部始终拒绝或不返回时不能声称已解绑/进程必退；停止 -1 是可观测的未完成边界。实际跨进程代理、Office/wpp 本身的引用行为仍需设备验收。
4. **同 identity、同 HWND 的重开若完整落在两次观测之间。** 没有任何新事实可供 owner 区分时仍是提供方可观测性限制，不以迟到事件猜换场。
5. **已有多实例选择策略。** ShouldKeepCurrentBindingForSamePresentation 在 typed HWND 不可得时沿用 FullName 维持 binding；两个实例打开同一路径可能保留旧实例。本轮保留原选择政策，未重做多实例优先级，也不把唯一 Win32 候选测试外推为所有选择情形已验证。
6. **既有 unsafe current/total 同步。** 跨 managed/native 的完整读写同步协议仍是规范中的待确认项；本轮保留 ABI，没有用单侧锁宣称解决全部跨边界一致性。
7. **GUI/D3D/输入实机 NOT VERIFIED。** 没有启动产品主窗口、D3D Debug Layer、绘制/prediction/抬笔/resize、真实 PPT 控件命中和 Office 工具接管。用户权限只允许无窗口验证，本轮也未改 renderer/HLSL/存储格式，不能拿 headless 代替这些检查。

## Verification

所有执行日志位于忽略目录 Build/Validation/pptcom-dynamic-20261018；实际退出码由主会话统一记录。

| 检查 | 结果与证据 |
| --- | --- |
| Lint/差异 | git diff --check --ignore-submodules=all 通过；无独立 C#/C++ lint 配置，以编译补充类型检查。 |
| 编码/换行 | 受跟踪修改文本 BOM 与 HEAD 一致，checkout 均 CRLF、无 lone LF；git core.autocrlf=true 的 index LF 转换不算格式破坏。新增源使用约定格式。 |
| Managed build | PptCOM.Tests/PptCOM 的 Release / AnyCPU 最终构建退出0；.NET4 目标与 PIA 引用保留。managed-verified-build.stdout.log。 |
| Managed core + real dispatch x64 | 实际 Framework64 CLR，43 cases / 0 failures，core PASS，进程退出0。managed-verified-x64.stdout.log，stderr空。 |
| Managed core + real dispatch ARM64 | 实际 FrameworkArm64 CLR，43 cases / 0 failures，core PASS，ARM64_MANAGED_TEST_EXIT_CODE=0。managed-verified-arm64.stdout.log，stderr空。 |
| Full solution | ARM64 原生 MSBuild、既有 InkeysRepo.sln Debug/ARM64、PATH 同进程规范化；最终增量构建退出0（24.27s）。solution-final-debug-arm64.stdout.log。前一完整构建退出0（114.98s）。 |
| Headless / --no-window | 最终同一 EXE / --no-window 在允许临时写入环境复核退出0（112.26s），日志末尾 PASS animation correctness、stderr空。headless-no-window-unrestricted.*。 |
| DLL/TLB / ABI | 输出目录与 Inkeys 随附 DLL/TLB SHA256逐一一致；主会话核验最终 EXE 内嵌 DLL 相同。TLB 与原始 baseline 位级一致，既有 GUID/vtable/12+7 schema 无变更。 |
| GUI/旧设备 | 未执行，范围及原因见以上边界。 |

DLL SHA256：A9A2A059A6B599E5ECF3D67F317C0DEBD0764DE0DC78032A634BED79754084E2。
TLB SHA256：C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606。

环境诊断没有通过改源码/工具链绕过：

- 普通 sandbox 的完整构建报 C1902；全新无 header /Zi /FS 探针在相同 cl19.44.35229、mspdb14.44.35229、相同命令环境内退出2，沙箱外自动审批后退出0，后续原完整 solution 同配置成功。证据为 pdb-probe-sandbox.log / pdb-probe-unrestricted.log。
- 普通 sandbox 的 --no-window 出现配置/原子写临时文件失败；主会话用相同最终 EXE/参数在允许写入环境复核，没有改测试或产品代码绕过。
- 完整构建存在原有 hashlib++ narrowing warning 和非致命 pwsh.exe 不在 PATH 的消息；构建退出0，最终关键 DLL/TLB/内嵌资源另行核验。没有为追求零 warning 修改第三方代码/构建配置。

## Affected package / Spec Quality Check

- **ppt-interop**：COM ABI/12+7 schema、真实 dynamic 访问、每笔取得/释放、事件失败账本、owner/cache、busy/关闭/换文稿/未知HWND、普通能力与墨迹门禁均按最终代码复核。必要规范已同步；真实版本/进程退出仍单列 NOT VERIFIED。
- **native-desktop**：实际生产 .cppm/.cpp/调用点一致，既有 solution/ARM64 host 构建与随附/嵌入 DLL/TLB 链完成；工程只登记必要托管测试/事件源，不变依赖/SDK/CI。窗口操作仍遵守可靠身份，基础生命周期不依赖 HWND 成功。
- **native 可复用不变量**：本轮未新增绘制线程外 D3D 操作，L0/L1/L2、dirty rect、resize、presenter fallback、CPU/GPU 布局无直接变动；失败路径仅处理本任务 COM/会话资源。该规范指向独立 demo 的 shader/solution 不能替代主产品；本轮用主产品构建，不将未执行的 GUI/Debug Layer 检查标通过。
- **cross-layer**：实际 COM adapter → envelope/parser → fresh basic control/可信 target → Draw3 Present → PptInfoStateBuffer → UI RenderSnapshot/Commit → epoch ack → input gate 路径已静态追踪，测试复用生产 helper，未靠手造 DTO/重复实现证明关键生产路径。
- **范围和可追溯性**：任务必要源码/测试/规范及随附 DLL；未动 additional/子模块/旧引擎、未创建 git commit 或 push，构建产物日志留忽略目录。

## 事件参考证据

微软 .NET Framework [ComEventsHelper reference source](https://github.com/microsoft/referencesource/blob/main/mscorlib/system/runtime/interopservices/ComEventsHelper.cs) 与 [ComEventsSink reference source](https://github.com/microsoft/referencesource/blob/main/mscorlib/system/runtime/interopservices/ComEventsSink.cs) 用于核对通用包装的参数物化与 Unadvise 所有权边界。reference source 和普通 Action mock 都不当作真实提供方退出证据；本轮 raw sink/CP 结论限于已运行的原生夹具。
