# 手掌橡皮：当前代码路径与规划决策

研究日期：2026-11-01。前半代码行号是实现前研究锚点；当前实现及命令行结果以design.md/validation.md为准。未运行GUI。

## 已确认的集成入口

| 主题 | 代码/规范证据 | 对本任务的影响 |
| --- | --- | --- |
| 生产路径 | `.trellis/spec/native-desktop/draw3-integration.md:114`；`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:10951` | 只在产品 Draw3 集成，不修改旧demo的绘制镜像或旧 Draw2；主工程直接编译的共享UInk适配器按R14实际需要处理。 |
| 真实触摸身份与面积 | `Draw3.ContactInput.cppm:19`、`:55`；`Draw3.SpeedEraser.h:135` | ContactSnapshot 已携带输入种类、面积原值、已验证单位及画布像素宽高；只允许真实 Touch 参与手掌识别。 |
| 面积单位解析 | `Draw3.RealtimeStylus.cpp:986`、`:998`；`Draw3.SpeedEraser.h:190` | 复用现有 WIDTH/HEIGHT 元数据与单位转换；单位缺失不能按数值猜像素，DPI 只能回退物理推荐值，不能修复未知输入单位。 |
| 合并采样 | `Draw3.RealtimeStylus.cpp:1392`、`:1408`；`Draw3.ContactInput.cpp:266`、`:775` | RTS Packets 取批次最后一包，coordinator 又只保存最新快照；绘制线程观测到的“窗口最大值”并非原始所有硬件包的最大值。最终设计必须明确采样口径；如需保留 producer 已接收的峰值，应使用有界聚合，不能引入无界逐包队列。 |
| Touch 新笔门禁 | `Draw3.DrawingController.cpp:3581`、`:3597`、`:11470` | 通用多指关闭会拒绝后来的独立 Touch，直到该物理路由终态；不能事后只在 UI 隐藏开关来改变此事实。 |
| 激光多指门禁 | `Draw3.DrawingController.cpp:3608`、`:10972`；`Other.Config.cppm:386` | 激光笔额外受 Laser.MultiTouchEnabled 控制（默认关）。手掌适用工具勾选与多指准入是两个不同概念。 |
| 原工具与有效工具 | `Draw3.DrawingController.cpp:11130`、`:11147`、`:11158`、`:11509` | selectedTool 是同批原始选择；tool 是每触点有效工具，已有笔尾/右键橡皮覆盖。手掌应覆盖本 Runtime.tool，不能将批次或全局工具一起切成橡皮，否则其他输入被污染。 |
| 每触点状态 | `Draw3.DrawingController.cpp:2047`、`:11506` | 可按 contact handle+generation 保存候选窗口、激活、定尺、相邻抑制和锁存偏好；Up/Cancel/页面边界、对象池复用时清理。 |
| 活动层重建 | `Draw3.DrawingController.cpp:4284` | RebuildActiveLayers 会清共享 L1/L0，并从全部剩余活动 CPU 状态重放。移除本触点旧笔迹必须先移除它的原工具 CPU 几何，再重建并合并旧/新脏区，保留其他触点。 |
| 入文档时机 | `Draw3.DrawingController.cpp:16607`、`:16626`、`:16770` | 正常完成笔划才提交 document/history/L2，然后重建余下活动层。不能只擦 GPU 像素却保留旧 CPU 几何，导致 Up、resize 或重放复活。 |
| 激光独立层 | `Draw3.DrawingController.cpp:16425`、`:16615` | Laser 不走普通 L0/L1/L2；若激光工具允许手掌激活，需清理本触点激光层及其附属状态，不能只重建共享层。 |
| 历史并发设计的限制 | `.trellis/tasks/archive/2026-08/draw3-source/active/07-18-rts-multicontact-rendering/design.md` 的 Layer batching；当前 `Draw3.DrawingController.cpp:16957`、`:16995` | 历史设计指出“同批同工具”的合并前提；当前已有有效工具覆盖入口。最新范围仅要求不同位置并行擦写，偶然交叠不承诺真实时序层级；不继续扩展成时间片架构。 |

上述短文件名均以 `Inkeys/Inkeys/Drawing/Draw3/` 为根，Other.Config.cppm 除外，其完整路径为 `Inkeys/Inkeys/Other/Other.Config.cppm`。

## 显示标尺、缺省和设置

- `Draw3.SpeedEraser.h:99` 的 DisplayScale 同时提供像素、DPI换算、物理cm/px、映射及generation；`Draw3.Host.h:425` 发布已应用的 EraserDisplayScaleSnapshot。
- `.trellis/spec/native-desktop/cpp-conventions.md:86`：只有业务 physicalSize.available 与正确输出/输入映射可用时才使用物理尺寸，原始 edid.valid 不足以证明可信；逐点路径不可查 DisplayConfig/注册表。
- `Draw3.SpeedEraser.h:151` 的既有面积辅助限制是 2..96 DIP、长宽比3.5、移动确认50ms，并有离群恢复与上限；这用于笔速橡皮下限，不适合直接拿来识别手掌。共享转换和显示标尺，不共享该分类/增长状态。
- 大屏缺省已有 `ConfigDetail::GetDefaultLargeScreenDevice`（`Other.Config.cppm:255`、`:355`、`Other.Config.cpp:760`），`IdtMain.cpp:2457` 在 Mini/正式配置前冻结；方案建议复用此值，避免另开硬件枚举或误用可变的 PaintDevice。现有自动规则包含触摸设备但尺寸未知的情况，这与“可信物理推荐值”应分开描述。
- 配置建议新增 `main.json` 的 `Drawing.PalmEraser` 组，与 `Drawing.Eraser` 并列；本任务尚未定稿，不创建真实字段。
- `Setting.Pages.h:206` 有 DrawingSubpage 与导航helper；`:218` 有草稿/事件模型。`Setting.Pages.cpp:582` 是笔速详情，`:670` 是标尺 Warning，`:687` 起是复选组与精细控制。
- `Setting.cpp:3418` 的擦除分类当前只有笔速入口，手掌入口紧接其后即可；无需新增根侧栏分类。
- `.trellis/spec/native-desktop/eraser-attributes.md:106` 规定每个绘制子页独立 content child ID、正文过渡、保持侧栏 Draw 高亮及滚动位置；新详情沿用这些合同。
- 三种阈值预览应共享同一阈值解析结果，各自按等面积计算方形/横矩形/竖矩形。图形尺寸使用实际像素，不乘 UI 的 Pixels()，文字与间距继续按设置 UI 正常缩放。设置窗口位于另一台屏幕时的目标/预览像素口径须在 design.md 说清，不能承诺跨屏像素对应同样厘米。

## 当前产品决策（最新答复优先）

- 边擦边写开关只控制手掌橡皮，说明中的“笔速橡皮”为笔误；默认开。
- 关闭时手掌激活将其他绘制正常收笔、保留真实已画内容，活动手掌期间禁止新绘制，旧触点须抬起重落。
- 一次Undo/Redo处理整笔或一次完整擦除。
- 多指关闭时严格单Touch，第二Touch不绘制、不识别手掌、不抢占；首个Touch仍可自行激活。
- 用户最新明确停止真实时间交错覆盖研究与实现。场景主要是两个不同位置的擦除和书写，偶然重叠允许层级差异，但不能崩溃、丢笔划或破坏历史/文件。
- 用户已选抬笔时序，沿用现有默认方案，复用正常整笔完成、文档、Undo/Redo与UInk保存；不增加落笔排序或时间片排序。
- 用户已选可信物理阈值25cm²，倍率按面积25%–400%，重置100%。后续补充并明确选择：EDID失效不禁用，缺省大屏仍默认开；基准改为100×100系统DIP（10000DIP²），Warning提示估算不准确，橡皮实际大小仍来自激活触摸面积。
- 用户新增识别时间窗口设置行，已选择100/150/250/500ms预设、默认150ms，默认项标“（推荐）”；每次Down锁存，新设置不更改当前接触的deadline。
- 用户选择刷子现在即可正常勾选和保存，默认勾上；没有可选择的刷子工具不影响该偏好编辑，不灰显。
- 用户新增成功断触接续继承合同：原绘制继续绘制，原手掌继承同一尺寸继续手掌；成功接续优先于新识别，未匹配时才视为新独立接触。

## 规划收敛结果

无阻塞产品问题，最新要求见[prd.md](../prd.md)，算法、UI与接续设计见[design.md](../design.md)，实施/无窗口验证顺序见[implement.md](../implement.md)。用户已批准并完成本期实现及无窗口验收，结果见validation.md。

## 已停止的历史研究

[时间交错Z序研究](temporal-z-order-and-persistence.md) 与 [可行性及回退评估](temporal-feasibility-and-fallback.md) 仅保留历史证据，已添加停止标记并移出implement/check上下文。本期不新增时间片日志、复合历史节点或私有UInk协议。

## 关闭边擦边写的实现证据

`Draw3.DrawingController.cpp:11768` 的 completeModelUp 接受同一Runtime的末点与cancelled标记，负责最终几何、Shape末点及ended状态；`:16770` 后按精确handle+generation调用DiscardUntilTerminal，保证物理路由终态前不复用。这些可作为“正常收笔但仍按住的触点不得继续画”的基础。具体实现仍需排除断触续接并按手掌激活时刻截取真实输入，不能用取消标记丢弃已画内容或保留预测尾部。当前已取消真实时间交错目标，后续只按用户选择的整笔Z序处理。

## 已收敛的技术建议（实施时验证）

- 初值已收敛：可信25cm²/失效回退10000系统DIP²、面积25%–400%、窗口100/150/250/500ms默认150ms；消费快照的有效最大面积、比例≤3.5、等面积圆形擦除。
- 初始满足面积与比例即激活；窗口结束后不再激活、不再调粗细。无有效面积时保持原工具，不能用 DPI 或压力伪造接触面积。
- 相邻抑制建议按新 Touch Down 锁存到该接触终态，移动离开后不重新升级；同帧多个候选需要确定性仲裁，不能对称互相全部压制或全部激活。具体公式与预约/同批仲裁见design.md。
- 手掌使用独立尺寸，不受笔速增长、面积辅助、Hover连续会话或普通橡皮尺寸覆盖。仅新独立Down重新采样；成功断触接续继承旧实际工具和同一尺寸，不重开窗口。首次物理Up结束识别/定尺，故窗口前Up的手掌也可继承已接受尺寸。
- 定时窗口必须在无移动时也可到期锁尺；既有 EraserIdle 休眠须纳入下一deadline，保持 Move 唤醒协议。

## 验证规划与边界

- 本轮仅规划：检查 task.status、文档内容、Git范围、编码和换行；不以现有测试二进制替新功能验收。
- 后续实现：纯策略测试覆盖识别/比例/时间边界/定尺/相邻仲裁/偏好；生产Controller无窗口回归覆盖自有墨迹撤回、多Runtime、Laser、Up/Cancel、重建与历史；真实Setting控件回放覆盖三语/主题/DPI/倍率预览/显隐和保存。
- 现有入口：`InkeysHeadlessTests/animation_tests.cpp:1730` 的 `--setting-eraser-only`、`:1735` 的 `--no-window`；真实配置验证在 `IdtMain.cpp` 的早期无GUI命令分支。
- 实际编译按用户AGENTS优先：完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，动态定位原生ARM64 MSBuild，同一PowerShell invocation中先去除重复PATH并禁用node reuse，至少5分钟超时。不为验证改构建配置。
- 新功能真实手掌硬件手感、EDID/DPI跨屏与多人触摸运行观感留待用户明确授权GUI测试，不能把无窗口回放称为硬件通过。

## 历史检索

已运行 `trellis mem search '手掌橡皮' --cwd D:/Project/Inkeys/Repo/Inkeys-draw --limit 8`，仅命中当前会话。已查阅显示物理标尺父任务、旧多接触设计与当前spec；没有发现可直接解决上述开关作用范围的既往产品决策。未升级Trellis或改其配置。

## 补充证据：定尺与断触接续

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:2137`的Fixed分支直接用widthEstimator.baseDiameter生成新增点，原realPoints半径不回写；可承接窗口内暂定尺寸，不必新增StrokeWidthMode或复用速度控制器。
- `Draw3.DrawingController.cpp:11962`附近的同位置/状态未变早退要求面积识别先行，否则原地摊掌不会触发。
- `Draw3.DrawingController.cpp:11190`按新Down生成包含selectedTool/实际tool/widthMode的身份；`:11214`评估候选，`:11354`附近模型Update成功后才转交handle。`Draw3.InkPrediction.cppm:333`/`.cpp:850`要求工具/宽度身份一致，所以旧手掌候选需用它原本的Eraser/Fixed有效身份参与既有匹配，不由新面积重新分类。
- 普通Laser/Shape排除位于Controller候选外层入口；已转手掌者的实际工具已是Eraser，需覆盖这个接续接线，不扩大普通Laser/Shape续接。
- 七项工具分类独立于DrawingTool枚举下标；Brush仅保存可编辑偏好，当前不新增Bridge工具。

本节短Draw3.*文件均位于Inkeys/Inkeys/Drawing/Draw3/。上述为当前源码研究，不是新功能已通过生产验证的声明。

## 适用长spec的完整读取要求

为避免单文件注入上限截断，JSONL只注入索引、较短专项与本研究；进入实现/检查时仍须按下列源文件读取相关完整合同，不能把本研究当作规范替代品：

- `.trellis/spec/native-desktop/input-and-ink.md`：真实Touch身份与面积单位、断触接续、临时工具覆盖；不得复制笔速面积辅助的较小跨度上限充作手掌分类。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：main.json/NoUpload、严格类型/宽整数、冻结缺省、三语生成链及设置保存FIFO。
- `.trellis/spec/native-desktop/canvas-freeze-boundaries.md`：F3的idle订阅、真实sequence复查、帧初generation和deadline；设置owner/渲染线程不可同步互等。
- `.trellis/spec/native-desktop/cpp-conventions.md`：模块/传统头边界、中文关键注释、编码/EOL和业务physicalSize可用性。

这里仅导航本任务已经适用的现有合同，不修改全局规范或扩大实现范围。
