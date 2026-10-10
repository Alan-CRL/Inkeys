# 提交后完成度核对

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 按分工只读任务/context、已提交源码和既有证据，不重复 611 项历史审计，不构建、不运行测试、不启动窗口；仅写本报告，不改产品/spec/共享账本。

## 结论

**不能确认“工程侧全部完成，只剩用户的 Win7 等人工验收”。** `e32a5fc06096c1e4ab88a29866c60ebe323fd722` 已提交本轮大部分实现、回归与记录；当前核心源码与最终独立审查的 blob 相同，仍有一个明确的退出失败边界及可由工程侧继续补充的生产故障注入。正式发布宏、正式 CI 和真机验收需要单列，不能把它们都说成代码 bug。

当前 `HEAD=e32a5fc06096c1e4ab88a29866c60ebe323fd722`，tree `df05eeeaed48950b76b772e76b3105026a5f04ef`，父提交/H0 `8b156fca59f0337a6afc6d722941666fcf143080`，分支 `chore/publish`。核对时 tracked 工作区和 index clean，仅四个未跟踪 `inkStrokeModelerTest/{inkPixelShader,inkVertexShader,laserParticleEmitCS,laserParticleUpdateCS}.cso`。这四个生成物不在 commit。任务仍 `in_progress`，不应仅因已经 commit 标记 completed。

## 工程侧仍可执行的项目

### 1. P1-01：双重监督创建失败后的退出处理没有关闭

- `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp::ArmCore`：296–301 行先记录绝对截止并尝试 `CreateThread(FallbackDeadlineThread)`；346–357 行创建/握手 helper；384–386 行当两个方案均失败时置状态 3、返回 `ArmResult::Failed`。没有第三条终止路径。
- `Inkeys/IdtMain.cpp::SetOffSignal`：260–261 行第一次 CAS 已占据正式退出意图；263–277 行 `ArmShutdownSupervisor` 后，`Failed` 仅记日志，随后继续 `CrashHandler::Shutdown`/其他清理。重复请求被 CAS 拒绝，无法重新 Arm。若后续 join/磁盘 I/O/窗口 owner 卡住，进程可超过 15 秒。
- 现有 suite 的模拟 helper 创建失败验证的是 fallback 仍成功建立；`simulateLateDeathForTest` 可禁止本地 fallback，但没有“同时禁止 fallback 与 helper”的完整生产入口验证。不能用任何单方案 PASS 覆盖双重失败。
- **分类：confirmed 条件性代码缺口，可由工程侧修复与隔离自动验证。** 用户已选择“15 秒无条件结束，可丢未 durable 请求”，无需把这一既有合同重新交用户选择。应给 `Failed` 一个不依赖新线程建立的确定退场处理，并测试真实意图入口、唯一退出/重启和最后 committed 文件保持有效；不能绕到 catch-all 继续损坏状态。

### 2. P1-02：Host 无内部超时必须按调用场景判断

- `Draw3.Host.cpp::Host::Impl::Stop`：1343–1348 行保存屏障 condition wait、1357–1358 行两个 `CloseAndDrain`、1363 行绘制线程 join 均没有内部 deadline。这是代码事实。
- **正常 Close/Restart 已返回 Armed 或 FallbackArmed 时，独立进程/线程提供 15 秒进程级截止。** 无内部超时不等于正常退出已被确认无条件死锁；等待已接受保存完成且到期强退符合用户边界。可先关闭 P1-01，再用“真实 Host 绘制线程卡住 / 保存 worker I/O 卡住”分别做隔离故障注入证明该组合合同，不必为复审标题重写所有 worker。
- `IdtMain.cpp:2527–2546` 的 DComp 初始化失败→ULW 重新建窗属于启动回退，尚未接受 Close/Restart，也没有 Arm。`Window.cpp::StopUnlocked:690–701` 的 owner join 无超时；该失败边界需要有界启动失败处理/注入证据，但不能把未请求结束的启动回退直接称作“正常关闭 15 秒违约”。
- 必须进一步收窄旧报告：`Host::Start` 在 graphics 失败返回 false 前，已经在 `Host.cpp:1234–1250` join/drain/detach；RTS 失败的 `:1278–1297` 也做清理。所以不能假定随后 DComp fallback 的 `StopProduct()` 一定进入“活动 Host 的最终保存屏障”。无界等待可能发生在 Start 失败清理自身，或 fallback 的 Window owner join；当前没有真实永久阻塞证据。
- **分类：已建立监督时正常 drain 合同可保留；双失败与尚未建立监督的启动失败路径需工程侧边界核对/故障注入。** 这些不是只能用 Win7 人工操作完成的项目。

### 3. F-057：局部状态机已修，生产使用者交错证据仍可补强

- 当前 `Draw3.ContactInput.cpp:837–882` 的 `ClosingDiscarded` 精确 generation CAS，让普通 Discard/Recycle 在 producer 仍 Closing 时有界返回；`Close:564–640` 由唯一 producer 最终单次释放槽。生产快照在 `:815–835` 前后核身份，拒读已交出的 handle。
- `InkeysHeadlessTests/draw3_contact_tests.cpp::TestClosingDiscardLiveness`（507–642 行）用真实 Coordinator、真实 Close CAS 后 pause hook，有红→绿及单槽回收/代次复用证据；不是复制一套状态算法。最新三架构构建/Headless/CLI 与独立报告支持这一修补，不应把它重新写成未修。
- 但普通 Controller 页边界 `DrawingController.cpp::sealPresentationContacts`（6307–6325 行）与 `:6363–6386` 拒收调用，没有独立暂停 Close 后继续处理真实页命令的整链测试。已有 fatal helper 和 Laser 第二 Touch 测试不能替代普通页切换。`AbortUnqueuedDown` 的 Closing 等待、Host Reset 前所有 RTS/窗口 producer 静止也未有完整交错证明。
- **分类：不是已确认的新 bug；生产交错/生命周期自动验证缺口，工程侧可在隔离数据和显式 hook 下继续调查。** 用户现场截图无线程栈，不能用该局部修补宣布现场卡死唯一根因已解决。

### 4. F-063：启动退场顺序已修，故障分支没有被通用测试实际触发

- `IdtMain.cpp` 的 D101/D201/D301/D401/D202/D102 失败分支已前移 `SetOffSignal(1)`；当前 blob `a45440114603e1d6a9ba3629cb4e3178706b5837` 与最终 reviewer 增量结论一致。源码顺序修复已完成。
- `validation.md` 的 `hf-startup-order-final-*` 记录 Debug ARM64、Release 三架构完整 Solution/Headless/PptCOM/supervisor exit0；但 Headless 和 supervisor CLI 没有进入这些 `wWinMain` 失败分支。它们证明编译/相关通用路径，没有证明每条失败分支中提示、owner join、15 秒监督和窗口不残留。
- **分类：修复完成、生产启动故障注入未验证。** 可由工程侧制作显式、私有 child fault trigger，验证选定代表性分支；真实 Win7 运行另列人工。不能将该自动化缺口全部交给用户去造初始化失败。

### 5. 提交后记录和生成物收尾

- `handoff.md` 顶部仍写“2026-09-30 本轮未创建 commit/全部工作区改动保留”，与当前 Git 不符；`validation.md` 的 HF 仍以 H0+2366 文件指纹记录，其中含四个未跟踪 `.cso`；task.json 的 `commit` 仍 null。PRD/implement 的“不提交”是最初授权边界，用户已在本会话明确授权这次 commit，应追加说明覆盖，不删除历史失败证据。
- 最终 reviewer 的源码 blob 与 commit 关键源码相符，不能仅因其开头旧 HEAD/HF 文案就把审查全部作废；`final-quality-check.md` 关于 HF 缺失/旧 reviewer blob 的部分文案被后续增量和提交覆盖，需要明确标为历史状态。
- **分类：工程侧文档同步/清理工作。** 记录 commit/tree 与当前非源码生成物的排除，核清理权限，仅清理本任务已确认为生成物的精确路径；不要提交二进制来制造 clean，也不要 archive/finish 整项任务。

## 属于正式发版准备的事项

`Inkeys/IdtMain.h:20` 的 `// #define IDT_RELEASE` 在 H0 已经如此。workflow 只配置 `workflow_dispatch`，`validate_release` 默认 true（`.github/workflows/build-windows.yml:5–12`），52–69 行会在正式执行时检查精确宏；它同时提供显式 skip。Release MSBuild 配置和这个产品发布标记是两件事。

已记录 `CL=/DIDT_RELEASE` 的隔离 ARM64 Rebuild/单实例测试通过，随后恢复无宏 Rebuild。**开发候选保持宏关闭本身不是代码 bug，也不应为了关闭审查表擅自修改版本/渠道/发布策略。** 维护者准备正式制品时，需要决定启用该宏并跑正式 matrix/打包检查；这属于发布前准备，未执行真实 CI/签名/上传不能写成发布通过，也不影响“工程修补是否已实现”的窄结论。

## 用户设备和体验验收清单范围

| 项目 | 最小范围与通过条件 |
| --- | --- |
| Win7 图形矩阵 | Windows 7 SP1 **仅 KB2670838**，x86/x64；分别有 Hardware FL11.0 和无 Hardware FL11.0→WARP FL11.0。记录驱动、实际 driver type/FL/presenter；DComp 缺失应 ULW，保持 FLIP_SEQUENTIAL，两种 DWM 禁用；透明、穿透、首次/连续成功呈现、resize/sleep/device-lost、关闭无拦截面。不能安装额外 KB 代替目标。 |
| 真实笔/触摸 | 软硬笔、荧光笔、固定/速度橡皮、Laser、已开放形状；点/慢/快/折返/停住/Up/Cancel、快速换工具、多接触；尤其禁 Laser 多指第二指、选择后桌面可交互、clear/undo/redo。记录长文档/长时资源和无输入失效。 |
| UI3 与比较体验 | 展开/收起/反向/拖动吸附、属性/颜色/粗细/Fine Dial、主题/光影/SVG、设置和长期 idle；与用户实际 HC 和 H2 在同一设备、同 Release/效果/轨迹下至少三轮，冷/首次/稳态分开。保留长帧/动画完成时长；不能凭 callback 数或单轮最好值宣称改善。 |
| 显示与窗口 | 多屏跨 DPI、分辨率/刷新率变化、混合 GPU、resize、睡眠恢复；Win10/11 DComp/ULW 和 fallback；定格/放大镜/设置 owner/排除列表。混合 GPU 黑屏需实际证据，不能默认已修。 |
| 正常结束/主动重启/崩溃 | 设置真实按钮、确认式重启及其他实际入口；正常退出无自动重启、旧死后只有一个新实例、正确参数/单实例、启动崩溃有限重试、无透明拦截残留。Win7 真 UEF/dump/报告/新 GUI 恢复分别验；仅使用受控独立测试数据/进程，强杀和 FailFast不作为普通 UEF验证。工程侧卡住worker注入不移交为人工项。 |
| PPT/Office/WPS | 已声明支持的组合，多窗口进入/翻页/退出，最后一页和 EndScreen 独立身份；每页真实墨迹 durable 与关闭后新进程可见恢复、重排仍对应 SlideID；旧 page-index 文件完整保留，新会话独立保存，Desktop/PPT不串页。旧页 gate/NotReady入口保持关闭。 |
| 实际存储故障 | 独立测试数据下无权限/磁盘满/坏文件/半写/恢复失败；最后 committed UInk/index仍可读，失败有安全退路；15 秒强退允许丢 pending，不能承诺未持久化笔迹零丢失。断电与硬终止能力边界另记。 |

实际 HARDWARE/WARP、成功 Present 和可见恢复应分别留日志/结果。已记录的隐藏 Host、真 PowerPoint 页身份和 UEF 隔离 suite 可以缩小人工步骤，不可替代真实输入/目标系统/主观体验。

## 用户有意保留、不属于未完成修复

- 更新来源认证与 HTTP/HTTPS/HTTPS→HTTP 回退，及旧 `智绘教.exe` 回退，按用户选择保留；同源哈希不等于来源认证，应明确 accepted residual risk，不追加升级任务。
- 旧 PPT page-index 不迁移到 StableSlideId，新会话另存；已关闭/Unsupported/NotReady 产品入口不开放。
- Win7 FLIP 保持，禁止引入 bitblt或两种 DWM方案；第三方重大升级、最低系统/设备线程合并/版本渠道均未授权。

## 文件身份与验证口径

| 已提交文件 | Git blob |
| --- | --- |
| Inkeys/IdtMain.cpp | a45440114603e1d6a9ba3629cb4e3178706b5837 |
| Inkeys/IdtMain.h | 32cddfde8356d0f22af18b2e0bc0e1d2db4e800c |
| Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp | 8301a55fbcd3bbeddfeb83f51303364bbe83512b |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp | 63f6518d3aa1f419fb0835e954038df8579ea72f |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp | e9b1792417d7cb40c14ddca969ae84ab09a83777 |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp | 6b77b771c45479a5a35a65c07280853fcd8c752a |
| InkeysHeadlessTests/draw3_contact_tests.cpp | 21ad3e729245bd749228a203b10d50a0c7fcfca3 |

本轮无产品机械修补。Lint/TypeCheck/Build/Tests：按只读分工未执行，引用已有验证不冒充本轮动态 PASS。文档/源码静态阅读和 Git状态/对象身份已核对；报告不改变 Trellis 状态、commit、分支或远端。
