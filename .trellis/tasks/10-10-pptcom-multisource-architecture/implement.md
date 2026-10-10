# 后续实施分期与验证计划

> 2026-10-10用户已批准实施，P0已完成；现授权P1及必要阶段commit。P0子任务为`10-10-pptcom-p0-baseline`；P1子任务已完成纯逻辑与fake验证，P2-P5保留为后续路线，不在本轮执行。

## 分期与进入条件

总体范围已确认并创建P0子任务；后续独立交付按下表显式依赖逐阶段拆分，不靠子任务树顺序表示。P0/P1已完成，本轮止于P1。

| 阶段 | 交付与依赖 | 验收/回滚锚点 |
| --- | --- | --- |
| P0 兼容基线与依赖核查 | 无代码重构地核查dynamic/释放、当前线程模型、PptCOM产物交接；验证ZMQ/命名管道候选对Win7 SP1/.NET4和现有平台约束，选择一个首发传输；核查现有Active/PID/WPS分值及统一前台观察边界，固定源码采样周期、隔离通道实测与后续切换/页更新测量点；补充损坏COM/权限/多窗口实机门禁。本轮无GUI授权，真实Office延迟与Win7/跨权限运行不算已验证。依赖已确认的活动放映优先/双向权限需求与最终规划批准。 | 原DLL/TLB/manifest与行为基线；不能为一次依赖失败升级类库或删除兼容分支。 |
| P1 通用合同与仲裁纯逻辑 | Envelope/候选/身份/能力/route与target修订、单一命令路由、统一活动分级、pending挑战/回滞/失效快速路径、来源恢复和绑定提交确认；fake adapters与可控时间测试。依赖P0合同确认。 | 尚未接管生产；多源同show/不同show/同文件不同实例/乱序/失联可独立验收。 |
| P2 内置适配器与共享通道 | 保留旧ABI，新增纯值共享内存与控制扩展；先LegacySingleCandidate旁路对照，后迁移native观察/命令消费者。依赖P1。 | 旧DLL可回退；共享通道损坏不拼接新旧状态。保持Draw3 ready。 |
| P3 独立helper与IPC/双向权限管理 | 新后台host复用PptCOM核心和P0选定通道；source epoch/重连/命令ACK、降权token/提权runas、UAC取消/进程句柄/父进程失效退出、可选设置与发布包装。依赖P0依赖方案和P2。 | 关闭兼容开关恢复内置；UAC取消/启动失败/断联仍有类库支持，不终止Office、不循环请求提升。设置归属须先沿现有配置链核查，不能猜新文件。 |
| P4 完整多候选与外部仲裁接管 | ROT纯值目录、同实例多放映、BrokerSelect与generation校验、最终选择单一所有者、轻量活动证据增量及旧本地分值归一化；逐项维护dynamic和acquisition释放规则。依赖P2/P3。 | 单独风险门，P2/P3不能冒充此阶段完成；可切回LegacyAutoSelect。 |
| P5 集成验收与扩展合同冻结 | 贯通输入、墨迹、业务控件与切换；协议版本/部署/降级说明；第三方fake按低能力模型验收。依赖P4。 | 真实第三方adapter另立任务，本任务不实现其协议。 |

## 必须保持的风险文件边界

- `PptCOM/PptCOM.cs`：dynamic根对象、ROT去重、FullCleanup、WPS兼容、事件/owner。P0先只读；P2/P4最小diff，关键特殊处理加中文注释，不顺便切apartment。
- `PptCOM/PresentationDescriptor.cs` / `SlideShowSession.cs`：同次页身份、HWND标量例外、busy和generation；现有合同与测试保持。
- `Inkeys/IdtPlug-in.cpp` / `Inkeys/Inkeys/Business/PptSession.*`：接管原状态与命令入口，serviceGeneration/target revision和ready路径必须一起校验。
- `Inkeys/SuperTop/IdtToken.h`：优先新增专用helper启动包装，避免影响现有SuperTop启动行为；必须验证目标用户/session和token API成功，补充普通host通过runas启动管理员helper；UAC可见、helper自身不显示控制台，不把已有SuperTop提升流程的exit(0)带入本任务。
- `InkeysRepo.sln` / `Inkeys/Inkeys.vcxproj` / `PptCOM/PptCOM.csproj` / 发布脚本：只有真实新增host/源码/依赖时才登记，复用现有MSBuild配置，不创建替代构建系统。

## 关键验证矩阵

| 测试层 | 必须覆盖 |
| --- | --- |
| 合同与共享内存 | C#/C++字段/宽度/对齐、Unicode、超限、截断、未提交、abandoned mutex、唤醒竞态、初始全量、版本不匹配及控制方向 |
| 仲裁fake | 同show两源不累加分、同文件不同实例、同标题不同文稿、一个source多候选、Active/PID/WPS证据分级、前台与Inkeys焦点、同等级保持、短脉冲挑战取消、持续挑战一次提交、软反向切换回滞、真实前台/失效绕过冷却、源恢复稳定后择优、PID/HWND复用 |
| 切换与反馈 | 心跳/重复包不充当新活动样本、迟到Active不能覆盖当前前台、pending目标/epoch变化取消、B确认到达时已切C则拒绝、被动绑定不自动激活、自有显式焦点交接不构成新挑战、正常PageChanged不等待切换防抖、Legacy/Broker控制模式互斥 |
| helper管道候选 | Win7/.NET4接口可用性、高host→低helper/低host→高helper访问、ACL与完整性、半帧EOF/部分读写、IO取消/有界退出及重连全量 |
| 消息与命令 | sequence缺口、重复/乱序、旧epoch、旧candidate generation、ACK丢失、OutcomeUnknown不重翻、来源切换后deadline请求、失联重连全量 |
| 当前managed tests | borrowed root零释放、temporary子先父后且一次、普通异常/busy/缺SlideID、同session End和原生批注保护 |
| native headless | parser/fallback、结束页、route/target修订、切换旧输入收尾、成功Present与UI ack之后才ready、源失联不能清错会话 |
| 真实Office/WPS | 正常及用户可复现受损COM、事件不可用轮询、跨文稿/多实例/同实例多show、busy、反复启停、自然结束/手动关闭Office后释放 |
| 权限与生命周期 | 提升host+普通Office、提升host+提升Office、两域同时放映、普通host+管理员helper、主程序自身不提升、UAC取消不重弹、不同管理员凭据组合、token失败、helper依赖失败/崩溃/关闭开关/host退出 |
| 运行基线 | 类库/helper在Win7 SP1/.NET4运行验证，具体目标架构分别记录；ARM64在现有Windows11设备验证，不把Win7测试要求与ARM64组合混淆 |
| 延迟/资源 | 既有基线 vs 迁移后：事件→源纯值→仲裁→target→Present→UI ack，退出完成、句柄/引用趋势；单个采样不宣称P95或所有版本性能 |

原规划轮仅检查文档、链接与状态；P0完成了隔离无窗口探针与既有managed测试；当前P1增加纯值服务、fake合同和headless测试。后续真实Office/WPS或窗口测试须用户明确授权GUI；默认只运行无窗口验证，未执行项标NOT VERIFIED，不能自动启动产品/浏览器。

## 后续命令与构建纪律

构建系统已确认：Visual Studio/MSBuild，PptCOM是经典.NET Framework csproj。P0再次确认当前native工程依赖和自动DLL/TLB复制已移除；接口变更须显式交接同次产物，验证主程序仍构建完整 `InkeysRepo.sln Debug|ARM64`。

```powershell
# 从本机Visual Studio安装定位ARM64原生MSBuild.exe，记录绝对路径到任务验证日志。
# 在同一个PowerShell invocation中规范化Codex PATH，再调用MSBuild。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $pptNativeMsbuild InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
# runner总超时至少5分钟；检查exit code与首个有意义错误。
```

只验证纯托管模块时可按现有配置构建 `PptCOM.Tests/PptCOM.Tests.csproj /p:Configuration=Release /p:Platform=AnyCPU`，同样先执行PATH规范化。PptCOM接口/产物变化需核对同次生成DLL/TLB、native import和manifest交接，再做完整solution构建；不能只单独构建Inkeys.vcxproj。

已确认可复用的无窗口入口：

```powershell
& ./PptCOM.Tests/bin/Release/PptCOM.Tests.exe
& ./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

新增protocol/arbitration测试应并入适合的既有测试入口；依阶段选足够最小范围，不为纯文档运行工程构建。构建失败首先区分代码与SDK/依赖/PATH/runner环境；不改代码绕过环境错误。所有已有文件保持原BOM/编码/CRLF；完成核查git diff。不自动commit/push。

## 规划与P0完成记录

- [x] 用户明确授权创建planning任务。
- [x] 已核查现有COM、native、token、测试和官方ZMQ/runtime约束。
- [x] PRD、design、implement和research均形成草案；context manifests收录真实spec/research。
- [x] 用户确认活动放映优先，补充普通主程序启动管理员helper及Win7 SP1/net40通信约束。
- [x] 已做无损PRD收敛，消除阻塞产品问题，更新架构/验收/研究并呈现最终规划。
- [x] 用户随后明确批准最终规划进入实施，当时只执行P0。
- [x] 已创建、激活并完成P0子任务，经全范围check后归档；当时无生产逻辑改造、无commit/push；随后本轮开始前已提交P0。

## P0 已完成（2026-10-10）

- [阶段验证记录](../archive/2026-10/10-10-pptcom-p0-baseline/validation.md)与[最终复核](../archive/2026-10/10-10-pptcom-p0-baseline/check-report.md)均通过P0交付范围；P0子任务已归档；P1本轮已完成，P2-P5未实施。
- 首发helper通道选定本机双向命名管道，见[依赖决策](../archive/2026-10/10-10-pptcom-p0-baseline/research/transport-decision.md)。内置共享状态/类库接口设计保持。
- 原26个生产源码/随附资源散列一致，PptCOM.Tests实际重建与运行exit0；最终net40引用编译与独立探针11/11通过，历史3轮10/10另存。
- 真实Win7/原版.NET4、双向跨权限/不同管理员凭据、生产ACL及remote/peer验证、损坏COM/Office实际释放、端到端延迟和最终防抖参数仍为后续实机门禁，未把本机通过当作它们已通过。
- P0补充了现有owner/直接命令/事件COM路径、ROT单赢家和新session自动Focus证据；后续旁路/接管须保留原线程/释放并避免来源切换重复focus。
- 下一阶段为P1通用合同与仲裁纯逻辑。P0归档时未创建/激活P1；随后已获用户指示进入P1。

## P1 源码与验收已完成，待签名收尾（2026-10-10）

用户明确指示开始下一步并确认创建P1子任务；按既有P1合同执行，P2-P5不在本轮。P0签名commit为6d4931b7。新增纯值Business服务与headless，未接管生产链路。

[阶段验证记录](../10-10-pptcom-p1-contract-arbitration/validation.md)和[最终独立复核](../10-10-pptcom-p1-contract-arbitration/check-report.md)：修后完整InkeysRepo.sln Debug ARM64及fresh headless均exit0，P1为138 checks/0 failures。COM基线25项完全一致，唯一预期变化为native工程3行文件登记；dynamic/ROT/旧线程及释放、ABI、随附DLL/TLB未改。活动优先、弱证据回滞、最新确认与单路命令均为纯逻辑交付；Win7/双向权限/损坏COM/实际Draw3与时延门禁仍待后续。下一阶段为P2，本轮未启动。

P1收尾状态：源码与138项fake验收已通过；本轮SSH签名提交因1Password错误退出1，P1尚未提交或归档，待恢复签名后收尾，P2未启动。
