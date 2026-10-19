# 光标策略设计与实施合同

2026-10-19 最新修订以 research/cursor-policy-revision.md 和 PRD 当前需求为准：鼠标默认系统，仅软/硬笔可换笔形；新增笔圆形/笔形（默认圆形）；高亮固定按输入来源，不提供开关；荧光笔独立。以下前次设计/验收保留为历史，末节记录此次修订。

## 1. 范围与组织

一个任务覆盖用户 R1-R6。三组工作（Win7 生命周期与触摸隐藏、鼠标视觉、设置持久化）共享同一套设备归属、工具状态和绘制/穿透边界；本轮按顺序里程碑规划，不另建可以脱离共同光标策略验收的子任务。若现场证明系统 Tablet feedback 与 Draw3 主光标是两个独立缺陷，后续可拆 child，但不得丢失父任务的混合输入验收。

沿用唯一 ProductHost、主 Drawpad HWND、Window Service 所有权和现有 RTS/Win32Mouse 后端选择；不建立第二套输入 producer，不重新启用 Draw2，不改文档、墨迹、撤销或持久层。

## 2. 调查基线证据与尚未定因的部分

详细出处分别保存在 `research/input-cursor-lifecycle.md`、`research/win7-official-api-options.md`、`research/mouse-cursor-capabilities.md` 、`research/settings-and-history.md` 和 `research/cursor-appearance-clarification.md`。

| 观察/源码事实 | 含义 | 当前不能推出的结论 |
| --- | --- | --- |
| Win7 没有当前输入来源查询 API；未知 Move 的回退存在允许接管的分支 | 是触摸期间多出 primary eraser 的优先调查入口 | 不能认定用户小光标必然是这条分支，或全部未知 Move 都是系统注入 |
| 正常 Pen owner、Touch 视觉归属和 Eraser/Laser 工具的系统隐藏策略已经存在 | 需要核对最终 owner、命中 HWND、刷新消息和系统反馈所属层 | 不能简单认定抬笔“忘了清样本”或再次调用 SetCursor 就能修复 |
| 自绘 primary、逐触点 eraser、OS HCURSOR、系统 Tablet feedback 是不同来源 | 必须分别计数/观察，避免修掉有效橡皮指示 | CPU visual=1 或 present success 不等于桌面真实像素 |
| 当前 Tablet flags 未包含 `TABLET_DISABLE_TOUCHUIFORCEOFF` | 可在首次 contact 前验证窗口级触摸反馈控制 | 官方列出 flags 不保证每个 Win7 设备的首次接触均零闪现 |
| `SetWindowFeedbackSetting` 最低 Windows 8；`CURSOR_SUPPRESSED` 也是 Windows 8 状态 | Win7 要有不依赖这些能力的路径 | 新系统成功不能代表 Win7 自动隐藏与恢复正常 |
| 当前鼠标普通工具默认系统箭头，已有关闭该策略的运行时接口 | 可复用普通 Mouse 自绘和荧光笔预览链 | 一个 bool 不足以表达工具默认、三场景高亮边缘、笔形热点与持久化 |

## 3. 统一行为合同

这里的“绘制”指画布处于可批注模式，包含悬停和按下；否则荧光笔无法在按下前预览粗细。“原版”保留当前用户所选 Windows 鼠标方案，不强制覆盖为应用自己的一张箭头。

| 模式/设备/工具 | 系统光标 | 应用视觉 |
| --- | --- | --- |
| 穿透/计算机交互模式 | 由被命中的桌面/应用按原版策略处理 | 无本画布遗留光标 |
| Mouse + 原版样式 + 画笔/荧光笔 | 原版系统光标 | 不新增普通工具主光标 |
| Mouse + 默认画笔样式 | 系统箭头被替换或隐藏，最终只见一个笔形指示 | 笔尖左上、笔尾右下；热点精确位于笔尖 |
| Mouse + 默认荧光笔样式 | 不叠加箭头 | 原屏幕笔同款粗细预览，默认高亮边缘，可关闭高亮 |
| ScreenPen + 普通工具预览 | 保留既有系统隐藏策略 | 默认半透明，按 ScreenPen 单独保存外观 |
| Digitizer + 普通工具预览 | 保留既有系统隐藏策略 | 默认高亮边缘，按 Digitizer 单独保存外观 |
| Mouse/Pen + 橡皮、Laser | 保留现有专用工具策略 | 不被“原版鼠标样式”误改为箭头，也不修改橡皮尺寸/alpha/Laser 尾迹 |
| Touch 擦除 | 无额外箭头/primary eraser 接管 | 每个活动擦除 contact 一枚指示；终态后清除 |
| 触摸结束后，无新设备输入 | 不恢复旧 Mouse/Pen hover | 无旧光标回弹 |
| 后续真实 Mouse/TouchPad 或有效 Pen hover | 及时按其设备/工具恢复 | 不使用固定等待时间挡住真实接管 |

隐藏系统触摸光标是始终启用的画布行为，无用户开关和隐藏偏好，不受原版/新式或高亮选择影响。保留 R1 所需的有效自绘橡皮；错误 Mouse primary 接管属于输入归属缺陷。用户认为当前似乎已隐藏，因此先复核现有实现及 Win7 效果，只针对已证实的缺口修改，不为了恢复旧选项重建一套隐藏链。

保留 Pen 正常悬停、落笔隐藏、倒置笔尾擦除和逐触点反馈合同；Mouse/ScreenPen/Digitizer 的高亮按各自偏好施加，修改鼠标值不能覆盖另外两个场景。

## 4. Win7 调查与修复路径

### 4.1 先识别第二枚光标的生产者

使用现有 Cursor 有界诊断，关联 `rts-input`、`touch-begin/end`、`mouse-state`、`owner`、`visual source=primary|touch`、`system trigger`、`system-skip` 和 present。记录实际程序版本/EXE 身份、设备与 Windows 版本、绘图模式、工具、后端及时间线。

- 若同帧有 primary 和 touch：追踪允许 Mouse 接管的首条消息，复核 promoted signature、barrier、按键、坐标、API 是否存在/成功。
- 若只有 touch visual 但仍可见小图标：进一步判别 HCURSOR 与 Tablet feedback。后续可按实际缺口补充低频 GetCursorInfo/命中 HWND 观测，不把 Win8 的 suppressed flag 作为 Win7 判据。
- 若隐藏决策正确但箭头仍在：检查 UI 所属线程是否实际应用、posted-refresh 是否到达、`WindowFromPoint` 命中哪个窗口、Window Service 转发/捕获/失焦路径和类光标是否恢复。保留防止修改其他应用光标的命中约束。

本轮没有该次复现日志，因此不选择一个未经证实的过滤补丁。既有 Bar Raw Input 接收可能在绘制期间关闭，缺失 raw 事件不等于没有物理鼠标；若以后需要补充可信来源证据，先核对注册唯一性、生命周期与 Window Service 所有权，不能另装全局 hook 或第二个输入 producer。

### 4.2 不牺牲真实鼠标接管

Win7 的未知 WM_MOUSEMOVE 在某些场景与物理鼠标不可区分。禁止直接把“未知来源”统一当成触摸，也不通过固定延迟、所有按键消息拒收、长期保留 Touch owner 来掩盖问题。

后续实现应只修正被现场序列或可重放用例证明的入口。若现有信息不足以同时保证无重复和即时真实 Mouse 恢复，先补充窄观测或可信来源证据，并在改产品行为前更新设计，不暗中改变兼容合同。

### 4.3 在接触前配置反馈，而非在 RTS 回调里补救

先复核当前隐藏效果；若独立系统触摸反馈确有缺口，优先验证已有 `MICROSOFT_TABLETPENSERVICE_PROPERTY` 与 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 的同源 flags 增加 `TABLET_DISABLE_TOUCHUIFORCEOFF` 是否有效。该候选为固定启用策略，不增加运行期开关。保留 `TABLET_ENABLE_MULTITOUCHDATA` 和已禁用的长按/轻拂等设置，不同时强制 ON 与 OFF。

在首次 contact 前设置完整窗口属性，由 HWND 所属线程应用固定策略；重建 HWND 时重放，脱离时只撤销本对象拥有的设置。检查 SetProp 的实际返回值；失败属于反馈能力未建立，不应继续显示“已隐藏成功”。

Win8+ 可动态查找 `SetWindowFeedbackSetting` 按类别补充关闭触摸接触/轻点等反馈。Win7 API 缺失不构成启动错误，不静态导入提高最低系统；任何类别的支持与恢复都按返回值判断。此候选必须与手势识别、系统 HCURSOR 和自绘橡皮分别验证。

`SetCursor(NULL)` 只控制 HCURSOR。继续通过既有 owner-thread 的 WM_SETCURSOR/刷新消息应用，不在 RTS 同步回调中跨线程直接调用，也不改用户全局 Tablet 设置或使用 ShowCursor 计数循环。

## 5. 鼠标视觉的最小扩展

### 荧光笔

默认复用原屏幕笔的 Rectangle 粗细预览、真实 widthDip→canvas px 换算和工具 alpha，鼠标场景默认增加高亮边缘；关闭高亮后使用对应半透明风格。光标选项只改变视觉，不改变绘制路径或 brush 参数。预览和实际绘制必须消费同一份宽度与显示上下文，不另按设置窗口缩放计算。DPI/显示器变化、工具和宽度变化均要使静止光标更新，并清理旧 bounds。

### 笔形

现有形状仅 Circle/Rectangle/EraserGripCircle，不能把圆点称为已完成笔形。候选 A 为在当前 transient 光标增加一个局部 PenIcon 形状，并定义笔尖热点与非居中 bounds；候选 B 为 UI owner 使用自定义 HCURSOR，荧光笔仍走现有宽度预览。

优先评估 A 以复用统一呈现、失效和工具切换；若鼠标跟随延迟达不到要求，再用 B 做有针对性的比较。B 要额外处理每 DPI 的图像缓存、HCURSOR/HBITMAP 生命周期、全透明背景和退出/穿透恢复；本轮不创建图像资产或选定渲染方案。

对 A，采样点/墨迹点坐标不变，笔身向右下延展；图形的尺寸与热点偏移只作用于视觉。更新 bounds、等价判定、dirty union 和 shader 描述，覆盖屏幕左上边缘、负屏坐标、缩放和笔尖落点。当前 renderer 的枚举映射末分支会落到橡皮，新 shape 需显式映射，不能只追加 enum。瞬态视觉不写入 L0/L1/L2、文档、undo 或保存的画布像素。

Win7 WARP 默认使用 `Draw3.Win7GuardedShader.h` 的完整 PS 字节码（`Draw3.Renderer.cpp:699-719`、`Draw3.ShaderCompatibility.h:9-17`），正式 HLSL 与该兼容体必须同步生成/核对；只改正式资源会使新笔形在两条路径上不一致。保留现有 driver/OS 选择和诊断精确 override，不修改兼容策略来掩盖视觉缺失。

历史 native runtime 规范有不自建 HCURSOR、避免逐次粗细变化重建资源的约束（`.trellis/spec/native/runtime-and-rendering.md:340,353`）；B 不是已获批准的捷径，采用前需明确调整的合同与必要理由。当前建议优先沿既有 transient 呈现验证 A。

### 外观

2026-10-19 用户已确认：外观意图是半透明与高亮边缘，高亮可选择，按 Mouse/ScreenPen/Digitizer 分别保存，默认依次为高亮、半透明、高亮。原先查找独立 RGB 亮暗 UI 的未决项关闭；保留历史调查用于解释当前实现差距。

当前 shader 的 outlineAlpha 和 fillAlpha 都乘整体 opacity；旧 translucent 开关只处理特定 Pen 填充。后续需把边缘强调与光标填充分开解析，保留实际荧光笔 alpha/粗细，不把整个工具改成不透明来增强边缘。高亮开关进入 appearance 等价判定、CPU/GPU 上传和静止重绘；如扩展 shader，正式与 Win7 guarded 同步。半透明/高亮切换保持工具形状和热点不变，不扩展 Pen Contact 显隐。

优先复用样本中的只读 InputSource，IntegratedPen/ExternalPen 分别映射 ScreenPen/Digitizer，三份配置独立。现有 ClassifySource 对 Win7 无现代 API 或冲突返回 Unknown；不能为了选光标而改变真实设备分类或套用橡皮 PenResponseChoice。Unknown/Conflict 需要明确的视觉场景选择/回退表，属于后续实施前的兼容设计项。

## 6. 设置与持久化候选

在现有 Setting→绘制页新增“光标”组，保留鼠标原版/新式选择；隐藏系统触摸光标始终启用，不增加对应控件。外观按鼠标、屏幕笔、数位板展示各自高亮选项（可分行或按场景选择编辑），默认值已由用户确认。鼠标默认映射为画笔笔形、荧光笔原屏幕笔预览加高亮，不能把全部普通工具解释成同一张图形。穿透恢复原版是模式行为，不额外增加容易冲突的开关。

建议使用新配置 `Drawing.Cursor`，仅保存可选的鼠标样式和 Mouse/ScreenPen/Digitizer 各自 HighlightEdge（默认 true/false/true）。隐藏策略属于固定运行时合同，不新增 HideSystemTouchFeedback 或等价持久化字段。旧 Beta 不迁移、不恢复；可选样式字段名称和布局继续按工程事实收敛。配置归属建议来自现有 Drawing.Eraser/触摸设置消费者，不代表迁移所有 SetListStruct 字段。

数据流维持单一真值：配置读入/设置变更 → 冻结光标偏好 → 既有业务/Host 同步 → 窗口线程系统光标与反馈、绘制线程瞬态视觉。两边消费同一版本的工具/模式/偏好，不在 RTS callback 或 render loop 读取 JSON。

生产实际类型是 `HostStartOptions` 与 `StrokeModelConfiguration`，没有名为 HostConfiguration 的类型；现有 cursor setters 尚未从产品设置接通。建议优先把可随工具/模式一起消费的 cursor preferences 纳入 ProductState；若选此路，必须同步 `IdtState::PublishDraw3State`、`Draw3.Product.cpp:90-105` 的逐字段 merge、Bridge 等价/revision 和 Host::PumpBridgeState，防止只填结构字段却在 merge 时丢失。独立 Host 期望快照也是既有可参照模式，但两条路线应择一，不双写 Controller/Window 状态。

设置持 ImGui 锁时不得同步等待 HWND；可选样式/高亮复用既有 defer/FIFO 或非阻塞 Host 入口，写盘复用已冻结的 QueueConfigWrite。旧 HideTouchPointerBeta 继续退役清理，其 true/false/缺失/非法值都不影响固定隐藏；无需迁移器或双写。现有退役键规范与最新决定一致，不为光标任务复活旧配置。

## 7. 兼容与回退

- 保持现有 RTS Extended/BasicXY/Win32Mouse 的单 producer 选择。无 RTS 时，Mouse 后端的工具/穿透/样式也需按同一规则工作。
- 保持 DComp 与 ULW，Win7 的 ULW 验收独立记录；CPU visual/present 成功不能代替实际可见结果。
- 非法配置按审定默认安全归一化；坏/不可读配置不被一份默认值覆盖。
- 用户可选原版鼠标样式回退；触摸重复归属修复不能被该回退关闭。
- 若新增 feedback API/flag 在某设备上失败，保留现有输入和窗口运行，明确记录失败并保留该设备未验收状态；不能以“回退正常”代替零闪验收。
- 本任务不改全局光标方案、系统设置、工具链、依赖或 build 配置；所有改动保持最小 diff。
- `draw3-integration.md:313,338` 的荧光笔固定 `6.25 × 50 px` 描述已落后于生产真实宽度；本任务遵守当前源码的共享宽度真值，后续实际改动时一并纠正规范对应陈述，不把旧常量恢复到代码。

## 8. 调查阶段收敛清单（现状见末节）

已关闭 D1-D4：默认鼠标画笔笔形、荧光笔现有预览加高亮；半透明/高亮边缘按三场景独立保存，默认 true/false/true；系统触摸光标始终隐藏，无开关且不迁移旧 Beta。

1. 可选样式/高亮的新配置布局继续作为工程设计收敛，候选字段不可当作已实施 schema；隐藏策略没有配置或旧值兼容决策。
2. 本次 Win7 双光标/抬笔残留尚无对应现场序列；先取得判别证据再冻结过滤修复，不承诺所有驱动零闪现。
3. 来源 Unknown/Conflict（含 Win7）时的视觉场景选择/回退需列出明确表格；分别保存不能被写成自动识别已验证。

上述项解决后更新 PRD、设计和实施清单，进行一次最新规划评审。只有用户在后续消息明确批准实施，才运行 task.py start。

## 2026-10-19 实施收敛与最小变更边界

用户已明确批准开始实施。采用既有 transient shader 路径 A，不引入自建 HCURSOR。新增一份窄 CursorSettings 头文件用于传统业务/Bridge 与 module 之间共享设置，Host 在绘制 owner 消费 ProductState。鼠标样式保存在 Drawing.Cursor.MouseStyle（System=0、Drawing=1，默认 Drawing），Mouse/ScreenPen/Digitizer 的 HighlightEdge 独立默认 true/false/true；不增加隐藏开关。IntegratedPen→ScreenPen，ExternalPen→Digitizer，Mouse→Mouse；Unknown/Conflict 仅在视觉上路由 Digitizer，真实身份不变，设置说明该回退。

预计核心变更为 PenCursor/Controller/Renderer/HLSL/Win7 guarded PS；窗口隐藏与终态刷新仅在 WindowControl；设置链为 Other.Config、IdtState、Product/Bridge/Host、Setting/i18n 与必要现有测试。各文件只承担实际数据流/视觉/同步所需修改，不改实际墨迹、画笔算法、工具栏、输入后端选择、全局系统设置或依赖。

Win7 没有现代来源 API 时，不能从未归属 Move 证明是物理鼠标或系统提升。实施只处理明确窗口反馈策略与终态刷新缺口，补足实际 HCURSOR/命中诊断；没有现场序列不盲目收紧来源过滤。真实鼠标接管和物理 Win7 零闪仍需现场验收。

## 2026-10-19 光标类型规则修订（本机验证通过，Win7待实测）

依照 research/cursor-policy-revision.md 已完成两类型配置、鼠标/笔两个下拉及三语、软/硬笔类型作用域、独立鼠标荧光笔预览和按设备固定边缘。Host保留原始偏好，Controller按活动runtime实际工具临时投影，系统光标与resolver同步消费；独立check发现的按住切工具缺陷已窄修并复查关闭。原 Win7 隐藏/终态、命中保护、shader/墨迹/历史本轮未再修改。

本轮完整 InkeysRepo.sln Debug|ARM64 最终退出0（首次MSVC增量链接内部崩溃，保留单个.ilk后同配置完整链接通过，未改源码/项目绕过）；headless --no-window退出0、配置0 failures；既有 --state-mode-production-test与--draw3-pen-width-test均退出0。i18n两语各370/370、独立审查、编码/BOM/换行、diff --check和最终PE资源数据核验通过。详情和本轮SHA/日志见 research/verification-cursor-revision.md；前次结果只作历史，不替代本次验证。任务保持in_progress，Win7真机和GUI视觉未验收，不commit/push。

## 2026-10-19 荧光笔边缘与笔形圆角（本机验证通过）

已按research/cursor-geometry-revision.md完成：高亮荧光笔左右0.625 DIP、上下1.25 DIP；笔侧/笔尾四角1.5 DIP内切圆角，尖端保持，均随DPI缩放。产品仅改正式PS与同步guarded数组，type21专属branch隔离圆角；配置/UI/来源/工具投影/墨迹及外框热点保持。

独立check、FXC/签名/尾部guard、完整InkeysRepo.sln Debug|ARM64最终exit0、headless --no-window exit0（配置0 failures，最终PASS）与最终PE资源/实际PS反汇编核验通过。首次MSVC增量链接内部错误保留单个.ilk后同配置完整链接成功，未改项目绕过。guarded现为36380 bytes / F61BFB9DE4A86157037588D3DEEB62185318BA8AEDD83C20A625EC40EFC09327。详细身份、日志、参考与实际编译选项差异见research/verification-cursor-geometry.md。旧验证仅历史；Win7和GUI视觉未验收，任务仍in_progress，未commit/push。

## 2026-10-19 放大笔形圆角（本机编译核验通过）

半径1.5→3 DIP，切线保护1/4→2/5，普通24 DIP四角实际均达到3 DIP，尖端与间隔保持。正式PS仅2常量和2注释变化并同步Win7兼容；独立check、FXC、完整Debug|ARM64构建exit0、最终EXE资源核验与格式检查通过。纯shader参数未重复不含该数学的headless/CLI/i18n。新guarded36380B/F216A0BEAF1C552A91013174795DEA65DF3E6E4BC7EC8E1B43BA31DCC1B91094。详细证据见research/verification-cursor-radius.md；GUI/Win7视觉仍未验收，不commit/push。

## 2026-10-19 Pen离开后Mouse光标回弹修复（本地验证通过）

当前增量以research/pen-mouse-takeover-regression.md为准。用户确认Win11；补Pen归属清样本后对明确系统注入Move的过滤保护，不通过一秒延时或吞未知来源屏蔽真实Mouse。先生产过滤/Window接线及无窗口回归，独立check后root完整Debug|ARM64和headless验证；不改shader/配置/UI/墨迹，不GUI、不commit/push。

本轮已落地默认Unknown的pointerAuthority及Window持久owner接线，Pen离开清样本后仍拒绝明确系统Move；System/Drawing实际渲染路径已核对。独立审查、完整Debug|ARM64构建exit0和headless --no-window exit0通过，现场消息/设备可见效果仍待复测。验证详见research/verification-pen-mouse-takeover.md。
