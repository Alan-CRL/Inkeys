# 实施与验证计划

## 开始门
- [x] 用户授权创建新任务，当前 bugfix/vibration 分支已有，无需新建 worktree/branch。
- [x] Git 历史、生产调用链、后端兼容与现有输入边界调查。
- [x] 完成 research/bar-haptic-path.md，核对严格关联/token与既有标记合同；跨驱动关联不被文档保证，失败仅跳过触觉。
- [x] PRD最终收敛通读；design/implement与jsonl上下文验证。
- [x] 2026-10-11 用户在最终方案之后回复“是的，批注并实现”，随后回复“继续”；批准实施并在关键逻辑添加中文注释。

## 执行顺序
1. trellis-implement 子代理加载两份research、prd/design/implement和 implement.jsonl；按 trellis-before-dev 读取目标规范。所有代理保持最小diff、原编码/换行、不改他人文件，不commit/push，不开GUI。
2. Host绘制线程创建/初始化/注入/清理 PenHapticFeedback；可选初始化失败不影响启动。生产后端精确绑定当前Pen，禁用后台GetForCurrentThread泛设备回退；保留现有系统库安全动态加载、波形回退与默认强度。
3. 增加最小 detach/重置过期身份及预设反馈状态收敛；Controller在选择/隐藏/不准入/Laser/Pan/退出相关边界停止，不改变RTS/光标事件所有权。
4. Bar只被动观察 Pen WM_POINTER，沿当前 WM_TOUCH 翻译保留按接触生成的有界token，保持触摸/取消/屏幕坐标与HiMsg去重语义。工具/类型按钮的既有接受点发布一次Click。请求走专用产品入口/Host控制mailbox，不转发为画布消息，不挤占画布/保存命令队列。不能唯一关联或已过期则只省略触觉。
5. 在现有headless测试入口补充生产实际策略/必要的窄fake seam回归，确保旧实现能暴露本次缺陷；不复制实现到测试，不增加新的构建系统。
6. 运行最小充分验证和diff/编码检查，随后 trellis-check 子代理核对产品调用链/失败路径/线程/光标/测试，并只修本任务缺陷。
7. 主会话按 trellis-update-spec 保存适量触觉迁移及来源/lifecycle合同；按trellis-finish-work记录任务状态与验证。用户未授权commit，跳过提交，不push；真实设备未验证部分明确保留。

## 预期修改边界
- 必需生产：Draw3.Host.cpp，Draw3.HapticFeedback.cpp/.cppm；Controller触觉状态/停止逻辑，以及WindowController触觉mailbox的必要边界。
- UI接线：Bar.Interaction.cpp、Bar.Button.cpp有效业务接受点与其私有状态/marker helper，Draw3.Product.h/.cpp、Draw3.Host.h/.cpp专用触觉请求API；如需helper，仅加入局部、生产实际使用的身份/token策略，不改HiMsg第三方定义。
- headless：优先扩展现有draw3_bridge_tests/draw3_contact_tests；若必须编译实际haptic module或独立小测试源，仅更新已有InkeysHeadlessTests.vcxproj登记，不改SDK/依赖/solution结构。
- 不涉及墨迹算法/HLSL、UInk、用户配置、PptCOM接口或第三方源码。

## 关键回归矩阵
| 场景 | 预期 |
| --- | --- |
| Pen/HardPen/Shape、Highlighter、Eraser、倒转笔尾 | 使用原波形映射，缺可选波形走基础回退 |
| Laser、Mouse/Touch绘制 | 无连续笔震动 |
| Hover预设后Selection/隐藏/Laser/Pan/不准入 | 显式停止与旧request失效，现有cursor策略不变 |
| 新不支持的笔/同数字Pointer ID新代际 | 不复用旧controller/失败缓存；重新有效绑定或no-op |
| 有效Pen工具/类型点击 | 每个被接受操作最多一次Click，动作次数不变 |
| 取消、拖出、程序刷新、Mouse/Touch、兼容鼠标副本 | 不发送Click |
| token/请求满、超时、身份歧义、Host停止/重启 | 只丢可选触觉；原UI/保存/绘制不中断 |
| 缺DLL/导出/factory/property/controller/基础波形、发送失败 | false/no-op，不抛出到Host启动或输入链 |
| Run异常/启动失败/正常停止 | 同线程Stop/Shutdown、COM初始化配对，无旧请求跨Host代际 |

## 验证入口
本机实际构建系统为 Visual Studio/MSBuild，生产验证按用户规则使用完整 InkeysRepo.sln Debug|ARM64；已通过vswhere定位到ARM64原生MSBuild，无需硬编码版本到项目。

在同一个PowerShell调用中：
~~~powershell
$buildSearchPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
Remove-Item Env:PATH -ErrorAction SilentlyContinue
[Environment]::SetEnvironmentVariable('Path', $buildSearchPath, 'Process')
$env:MSBUILDDISABLENODEREUSE = '1'
$vsLocator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nativeMsbuild = & $vsLocator -latest -products '*' -find 'MSBuild\Current\Bin\arm64\MSBuild.exe' | Select-Object -First 1
& $nativeMsbuild InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64 /nr:false
~~~

构建总等待至少5分钟，长时间无输出不重复重启；检查真实进程退出码和第一个有意义错误。环境失败独立报告，不污染项目配置/SDK/签名。构建后执行已有Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window（先核实实际输出路径），不启动Inkeys.exe主窗口或隐藏窗口harness。

补充：相关fake/生产策略回归、原输入光标过滤测试、git diff --check；检查既有文件BOM/CRLF没有变化，dumpbin /imports静态检查没有新增WinRT/新Pointer API加载时依赖。额外Win32/x64构建仅当本次平台相关变化或ARM64证据无法覆盖实际问题时按风险扩大，不把ARM64结果当Win7实测。

## 回滚与未自动验收项
按Host注入、Bar/token/API旁路和连续状态补丁逐块回退；无持久化迁移。实体笔体感、多笔交接、真实Win7/非震动硬件和混合设备光标仍需目标设备验收；本次授权只做无GUI验证。研究/编译/静态结果分别记录，不声称硬件反馈已实测。

## 完成记录（2026-10-11）
- [x] Host/后端/连续反馈与 UI3 已接受按钮触觉接线，关键逻辑中文注释。
- [x] 独立 trellis-check 全范围审阅并修复旧绑定、消息交错与 marker 有符号边界。
- [x] 完整 solution Debug|ARM64 编译退出 0；完整 --no-window 退出 0。
- [x] 静态 imports、git diff --check、15 既有源码 BOM/CRLF 对照通过。
- [x] 新增 native-desktop/pen-haptics.md 并更新索引，完整证据见 validation.md。
- [x] 按用户规则保留未提交改动；归档和日志均 --no-commit。
- [ ] 实体震感、真实 Win7、跨驱动关联与混合设备 GUI：本轮授权范围外，保留设备验收。

## 设备实测后的追加执行
- [x] 添加实验室“测试触控反馈”默认关闭持久化开关和中英繁文案；根据设备反馈改为重启后生效。
- [x] 在 Bar/产品入口/Host/Controller/后端关键接受与拒绝边界增加结构化、可关联、有限频控制台诊断；失败路径不影响原业务。
- [x] 独立 trellis-check 核对跨线程、锁、输入/光标不变和诊断覆盖。
- [x] 完整 InkeysRepo.sln Debug|ARM64、已有 --no-window、i18n check、diff/BOM/EOL 验证。
- [x] 保持任务进行中，不归档；用户本轮未要求 commit/push。

验证记录：InkeysRepo.sln Debug|ARM64 使用 ARM64 原生 MSBuild，通过命令行 LinkIncremental=false 后成功；首次增量链接发生 MSVC 内部 LNK1000，未修改项目配置。InkeysHeadlessTests.exe --no-window 退出 0，Scripts/i18n.ps1 check、git diff --check 和原有 BOM/CRLF 检查通过。实体笔反馈与真实设备日志仍需用户实测。

设备日志复核：Bar 笔触摸因缺 `TOUCHINPUTMASKF_TIMEFROMSYSTEM` 被拒绝，Click 没有进入发送层；Canvas 记录的成功发送来自非接触 `WM_POINTERENTER`，不能替代书写接触或实体震感验证。本轮只修开关生效时机，震动逻辑保持现状；当前任务继续进行，不提交。

本轮验证：`Scripts/i18n.ps1 check` 与 `git -c diff.ignoreSubmodules=all diff --check` 通过；`InkeysRepo.sln Debug|ARM64` 的增量构建和完整 Rebuild 均在多个无关源码/标准库头文件报 MSVC `C1902 Program database manager mismatch`，属于本机构建链故障，未改项目配置绕过。Rebuild 清除了旧的 headless exe 且未能重新生成，因此本轮无法执行 `--no-window`；前一轮通过记录不代表本轮已编译验证。

## 当前定向日志实施（2026-10-11）
- [x] Bar 原始 Pointer/Touch 字段与匹配失败阶段可在单次笔按钮操作中对照；不放宽身份校验。
- [x] Canvas RTS 接触、Pointer 入站/拒绝、绘制线程重置/绑定/停止可按接触时序对照；不改变输入或光标。
- [x] 后端实际发送 HRESULT、波形、控制器身份可判读；不把 API 成功写成实体震感成功。
- [x] diff/编码/i18n、完整 ARM64 solution 构建及无窗口测试通过；C1902 已定位为沙箱限制并通过同配置沙箱外验证。
- [x] 保持任务 in_progress、不提交、不启动 GUI；等待用户下一份实体设备日志后再定修复策略。

2026-10-20 恢复验证：当前 PowerShell 删除 `Env:PATH` 会删除整个搜索路径，需先保存原值并恢复为单一 `Path`，既避免重复键也保留 vcpkg 子进程的搜索环境。沙箱内独立的一行 C++ `/ZI /FS /c` 探针同样报 `C1902`，相同命令在沙箱外退出 0，确认是 runner/PDB 运行限制；不修改源码、SDK 或项目配置绕过。后续完整构建按同一原有 solution 配置在沙箱外验证。

2026-10-20 独立 trellis-check 已通过本轮六项定向诊断复核，含 Pointer Update 拒绝可见性、RTS 身份、Bar Up/Move 失效、外层产品锁外输出、真实发送 HRESULT/最终缓存波形、重复失败及多 Pointer 满槽限频。缓存满时不轮换，省略 Update 的饱和计数附于下一条边沿/状态日志；Pen 保留槽位。诊断最长 detail 保守估算低于 384 字节，原准入/匹配/发送/重试/光标动作不变，原 BOM/CRLF 保持。

本轮最终验证（2026-10-20）：ARM64 原生 MSBuild 构建 `InkeysRepo.sln Debug|ARM64 /p:LinkIncremental=false` 退出 0，耗时 5分52秒、0 error；既有第三方/SDK 等 warning 未作范围外处理。相同产物 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 退出 0。`git diff HEAD --check`、11份定向源码 UTF-8/BOM/全CRLF 对照、`Scripts/i18n.ps1 check` 均通过。构建日志为 `.trellis/.runtime/restore-pen-haptics/diagnostics-build-20261020-verified.log`，测试日志为同目录 `diagnostics-headless-20261020.log`。没有启动 GUI、提交或归档；实体震动问题等待此诊断版的设备日志继续定位。

建议设备取证顺序：确认“测试触控反馈”已开启并重启，保留从启动开始的完整控制台输出；先在普通笔模式悬停后绘制一笔并抬起，再用笔点击类型按钮、选择荧光笔并再绘制一笔。Bar 的 candidates 按 recent/source/time/point 顺序报告严格条件下的候选数；以 `rts_contact`、`canvas_reset`、`canvas_ingress`、`canvas_pointer` 和 `send` 对照接触与设备请求，不能把 `hr=0` 当作实体震感已验证。

## 2026-10-20 实测修复执行（用户已批准）
- [x] 分离画布范围身份与 RTS 笔划生命周期；被动 cursor 核对，修复 Enter 后没有 Pointer Down/Update 的多笔路径。
- [x] 修正 Bar Down/Up 提供方时间匹配，保留来源、唯一性、单调时钟有效期和取消去重。
- [x] 删除失效的 require-contact 等前提及旧测试断言，增加真实事件顺序回归与负例。
- [x] 独立检查输入/光标无副作用、身份失效、跨线程和可选平台回退；更新 spec。
- [x] 完整 Debug|ARM64 solution 构建、--no-window 测试、diff 和原始编码/换行检查。
- [x] 保持任务 in_progress，不 commit/push、不归档，说明实体设备待验收。

2026-10-20 修复实现与独立检查：CanvasPointerMailbox 以动态 Pointer tablet cursor 核对 RTS 来源，并用 tablet context/contact/generation 与 range revision 保护终态所有权；普通 Up 只停止当前笔，下一笔可重新排入有效范围，Controller 恢复已验证活动笔划的波形 Tick。删除 requireContact/ContinuousCandidateAllowed 旧前提。Reset 对空状态幂等，拒绝 Pointer 并失效范围时也唤醒 owner。Bar 移除 Down/Up 的 TIMEFROMSYSTEM 一票否决，保留提供方时间精确关联和本进程单调时钟有效期。
独立 trellis-check 无剩余阻断项；用例覆盖实测 Enter 后三笔 RTS、两种到达顺序、来源冲突/多笔/代际/迟到终态、空门禁代际稳定以及真实 mask=0x4 点击链路。自动用例覆盖生产共享 mailbox 与 ABI fake 后端，不等于完整 GUI 事件循环或实体震感验收。7 份本轮代码/测试文件严格 UTF-8、原 BOM、CRLF 检查及 git diff HEAD --check 通过；本轮基线保存在 runtime/restore-pen-haptics/repair-baseline，保留既有未提交格式及用户修改。

首次修复构建（repair-build-20261020.log）退出 0、0 errors、118 warnings，耗时 1分15秒；相应无窗口测试退出 0。最终复核进一步移除同范围 RTS Down 的不必要 Detach/设备工厂重查：SameCanvasBinding 在生产与测试共用 pointer/cursor/range revision 校验，首次落笔延续悬停预设，Up 后下一笔只重发，不重复 Attach。独立复核通过；此最终修改另行重新构建和测试。

最终修复验证（2026-10-20）：ARM64 原生 MSBuild 构建 InkeysRepo.sln Debug|ARM64 /p:LinkIncremental=false 退出 0，0 errors、118 warnings，耗时 57秒；最终 InkeysHeadlessTests.exe --no-window 退出 0，包含同范围控制器复用与提供方时间关联的新增回归。日志为 repair-build-20261020-verified.log 与 repair-headless-20261020-verified.log，位于 .trellis/.runtime/restore-pen-haptics/。7份修改源码/测试 UTF-8/BOM/CRLF 和 git diff HEAD --check 通过；dumpbin /imports 确认 GetPointerCursorId 与可选 WinRT 入口无静态导入。使用既有已证实可用的沙箱外构建/无窗口测试方式，无环境绕过源码修改。
可交付产物：Build/ARM64/Debug/Inkeys.exe（2026-10-20 08:41:42）。独立检查包含最终 SameCanvasBinding 复用修改，通过。尚未执行实体设备/GUI/真实 Win7 验收；建议同一次进入画布连续写三笔、用笔切换荧光笔/橡皮后再写，并核对鼠标/手指不产生笔 Click。诊断继续重启生效，canvas_identity 可看到 cursor、recognition、matched/reason；按钮应由 matched token 到 click_consume/send discrete。任务维持 in_progress，本轮无暂存、commit、push 或归档。

## 2026-10-20 人工验收与提交检查
用户确认 Surface 超轻薄触控笔 2 人工测试通过，并授权本轮提交，明确要求不要结束任务。复核本次附件 f05e1928-8355-4e66-8c3a-5166e7261164：18 对 RTS Down/Up，18 次 canvas_identity 均 matched=1；61 次 click_consume 均 attached=1/current=1/sent=1，对应 61 次 Click 发送；32 次连续发送包含 27 次 Ink 和 5 次 Eraser，发送 HRESULT 均为 0。同一 Pointer 范围内连续多笔能在 Up 停止后重新发送，不再因等待缺席的 Pointer Down/Update 丢失反馈。日志本身没有 Highlighter 实际发送记录，不单独据此声称该波形已验证；实体震感通过以用户反馈为依据。
本轮未修改触觉实现，复用最终 repair-build-20261020-verified.log 与 repair-headless-20261020-verified.log 的构建/无窗口验证；补充提交差异及格式检查。提交包含诊断、已批准修复、测试与任务记录；保留工作区中无关的 IDT_RELEASE 开关修改和 DrawingController 格式改动。Trellis 状态保持 in_progress，不执行 finish/archive/push。

## 2026-10-20 后续实施：设置与全按钮反馈
- [x] 获取 origin/dev 26dbe340，解决输入/主栏/设置/诊断冲突并完成独立静态复核，合并提交 347ea174。
- [x] 备份并三方核对原有本地改动：dev 已包含 IDT_RELEASE 注释和对应格式调整，恢复后工作区无额外差异；保留 stash efd0a149 及 runtime 备份。
- [x] 添加新配置、绘制设置卡片与三语文案，独立实时开关及旧请求失效。
- [x] 降低按钮反馈强度，覆盖主栏/More/形状/绘制属性/橡皮/撤销重做属性的业务接受点。
- [x] 增加能力回退、开关代际、全按钮共享接受边界回归；独立全范围复核。
- [x] 完整 Debug|ARM64 solution、--no-window、i18n check、diff/编码检查；不启动 GUI。
- [x] 更新按钮/形状新增接入合同，记录人工验收限制；不提交新增功能、不结束任务。

实现与复核：新增 Drawing.Haptics.Drawing / Buttons（默认 true），设置→绘制卡片持久化并立即通知 Host；关闭绘制反馈由 owner 停止发送但保留有效范围，按钮独立启停且清除旧代际请求。按钮统一单次 Click、请求强度 0.35，能力不足沿既有路径回退，发送失败不增加力度重试。主栏注册回调、More、形状、颜色/粗细快捷及笔/橡皮/撤销重做属性共享业务接受边界；拖动、取消、禁用/拒绝动作、鼠标/触摸和重复回调不发送笔确认。复核补齐 Freeze 不可用或状态未改变时的拒绝结果。画布 Leave/模式变化仅失效画布绑定，不截断合法按钮确认；保持 dev 的输入、光标、快捷键和窗口行为。

最终验证（2026-10-20）：原生 ARM64 MSBuild 构建 InkeysRepo.sln Debug|ARM64 /p:LinkIncremental=false 退出 0，耗时 2分18秒，0 errors、166 warnings（既有第三方/工具链警告未作范围外调整）。首轮仅新增 fake 测试误调用私有 helper，已改为通过真实 AttachPointerId 入口绑定，再构建通过。InkeysHeadlessTests.exe --no-window、Inkeys.exe --config-boundary-test、Inkeys.exe --state-mode-production-test 均退出 0；后两者在产品初始化前进入独立无窗口测试分支。配置测试覆盖默认/缺省字段和四种开关组合的实际写入读回；状态测试含 Freeze 被拒绝断言。i18n check（en-US/zh-TW 各 408/408）、git diff --check、32份差异文件 UTF-8/BOM/EOL 检查通过。dumpbin /imports 确认可选 GetPointerCursorId / WinRT 入口没有静态依赖。

证据位于 .trellis/.runtime/restore-pen-haptics/：preferences-build-20261020-verified.log、preferences-headless-20261020.log、preferences-config-20261020.out.log、preferences-state-20261020.out.log、preferences-imports-20261020.log。产物 Build/ARM64/Debug/Inkeys.exe（2026-10-20 09:14:45）。本轮未启动 GUI；新按钮覆盖、开关组合和 0.35 强度的实体笔体感待人工验收，先前人工通过不替代本轮验收。pen-haptics.md 与 rendering-and-ui.md 已建立未来按钮/形状扩展接入关系。任务保持 in_progress；仅 dev 合并提交 347ea174，新增功能未提交、未 push、未 finish/archive。

2026-10-20 体感调参：用户反馈 0.35 偏弱，按要求将统一按钮 Click 请求强度改为 0.75。现有后端回归使用 kButtonHapticIntensity，自动覆盖新值，无需复制数值或修改测试逻辑。书写强度、按钮接线及能力回退保持原样；任务仍进行中，不提交。
0.75 调参验证：完整 InkeysRepo.sln Debug|ARM64 构建退出 0，0 errors、16 warnings，耗时 1分15秒；日志 intensity075-build.log。diff 与 UTF-8/BOM/EOL 检查通过。本轮仅统一常量与文档变更，未重复执行上一轮已通过的完整无窗口测试，未启动 GUI，实体体感待用户复测。

## 2026-10-20 按钮反馈间歇丢失修复
用户日志 98eb9ecc 显示多数 Pointer/Touch Down/Up 已精确匹配，却在 bar_click 接受入口被拒绝；成功请求均以 intensity=0.750、hr=0 发送。优先核查新增 ButtonMotionGate 使用系统鼠标拖动像素阈值误判笔点击微移的问题，使触觉资格遵从实际控件的点击/拖动业务判断，并保留来源匹配、取消和去重。补充真实日志位移回归、独立检查、完整构建和无窗口测试；不修改书写反馈、不提交、不结束任务。
实现与独立复核：去掉普通按钮的全局 ButtonMotionGate，仅连续选色板保留局部轻点判定；颜色快捷按钮按跨色块业务动作锁存拖选，返回原色也不反馈。真实 Pointer Up/同一接触/有效期/取消/去重条件不变，反馈仍为 0.75。新增实测微移的共享接受回归；bar_click 细分缺凭据/非 Up/接触不符，bar_invalidate 在锁外记录有效凭据首次失效边沿，重复失效静默。独立检查确认主栏 Seek、粗细连续调整、选色板和取消边界无新的反馈回归。
完整 InkeysRepo.sln Debug|ARM64 构建退出 0，0 errors、7 warnings，耗时41秒；日志 button-jitter-build.log。32份差异文件 UTF-8/BOM/EOL 和 git diff --check 通过。
最终无窗口验证：InkeysHeadlessTests.exe --no-window 退出 0（button-jitter-headless.log），包括本轮接受策略回归。产物 Build/ARM64/Debug/Inkeys.exe（2026-10-20 12:50:50）。未启动 GUI，实体连续点击与微移体感待用户复测；任务保持 in_progress，未 commit/push/finish/archive。

最终验收：用户确认按钮间歇丢失修复生效，要求最终按钮强度调整为 0.5，明确不再编译和测试，并授权提交及结束任务。源码及当前规范已同步 0.5；现有可执行产物仍为上轮 0.75，最终常量未重编译。此前构建、回归和人工结果见上文。
