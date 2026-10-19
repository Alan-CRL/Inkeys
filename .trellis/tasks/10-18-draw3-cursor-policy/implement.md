# 实施计划与执行记录

2026-10-19 最新修订以 research/cursor-policy-revision.md 和 PRD 当前需求为准：鼠标默认系统，仅软/硬笔可换笔形；新增笔圆形/笔形（默认圆形）；高亮固定按输入来源，不提供开关；荧光笔独立。以下前次设计/验收保留为历史，末节记录此次修订。

## 实施门

- [x] 用户明确授权创建 Trellis 任务并开始调查规划。
- [x] 创建 `10-18-draw3-cursor-policy` 并记录 R1-R6。
- [x] 2026-10-19 确认鼠标画笔/荧光笔默认、半透明/高亮边缘，以及按 Mouse/ScreenPen/Digitizer 分别保存（默认 true/false/true）。
- [x] 用户确认系统触摸光标始终隐藏，无开关、不新增隐藏配置、不继承旧 Beta；优先复核现有隐藏效果与 Win7。
- [x] 收敛 Drawing.Cursor 四字段、三个独立默认及 Unknown/Conflict→Digitizer 视觉回退。
- [x] 用户授权实现；先补已确认的固定反馈/终态刷新缺口和实际 HCURSOR 诊断，未获得现场证据不修改未知 Mouse 来源过滤。
- [ ] 保存并核对 Win7 设备/程序身份及可判别时间线，完成硬件验收。
- [x] 完成 PRD 收敛、design/implement 与 context manifests 检查。
- [x] 用户明确批准开始实现，已运行 `task.py start`。

本轮可修改产品代码并进行命令行构建/无窗口测试。窗口 harness、GUI/Computer Use 和 commit/push 没有授权，不执行。

## M1：Win7 证据与可重放边界（R1、R2、R3）

开始 M1/M2 前读取 native-desktop/input-and-ink.md 的 Touch 归属、schema=2 与 Win7 shader 专题；开始 M3 前读取 draw3-shaders.md 和 draw3-integration.md 的工具光标合同；开始 M4 前读取 configuration-i18n-and-assets.md 及 rendering-and-ui.md 的 Setting 同步专题。长 spec 可能受注入上限截断，必须主动读取相应段落，不能仅凭 manifest 存在视为已读。按阶段读取同任务对应 research 完整报告；不把历史行号套到当前源码。

1. 记录 Win7 版本/位数、补丁与驱动、触摸/笔设备、程序版本与 EXE 身份、输入后端、当前工具/交互模式及可选光标样式/高亮值；隐藏策略固定启用，无启用/关闭两套产品场景。不要推定历史日志属于本次程序。
2. 使用既有 Cursor 诊断采集启动至结束全量 stdout/stderr；覆盖单指、多指、最后 Up、Pen Up/OutOfRange、取消、纯触摸与真实 Mouse/Pen 接管。保留 dropped、截断及失败，不使用末屏作为完整证据。
3. 以 source=primary/touch、系统 hide/apply/skip 和实际可见小图标区分生产者。若缺口确实在 HCURSOR 状态或命中 HWND，再增加最小、限频、非阻塞诊断；不改日志 worker 的有界合同。
4. 用被证实的事件序列补 `InkeysHeadlessTests/draw3_contact_tests.cpp` 或相邻既有测试，包含 Win7 来源 API 缺失、未知按键态 Move、末点偏差、迟到消息、真实鼠标恢复及 Pen 终态。测试须调用生产过滤入口/解析器，不能仅复制一份实现。
5. 若需要新增可信物理鼠标证据，先评审现有 Raw Input 注册与绘制期间的接收状态；不新增第二套 producer，不用“没有 raw 日志”证明没有鼠标。

**验收门**：能明确异常属于错误 primary、系统 HCURSOR、Tablet feedback 或其他层；证据不足时本阶段只能称诊断完成，不能称修复完成。

## M2：生命周期与触摸反馈修复（R1、R2、R3）

1. 先核实现有隐藏链，按 M1 已证实的缺口最小修改 Draw3.PenCursor.cpp、Draw3.WindowControl.cpp 或 RTS 窄事件通知；已满足要求的路径保留，不为旧开关恢复新增逻辑。保留真实 Mouse 即时接管、Pen hover/倒置、Touch Pan 和所有来源已知分支。
2. 若 M1 证实 Win7 未归属 Move 错误接管，只收紧对应条件；不得统一拒绝未知消息、依赖固定延时或把按键位直接视为真实鼠标证明。
3. 如证据确认系统反馈未被现有路径隐藏，在 HWND 所属线程于首触前建立固定启用的 tablet 属性/gesture flags，验证 TABLET_DISABLE_TOUCHUIFORCEOFF 候选并保留多点与原手势控制；覆盖附着、重建与脱离，不引入隐藏开关。
4. 若 Win8+ 确需额外 feedback API，动态解析并按返回值记录；Win7 缺失时保留独立可用路径，不新增 Win8 静态导入。
5. 核对 Pen Up/Cancel/OutOfRange、Touch Up/Cancel、失焦、捕获丢失、离窗、工具/模式变化：状态和视觉清理、owner-thread refresh 均收敛。不要简单删命中 HWND 保护。

**验收门**：单指/多指与混合输入无额外主光标，Pen/Touch 结束无旧光标回弹，真实鼠标可立即恢复；Win7 反馈零闪须有真机记录，逻辑测试不能替代。

## M3：鼠标预览与笔形（R4、R5，依赖共同策略稳定）

1. 打通产品偏好到现有 Mouse 光标路径，按工具保持用户确认默认：画笔为鼠标笔形，荧光笔为原屏幕笔预览并默认高亮边缘。原版/新式选择仍按工具解析；Eraser/Laser 不受普通工具样式误影响。
2. 荧光笔复用真实工具宽度、alpha 和 Rectangle；覆盖悬停、按下、静止调宽、工具切换、显示器/DPI 更新。边缘高亮默认开启并可关闭，实际墨迹的 alpha/粗细不变。
3. 在 design 的两个候选中选定笔形呈现方式。若用 transient shape，仅增最小 PenIcon 几何/着色与热点偏移；同步 bounds/equivalence/dirty/shader。若用 HCURSOR，明确每 DPI 缓存、资源销毁和 owner-thread 设置/恢复。
   shader 方案须同时更新正式 HLSL/资源和 Win7 WARP guarded PS 字节码，沿既有生成办法重建并核对；不改变 OS/driver 选择和诊断 override。现有 enum→shader 的 fallback 不能让新笔形误画成橡皮。HCURSOR 方案须先协调现有“不自建 HCURSOR”的历史合同。
4. 笔尖左上、笔尾右下，热点与实际墨迹点一致；不得通过移动输入坐标补图形偏移。
5. 高亮按 Mouse/ScreenPen/Digitizer 分别应用，默认 true/false/true。核对样本 InputSource 到视觉场景的映射；不修改真实来源分类或橡皮响应。Unknown/Conflict 的选择/回退须先写入设计。将边缘强调与工具填充分开，覆盖新增参数的等价判定、上传、dirty 和两套 PS；不连带改变 Pen Contact 隐藏或实际荧光笔透明度。
6. 新光标只作为瞬态视觉，覆盖其离开/关闭/穿透后的旧脏区清除，验证保存/撤销不含光标。

**验收门**：光标形状与热点、荧光笔宽度预览及原版切换有可验证结果；不以已有圆形 Mouse cursor 代替笔形交付。

## M4：可选样式设置、启动恢复与兼容（R3、R6）

1. 在已批准配置归属中新增最小字段、默认与合法值归一化；三种输入场景的高亮偏好各自有独立存储，默认 true/false/true，避免单个全局 bool 和双份配置真值。
   若光标偏好走 ProductState，完整覆盖业务发布、Product 逐字段 merge、Bridge 等价/revision 与 Host 消费；测试“仅偏好变化、工具与位置不变”仍即时生效。
2. 保持旧 HideTouchPointerBeta 的忽略/退役清理，检查 true/false/缺失/非法值都不能关闭隐藏；不新增隐藏字段、迁移器或对应 UI。只为实际需要的固定策略核验补最小测试，不重写已有清理逻辑。
3. 启动恢复可选样式/高亮到 Host；Setting→绘制增加光标块，只让鼠标样式及三场景高亮即时更新。隐藏触摸光标不显示开关；三语文案说明绘制/穿透和各外观选项。
4. 复用既有设置业务 FIFO、冻结写盘快照和 Sync defer guard；不在 ImGui 锁内等待 HWND，也不每帧落盘。
5. 修改基准 i18n 后运行既有 `Scripts/i18n.ps1 sync`，补齐三语，再 `check`；只审查生成的必要差异，不手改 generated header。
6. 配置测试覆盖首次默认、原版/新式、三场景分别切换及重启、非法值、未知键保留、配置不可读不覆盖；模式测试覆盖绘制→穿透→绘制及设置打开/关闭，设备切换不互相覆盖外观。

**验收门**：设置、运行时和重启后的值一致；退出/穿透不影响下层应用或用户原有鼠标方案。

## 最小验证命令（实际结果另记）

先复核当时工作树、实际构建系统与已有目标；本轮已经确认主入口为 `InkeysRepo.sln`，不新建构建系统或改工程依赖。

```powershell
# 当前已获代码实施与无窗口验证授权。
pwsh -File ./Scripts/i18n.ps1 sync
pwsh -File ./Scripts/i18n.ps1 check

# 从当前 VS 安装定位 ARM64 原生 MSBuild，不写死版本号。
$cursorVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$cursorVsRoot = & $cursorVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$cursorMsbuild = Join-Path $cursorVsRoot 'MSBuild\Current\Bin\arm64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $cursorMsbuild)) { throw '未找到 ARM64 原生 MSBuild；先处理构建环境。' }

# PATH 规范化与 MSBuild 必须处于同一 PowerShell invocation。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $cursorMsbuild .\InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false
$cursorBuildExit = $LASTEXITCODE
if ($cursorBuildExit -ne 0) { throw "Solution build failed: $cursorBuildExit" }

& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
if ($LASTEXITCODE -ne 0) { throw "Headless failed: $LASTEXITCODE" }
git diff --check
```

- 执行前再由 vcxproj 确认测试 EXE 实际输出路径；上述主仓 Build 路径不是本轮产物声明。
- 每次完整构建至少允许 5 分钟，建议显式 600 秒；运行工具在 10-30 秒返回 session 时继续等同一次进程，不反复重启构建。
- 获取退出码与第一个有意义错误。PATH/SDK/PDB/submodule 权限等环境失败单独报告，不改源码、SDK、签名或依赖绕过。
- 如果选定方案修改 shader，复用既有嵌入链与 `10-02-win7-main-canvas` 中的 guarded PS 生成办法，验证正式资源/兼容字节码同步、既有离屏/CPU 几何检查；不会创建 HWND 的检查先行。需要 GPU/窗口 harness 时按其实际能力和授权另列，不能把名称“hidden”当作无窗口。
- 以下历史规划记录中的“未执行”对应当时阶段；当前结果必须另按日期记录，不能覆盖历史或预先把未来命令记为通过。

## 验收矩阵

| 维度 | 必测组合 | 证据要求 |
| --- | --- | --- |
| 设备与后端 | Win7 RTS；Win8+ RTS；Win32Mouse 回退；Pen/Touch/Mouse 混用 | 记录真实设备与所选后端；合成输入不替代硬件 |
| 触摸 | 固定隐藏策略下的首次 Down、单指/多指擦除、最后 Up/Cancel、结束后静止、迟到 Move、重启/HWND 重建 | primary/touch/system 分别收敛；有效触点指示保留且无额外小光标，首次接触及终态无闪现/残留 |
| Pen | 悬停、Down/Up、OutOfRange、笔尾橡皮、取消 | 有效悬停保留，终态残留消失；Win7 独立实测 |
| Mouse | 原版/新式 × 画笔/荧光笔/橡皮/Laser，触摸中与结束后接管 | 只显示一种正确工具指示，不延迟真实鼠标 |
| 模式 | 绘制、穿透、切回、打开设置、失焦/离窗/捕获变化 | 清理旧视觉；其他应用原版光标恢复 |
| 几何与外观 | 最小/大笔宽、多种 DPI、跨屏、负屏坐标、屏幕边缘、亮暗背景、半透明/高亮切换 | 热点与落点对齐，预览宽度准确，高亮体现在边缘，dirty 无裁切或残影 |
| 配置 | 可选样式/高亮的缺失/合法/非法值、AutoClean、重启、三场景分别切换；旧 Beta true/false/缺失/非法 | 高亮/半透明/高亮各自持久化且互不覆盖；旧 Beta 不影响固定隐藏并继续清理，没有新隐藏配置 |
| 场景来源 | Mouse/IntegratedPen/ExternalPen/Unknown/Conflict、Win7 API 缺失 | 使用对应光标偏好；Unknown 不伪造设备身份，视觉回退与真实分类分开 |
| 呈现与保存 | DComp/ULW、停笔/调宽/切工具、保存/undo | 瞬态光标不入持久墨迹；实际可见结果独立于 present 返回值 |

Win7 真机视觉、首次 Down 零闪及硬件混合输入目前均未执行；在获得明确授权或用户现场记录前持续标为待验证，不以本机 ARM64 编译代替。

## 变更范围与回退点

- 预计只触达 Draw3 光标/窗口/Host 与必要 renderer/shader、ProductState/业务同步、Config/Setting/i18n、对应既有测试和本任务/必要 spec。
- 不提前把全部候选文件列为必须修改；M1 定因与笔形方案选择后缩小实际清单，不改工具栏/笔宽算法/RTS 启动回退/图形设备逻辑。
- 每阶段检查编码/BOM/换行与 git diff；不得顺手格式化大文件。
- 阶段回退按 M2（归属/feedback）、M3（鼠标视觉）、M4（设置/schema）分别审查，恢复时同步匹配状态与配置；不丢弃用户其他工作，不使用仓库级 reset。
- 代码完成后按实际改动审查相关 spec，完整记录验证结果和未验收项；不自动 commit/push。

## 历史：规划文档检查（2026-10-18）

- `task.py validate .trellis/tasks/10-18-draw3-cursor-policy`：退出码 0；implement/check 各 6 个真实条目，引用文件存在。input-and-ink.md 大于单文件 32768 字节，工具提示注入会截断；本计划已要求按阶段主动读取 Touch/schema2/Win7 shader 等对应完整章节，不能把截断内容视为读完。
- `task.py current --source`：主会话当前任务为本目录；task.json 状态仍为 planning，没有执行 start。
- JSON/JSONL 解析、重复/空上下文与路径检查、R1-R6 和 AC1-AC10 标识检查通过。按默认文件注入上限估算，当前 manifest 加三份规划文档未超过 131072 字节；后续若增写文档须重新检查预算。
- 当前任务 10 个文件均可按 UTF-8 解码，无单文件混合换行、尾随空白或缺失末尾换行。Trellis 生成的 PRD/task/JSONL 保留 CRLF；新文件使用各自一致的 LF 或 CRLF，未改任何产品文件编码。
- `git diff --ignore-submodules=all --check` 退出码 0；主仓 tracked 与 staged diff 均为空，唯一未跟踪变更为本任务目录。新文档另做逐文件空白检查，避免将 tracked diff 为空误写成已检查未跟踪文档。
- 默认 git diff/status 递归 HiMsg 时因 worktree submodule 元数据访问受限失败；仅以显式 ignore-submodules 检查主仓，未改权限或 submodule，也不声称验证了 submodule 内部状态。
- 未执行编译、i18n sync/check、headless、应用启动、窗口 harness、GUI/Computer Use、Win7 真机或硬件测试；原因是本轮明确限定调查与规划，产品源码无改动。没有代码正确性或 Win7 缺陷修复通过结论。

## 历史：2026-10-19 需求澄清后的文档检查

- 用户确认 D1/D2/D4 已同步到 PRD、设计、执行清单、task.json 和外观澄清研究；新增 AC11 覆盖三场景独立持久化。默认高亮为 Mouse=true、ScreenPen=false、Digitizer=true。
- task.py validate 退出码 0，implement/check 各 6 个有效上下文引用；长 input spec 的截断提示仍按既有主动读取要求处理。manifest 改为优先注入最新澄清报告，M3 仍需读取原完整鼠标能力报告。
- JSON/JSONL、默认值一致性、R1-R6/AC1-AC11、引用路径、UTF-8/原换行和空白检查通过；主仓 git diff --ignore-submodules=all --check 退出码 0，tracked/staged diff 为空，只有本任务目录未跟踪。
- task 仍为 planning；本次继续不改代码、不 start、不构建/测试/GUI、不 commit/push。Win7 和像素表现没有新增验证结论。

## 历史：2026-10-19 隐藏策略收敛后的文档检查

- 用户 D3 已确认：隐藏系统触摸光标固定启用，无用户开关、新持久化字段或旧 Beta 值继承；优先核实现有隐藏效果与 Win7。PRD、设计、M2/M4、配置/API 研究及任务元数据已同步，pending_decisions 为空。
- task.py validate 退出码 0，implement/check 各 6 个有效引用；已核对固定策略元数据、R1-R6/AC1-AC11、JSON/JSONL、引用路径、原编码/换行和任务文档空白。长 input spec 仍须按既有说明主动读取相应章节。
- git diff --ignore-submodules=all --check 退出码 0；tracked/staged diff 为空，主仓唯一变更仍为本任务目录。未改产品或测试代码，未构建、执行测试、启动 GUI 或进行 Win7 真机验证；任务保持 planning。

## 2026-10-19 实施授权与执行状态

用户最新消息已批准开始实现，覆盖本任务代码、必要命令行构建与无窗口测试。此前仅规划的限制为历史阶段；本轮不启动 GUI/窗口 harness/Computer Use，不 commit/push。先完成核心视觉、窗口隐藏/终态、配置/设置接线，再由独立 Trellis check 审查并串行执行完整 Debug|ARM64 Solution 与 Headless --no-window。Unknown/Conflict 的光标场景回退已收敛为 Digitizer 外观，保持真实输入身份不变，设置说明该行为；Win7 真机 T1/T2 不预先记为通过。

## 2026-10-19 已实施范围（本机验证完成，硬件待验收）

- M1：保留未知来源既有接管链，补限频实际系统光标 before/after flags/handle 与命中线程；现场生产者判别仍待 Win7 记录。
- M2：首触前 property/query 同源 FORCEOFF、属性失败/借用值恢复、Touch End/重复 Pen Clear、工具/选择及 SHOW/HIDE 刷新；selection 只交还 Mouse/Unknown，Touch/Pen 隐藏保持。
- M3：显式产品光标设置接入；Pen/HardPen 24 DIP 左上笔尖 PenIcon，Highlighter 真实宽度矩形，边缘 alpha 独立；hotspot/dirty/等价与正式 shader/Win7 guarded 同步。新增 type21 后 DefaultInk 门必须从 >20 改为 >21，避免新光标重新进入 Win7 的问题尾部。
- M4：Drawing.Cursor 四字段、三场景独立默认及严格恢复；ProductState→Product merge→Host owner 单向消费，设置复用 deferred Sync/FIFO 与冻结写盘；三语/i18n 生成和生产 Config/Bridge/resolver 测试已补。
- 图形工具、橡皮/Laser 专用策略、RTS/Win32Mouse 后端、文档层和历史语义保持原范围；不新增触摸隐藏开关/旧 Beta 迁移。
- 独立 Trellis check 已完成配置/UI接线静态阶段，未改源码；最终只读 shader/window 复查和根代理完整 solution/headless 结果另见 research/check-implementation.md 与 research/verification.md。
- 首轮受限 runner 构建 exit1，C1902/PDB 服务环境错误；根据仓库既有同错误对照，在允许 PDB 服务环境中执行同一完整 solution 命令。未改源码或项目配置绕过。

## 2026-10-19 最终本机验证

- 完整 `InkeysRepo.sln Debug|ARM64` 最终退出 0；此前 C1902 环境失败与 C2248 源码修复分别记录，不混为同一问题。
- 新 headless `--no-window` 退出 0，配置测试 0 failures，最终 PASS animation correctness；生产状态早退 CLI 退出 0，两条 StateModeProduction PASS。
- i18n sync/check、独立全范围 check、编码/BOM/EOL、最终 shader/资源数据核验和 diff --check 通过；详细命令、产物 SHA、原始日志位置见 research/verification.md。
- 新 Win7 guarded bytecode已进入最终 EXE，但 Win7 真机和所有 GUI 视觉场景未执行，任务保持 in_progress，不归档。不 commit/push。

## 2026-10-19 光标类型规则修订（本机验证通过，Win7待实测）

依照 research/cursor-policy-revision.md 已完成两类型配置、鼠标/笔两个下拉及三语、软/硬笔类型作用域、独立鼠标荧光笔预览和按设备固定边缘。Host保留原始偏好，Controller按活动runtime实际工具临时投影，系统光标与resolver同步消费；独立check发现的按住切工具缺陷已窄修并复查关闭。原 Win7 隐藏/终态、命中保护、shader/墨迹/历史本轮未再修改。

本轮完整 InkeysRepo.sln Debug|ARM64 最终退出0（首次MSVC增量链接内部崩溃，保留单个.ilk后同配置完整链接通过，未改源码/项目绕过）；headless --no-window退出0、配置0 failures；既有 --state-mode-production-test与--draw3-pen-width-test均退出0。i18n两语各370/370、独立审查、编码/BOM/换行、diff --check和最终PE资源数据核验通过。详情和本轮SHA/日志见 research/verification-cursor-revision.md；前次结果只作历史，不替代本次验证。任务保持in_progress，Win7真机和GUI视觉未验收，不commit/push。

## 2026-10-19 荧光笔边缘与笔形圆角（本机验证通过）

已按research/cursor-geometry-revision.md完成：高亮荧光笔左右0.625 DIP、上下1.25 DIP；笔侧/笔尾四角1.5 DIP内切圆角，尖端保持，均随DPI缩放。产品仅改正式PS与同步guarded数组，type21专属branch隔离圆角；配置/UI/来源/工具投影/墨迹及外框热点保持。

独立check、FXC/签名/尾部guard、完整InkeysRepo.sln Debug|ARM64最终exit0、headless --no-window exit0（配置0 failures，最终PASS）与最终PE资源/实际PS反汇编核验通过。首次MSVC增量链接内部错误保留单个.ilk后同配置完整链接成功，未改项目绕过。guarded现为36380 bytes / F61BFB9DE4A86157037588D3DEEB62185318BA8AEDD83C20A625EC40EFC09327。详细身份、日志、参考与实际编译选项差异见research/verification-cursor-geometry.md。旧验证仅历史；Win7和GUI视觉未验收，任务仍in_progress，未commit/push。

## 2026-10-19 放大笔形圆角（本机编译核验通过）

半径1.5→3 DIP，切线保护1/4→2/5，普通24 DIP四角实际均达到3 DIP，尖端与间隔保持。正式PS仅2常量和2注释变化并同步Win7兼容；独立check、FXC、完整Debug|ARM64构建exit0、最终EXE资源核验与格式检查通过。纯shader参数未重复不含该数学的headless/CLI/i18n。新guarded36380B/F216A0BEAF1C552A91013174795DEA65DF3E6E4BC7EC8E1B43BA31DCC1B91094。详细证据见research/verification-cursor-radius.md；GUI/Win7视觉仍未验收，不commit/push。
