# UI3 分层封装与通用 UI3 模块迁移 — 实施计划

> 先读 `prd.md`（需求与决策 D1–D9）、`design.md`（边界与契约）。本文是执行顺序、验证命令与检查点。
> 状态：实施中（2026-11-17）。S0–S5 完成，S6a 进行中。**压缩或重启后先读 §6.0「断点与交接」**，再读 §6.1 与 §7。

## 0. 恢复说明（给压缩或新会话之后的执行者）

- 任务目录：`.trellis/tasks/11-16-ui3-layered-encapsulation/`，父任务 `11-16-inkeys3-midterm-refactor`。
- 分支 `refactoring/drawpad-ui`，规划时 HEAD 为 `7047db94`，工作区没有产品代码改动。
- 用户已于 2026-11-17 批准规划，任务已 `task.py start`（状态 `in_progress`）。会话重启后若 `task.py current` 为空，重新执行 `python ./.trellis/scripts/task.py start .trellis/tasks/11-16-ui3-layered-encapsulation` 即可。
- 四份调研在 `research/`，内容是 2026-11-16 Codex 的只读调查，关键事实已抽查；行号以实施时的文件为准。它们每份都超过 Trellis 上下文注入的 32 KiB 上限，自动注入会被截断——需要某一层的事实时直接用 Read 打开对应文件，不要依赖注入内容。
- 调研简报与脚本留在仓库外的 `%TEMP%\inkeys-codex\ui3\`，可丢失，不依赖。
- 进度记录：每完成一个步骤，在本文件末尾「进度记录」追加一行（步骤、日期、构建与测试结果、基线是否刷新）。

## 1. 执行约束

- **Git**：未经用户在当前任务中明确授权，不提交、不推送。提交信息不带任何 AI 署名。移动文件用 `git mv`。
- **文件格式（D9）**：新建或修改的 `.cpp` / `.cppm` / `.h` 存为 UTF-8 带 BOM + CRLF。改完用首三字节 `EF BB BF` 检查。Git Bash 的 `sed -i` 会破坏 CRLF，不要用它改源文件。`.vcxproj` / `.filters` / `.sln` 保持原格式；`InkeysRepo.sln` 头部的 VS 版本两行不得被改写。
- **最小修改**：只做 `design.md` 范围内的事。发现的其他问题记录到本文件「遗留事项」，不顺手修。
- **不做 GUI 自动化**，不启动主窗口。只跑构建、无窗口测试与命令行测试入口。
- **Codex 协作**（用户已授权本任务使用）：`gpt-6.1-sol` + `model_reasoning_effort=high` + `service_tier=default`（Fast 与 Astra 当前不可用）。实现用 `--sandbox workspace-write`，复审用 `read-only`。Codex 沙箱内不能运行构建和 git，构建与测试一律由主 Agent 执行。同一时间只允许一个写入者；如需并行，按文件划分所有权并在简报中列出禁止触碰的文件。简报必须写明：新引用的符号逐个搜索确认存在；共享代码用 `(std::min)` / `(std::max)`；未列出的视觉行为保持原样，不要自行改进；与设计冲突即停止并报告。
- **冲突即停**：实施中发现设计与代码事实矛盾、或出现清单外的可见变化且不是迁移错误，停止并向用户说明。

## 2. 验证命令

构建（PowerShell，一次调用内完成环境规范化与构建；日志用 `-Encoding utf8`）：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue   # 仅当同时存在 Path 与 PATH 导致 MSB6001 时需要；执行前先保存原值，丢失则恢复
$env:MSBUILDDISABLENODEREUSE = '1'
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m
```

- 构建超时至少 5 分钟；按退出码和第一个有意义的错误判断，不看末尾几行猜测。
- 增量链接报 `LNK1000` 时删除 `Inkeys.ilk` 重链；`LNK1104` 多为 exe 被占用，重试。
- MSBuild 路径以本机实际安装为准，不要写死版本号到脚本以外的地方。

测试（均为命令行、非交互；产物在 `Build/ARM64/Debug/` 下，具体子目录以构建输出为准）：

| 命令 | 用途 | 何时跑 |
| --- | --- | --- |
| `InkeysHeadlessTests.exe --no-window` | 算法与合同测试（约 5 分钟，后台运行并落盘） | S0、每个涉及被测模块的步骤、最终 |
| `Inkeys.exe --ui3-geometry-attribute-test` | 图形属性面板离屏测试，结果 `Build/shape-p2-ui/results.log` | S0、S4、S5、最终 |
| `Inkeys.exe --bar-eraser-offscreen-test` | 橡皮属性与翻页离屏测试，结果 `Build/eraser-b/offscreen-results.log` | S0、S3–S6、最终 |
| `Inkeys.exe --ui3-shape-editor-test` | 形状编辑离屏测试，结果 `Build/shape-p34-ui/results.log` | S0、S4–S6、最终 |
| `Inkeys.exe --ui3-multitarget-benchmark` | 主栏加四翻页的多目标基准（估计 5 分钟以上） | S0、S6、最终 |
| `Inkeys.exe --ui3-golden-dump <目录>` | 像素基线导出（S0 新增） | 每个步骤结束 |
| `Inkeys.exe --ui3-host-offscreen-test` | 第二宿主验证（S6 新增），结果 `Build/ui3-host/results.log` | S6、最终 |

各入口以退出码加结果文件判断，没有统一的 stdout PASS/FAIL。窗口类测试不要并发运行。已知既有失败（规划时未运行，S0 实测后填入「基线现状」）不归本任务。

## 3. 步骤

每个步骤的完成条件：完整构建通过；本步骤指定的测试不比 S0 基线差；像素基线符合该步骤的预期；本文件进度记录已更新。

### S0 基线与护栏

- [x] 完整构建一次，确认起点可编译。
- [x] 运行上表全部既有测试，把通过/失败现状写入「基线现状」。
- [x] 新增 `--ui3-golden-dump`：实现放 `UI/Bar/Bar.Golden.Test.cpp`（模块实现单元，编入主工程，登记 vcxproj 与 filters），在 `IdtMain.cpp` 的测试参数分发处接入。场景清单见 `design.md` §11。经 `CreateBarOffscreenRenderCallback`、`CreatePageControlOffscreenRenderCallback` 与形状编辑夹具取得像素；动画用固定帧时间推进到静止。
- [x] 连跑两次确认逐字节一致；不稳定的场景剔除并记录原因。
- [x] 导出基线到 `Build/ui3-golden/baseline/`。

### S1 UI3 骨架与叶子独立模块

- [x] 建目录 `Inkeys/Inkeys/UI/UI3/`、根模块 `UI3.cppm`、filter `Modules\Inkeys\UI\UI3`。
- [x] 迁入独立模块（`design.md` §2.2 上表）：`Animation`（含 `Bar.FontSize.h`；15 个产品 helper 拆到 `Inkeys.UI.Bar.PenPreviewMotion`）、`Metrics`（通用部分；`Inkeys.UI.Bar.Metrics` 保留产品常量并 `export import` 它）、`Material`、`DirtyRegion`、`Superellipse`、`LoadingIndicator`、`FrameRing`、`WakeSignal`、`FramePacing`、`PresentationProbe`。
- [x] `Inkeys::UI::Bar` 中加 `using` 垫片保留旧名（§2.5）。
- [x] 所有 includer 改为 import（RH §2.2）；`RenderPipeline.cpp` 改导入 `Inkeys.UI.UI3.FramePacing`；`StartupPreview.Drawing.h`、`StartupPreview.Effects.Policy.h` 改为由实现单元先 import 再 include。
- [x] 同步主工程、filters、`InkeysHeadlessTests.vcxproj`。
- 预期：像素基线逐字节一致；Headless 测试不变差。

### S2 主栏专有头模块化

- [x] 按 `design.md` §2.3 的表逐组转换，一组一构建：`BottomDock` + `AutoCollapse` + `StartupPlacement` → `MainLayoutPolicy`、`DisplayTransition`、`WindowGeometry` → `PresentDecision`、`PresentationAlpha`、`PresentationSource` → `A2`、`I18nFormat`、`Haptics`、`ShapeGesture`、`LogoAppearance`、`StatusIndicatorModel` → `KeyboardShortcuts`（分区）。
- [x] 全部 includer 改 import，含 `IdtMain.cpp`、`IdtState.cpp`、`Setting.cpp`、`Setting.Pages.cpp`、`PageControl.cpp` 与约 20 个测试源。
- [x] 删除已转换的 `.h` 并清理工程登记。
- 预期：像素基线逐字节一致；Headless 测试不变差。

### S3 第一层重型分区

顺序很重要，先解耦再移动：

- [x] 主题：建立 UI3 主题状态与订阅（§4.2），主栏改为发布；`GetThemeColor` 的 SelectionOverlay 副作用改为订阅回调。
- [x] 绘制器解耦：删除 owner 构造器与三个友元声明，主栏改为服务注入；粗细预览/FineDial 绘制移到 `Inkeys.UI.Bar:PenPreviewRendering`（§4.4）；常驻透明度改读主题快照；SVG 主 Logo 改为预处理钩子（§4.1）。
- [x] 光照：拆出产品侧输入解析与 UI3 光照状态机（§6.3 的状态机部分先落地，发布订阅在 S5）。
- [x] 移动：`:Primitive`、`:Format`、`:Envelope`、`:Theme`、`:Renderer`。此时绘制器仍与帧目标在同一个类里也可以，目标分离在 S6；但 UI3 内不得再有对 Bar 的依赖。
- [x] 图元位图缓存增加设备代次校验。
- [x] 迁入代码的日志改为 `Module::UI3` 并全部改为非阻塞接口（§8）。
- 预期：像素基线逐字节一致；橡皮离屏测试不变差。

### S4 第二层

- [x] 按钮模型拆分（§5.1），入口函数迁入 `:Button`，去掉对 `barUISet` 的读取。
- [x] `Inkeys.UI.UI3.Interaction`：迁入邮箱、任务、`ButtonPointerState`；新增激活方式与手势识别（§5.3）。
- [x] `:Progress`：长按进度状态单元；图形长按与取色器长按改用它，时长不变；改正 `ConsumeHoldWake` 处的过期注释。
- [x] 扩展内容合同（§5.5）；现有 `content` / `underlay` 调用方按需补充范围声明。
- [x] 把手工自绘的普通控件改为标准按钮（有意变更清单 C5–C7 涉及的控件），尺寸参数保持原值。
- 预期：像素基线只允许 C5–C7 相关场景出现差异；逐项核对实测差异并更新 `design.md` §9，随后刷新基线。三个离屏测试不变差。

### S5 第三层

- [x] `:List`：容器与通用排布引擎；产品排布规则留在调用方。
- [x] `:Panel` 与叠层；按 `design.md` §6.2 的采用范围表逐个接入（属性面板 → More/tooltip/底栏提示 → 翻页固定面板 → 形状样式栏 → 主栏输入的留白查询）。
- [x] `:Lighting` 的发布订阅与指针通知接口；移除对 `floating_window` 的 `PostMessage` 和发布端对 SelectionOverlay 的点名唤醒。
- [x] `:ColorPicker` 组件；主栏与形状样式栏改用它，各自提供放置、默认色系与提交回调（§6.4）。删除两套旧实现与 `Bar.State.cppm` 中不再需要的 `colorPicker*` 字段。
- 预期：像素基线只允许 C1–C4 相关场景（形状样式栏取色器）出现差异，核对后刷新基线。验收点「主栏拖动实时改笔色、样式栏松手单次提交」由形状编辑离屏测试中已有的断言（picker marker/preview 本地变化、对象样式保持权威）与新增断言覆盖。

### S6 第四层

- [ ] `:Host`：帧目标三种形态、帧事务、提交端、坐标映射（§7.1–7.3）；绘制器与帧目标彻底分离。
- [ ] `:HostInput`：共享输入 worker 与表面注册迁入；主栏注册主内容处理器；注册键加内容实例标识（§7.4）。
- [ ] 三类宿主与白板背景接入（§7.5）；`BarPageSurface`、`ShapeEditorSurface` 去掉对外的绘制器/资源/Present 接口。
- [ ] 帧事务失败与恢复、帧目标生命周期、宿主绑定的日志（§8）。
- [ ] 新增 `--ui3-host-offscreen-test`（§7.6）。
- 预期：像素基线逐字节一致；全部离屏测试与多目标基准不变差；新测试通过。

### S7 改名与清理

- [ ] 列出去掉 `Bar` 前缀后会冲突的名字并定名。
- [ ] 机械改名，删除 `Inkeys::UI::Bar` 中的全部垫片。
- [ ] 确认 `UI/UI3/` 内没有 `import Inkeys.UI.Bar`、`IdtMain.h`、`IdtState.h`、`barUISet`、`floating_window`、`currentBarThemeStyle`。
- 预期：像素基线逐字节一致。

### S8 日志复核、规范、收尾

- [ ] 日志复核：对照 `design.md` §8 的事件表逐项确认存在；确认渲染线程、交互 worker、窗口过程中没有阻塞式日志调用；实际运行一次离屏测试，检查日志文件里出现 `UI3` 模块的生命周期记录。
- [ ] 按 `design.md` §10 更新 `.trellis/spec/`（使用 `trellis-update-spec`）。
- [ ] 全量验证：完整构建、Headless、四个既有离屏入口、第二宿主测试、像素基线。
- [ ] 独立复审（Codex 只读 + 主 Agent 读 diff）：检查越界修改、模块环、文件格式、工程登记、热路径日志。
- [ ] 更新父任务 `prd.md` 的任务地图；向用户交付实机检查清单（§4）。

## 4. 用户实机检查清单（任务完成后由用户执行）

像素基线不覆盖动画过程、真实输入、窗口合成。请在 Windows 11 上至少检查：

- 主栏：展开/收起动画、拖动与吸附、换边、深浅主题切换、常驻透明度调节、各工具按钮按压与 hover、光标边缘光跟随。
- More：开合动画、关闭 X、留白点击不穿透到下层按钮。
- 绘制属性：笔型切换与扩展入口、「自由线」行、三档粗细与精调、粗细预览、两个 tooltip 的出现与关闭、11 个预设色块的点选与拖选、自定义色盘入口。
- 主栏取色器：打开/关闭、亮暗切换、拖动时笔色实时变化、长按锁色进度、松手后颜色被记住、亮暗与关闭按钮的释放回弹（C5）。
- 图形/橡皮/撤回属性面板：开合、图形长按锁定进度、橡皮示例、撤回按钮。
- 多步形状提示栏：出现、折行、步骤环、四个按钮；与底栏吸附提示互斥。
- 底栏吸附/居中提示：文字切换动画。
- 翻页控件：PPT 模式四个位置、白板模式两个位置、More 与主题菜单、页码等待环、拖动与长按。
- 形状样式栏：工具栏按钮、色板、边数、取色器（C1–C4：关闭变慢到 0.40 秒、紧凑态与过渡方式、亮暗图标直接切换）、拖动时图形颜色不变而松手后一次生效、避让主栏与屏幕边缘。
- 跨表面：光标从主栏移到翻页控件、形状样式栏时边缘光连续。
- 日志：关闭调试开关正常使用一段时间，`Inkeys\Logs\` 下的日志文件中有 `[UI3]` 的生命周期记录（`frame target recreated`、`输入表面注册`、`主内容输入处理器注册`、`光照订阅者登记`）且没有刷屏；拔插显示器或切换缩放后出现新的 `frame target recreated`，没有 `frame transaction failed`。离屏测试只验证了帧目标三类记录能落盘，其余记录与真实窗口下的频率需要在这里确认。
- Windows 7 SP1 + KB2670838（如有条件）：启动、主栏显示、基本书写。

## 5. 遗留事项（实施中发现、不在本任务内处理的问题记在这里）

- `InkeysHeadlessTests` 没有 `.vcxproj.filters`，而目录规范要求登记；本任务不新建。
- `Other.Config.cppm` 导入 `Inkeys.UI.Bar.Metrics` 但调研未找到使用点，可能是遗留依赖；本任务不删除。
- （已处理）浅色常驻填充透明度：用户 2026-11-17 晚裁定以代码实际值为准（0.80 × 1.15 = 0.92），已修正 `UI3.Material.cppm`、`UI3.Theme.cpp` 的注释与规范 `bar-theme-material.md`（比例 1.10 → 1.15）、`ppt-idle-fade.md`（0.88 → 0.92）；数值未动。
- `SelectionOverlay::Render` 的隐藏分支未检查 `Hide` 结果即提交（调研标为推测，需运行确认）。
- `--ui3-bottom-dock-transition-test` 在任务开始前就失败 28 条（PPT/白板模式下的重复拖放收尾，如「repeated Up did not commit its final input or released identity」）；本任务不修，只保证不变差。需用户另行确认是测试过期还是产品缺陷。 **用户 2026-11-17 晚：先不管。**
- `Whiteboard::Shutdown` 重建背景场景但不清 `configuredBackgroundScreen`（S6b-1 子代理读码推测：白板激活时 `Shutdown` 后同进程再 `Initialize` 且屏幕范围不变，背景场景不会被重新配置）；未验证，未改。
- `--ui3-geometry-attribute-test` 的 `pin-badge-hold` 用例依赖真实时钟（按下到抬起的实际耗时须小于约 0.48 秒）：机器有其他负载时会出现 3 条连带失败，单独运行通过（S7 验证时实测）。属于既有测试的脆弱点，未改。
- （已处理）UI3 里带 `Bar` 中缀的函数名：用户 2026-11-17 晚指示追加改名，已完成，见 §6.1 S7 行。
- 第二宿主测试未覆盖：字号变化过程中的跨宿主比较；漏调 `NotifyExternalEndDraw` 的发现能力（绘制器没有可观察的出口）。
- （已处理）取色器面板外框的鼠标边缘光强度：用户 2026-11-17 晚实机测试后裁定统一为 1.0（与其他大面板外框一致）；主栏取色器由 0.30 改为 1.0，`ColorPickerPlacement::cursorLightIntensityScale` 默认值同步改为 1.0。
- （已修复，S5 迁移引入的回归，用户实机发现）主栏绘制属性里的取色器：展开/收起时脏区只到绘制属性外框，选色或切换深浅后刷新不全，换色后取色器外框的颜色光影不更新。原因：迁移前取色器 6 个图元在 `shapeMap` 的绘制属性区间内，逐对象登记脏区时会并入绘制属性组边界并参与主光/鼠标光重绘范围；迁入 UI3 组件后不在 `shapeMap` 里，这一步漏掉了。修复：`Bar.RenderLoop.cpp` 逐对象登记循环之后，对 `colorPicker.Shapes()` 按绘制属性组成员（`Rigid` 变换）调用 `ObserveShape` 并并入组边界。离屏测试不覆盖局部脏区下的残影，需实机复核。
- 形状样式栏没有动态边缘光（鼠标光）：光标跟踪只接受主栏窗口和四个 PPT 控件窗口（`IsBorderCursorAcceptingWindow`），样式栏所在的选区覆盖层窗口不在名单内。任务开始前即如此，非本任务回归；用户 2026-11-17 晚：后续再加。
- 状态指示器小图标态（用户 2026-11-18 实机确认当前行为正确，已写入 `rendering-and-ui.md` 状态指示器合同）：脏区（含光晕外扩）越出窗口边缘、由窗口裁掉是正确的；小图标不得影响窗口大小，窗口上沿只在多步形状步骤栏或“底栏模式”提示栏出现时抬高。胶囊与小图标的现有设置均正确，后续改动须保持。
- 主栏呈现失败日志的口径变化：Module 由 `Bar` 变为 `UI3`（`frame transaction failed: role=main-bar …`）；延后/放弃的帧不再记为失败。
- 随任务一起做的两处非迁移改动：`UI3.Lighting.cpp` 指针通知序号改为 32 位回绕（原 64 位序号经 `WPARAM` 传递，在 32 位配置下会截断失配）；`UI3.ColorPicker.cpp` 清掉 `TickHold`/`PointerDown` 里的冗余代码。
- 2026-11-17 10:55 工作区曾被意外切到 `refactoring/draw` 再切回（两分支同一提交，文件内容未变）；期间原来未跟踪的新文件（37 项）被暂存进真实索引，非本任务操作，未还原。

## 6. 基线现状（S0 填写）

2026-11-16，HEAD `7047db94`，Debug|ARM64，动产品代码之前：

| 入口 | 结果 | 备注 |
| --- | --- | --- |
| 完整构建 | 通过，0 警告 0 错误 | |
| `InkeysHeadlessTests.exe --no-window` | 退出码 0（约 500 秒） | |
| `--ui3-geometry-attribute-test` | 退出码 0，`checks=1587890 failures=0`（约 290 秒） | |
| `--bar-eraser-offscreen-test` | 退出码 0，`failures=0`（约 30 秒） | |
| `--ui3-shape-editor-test` | 退出码 0，`scenes=549 checks=330379 failures=0`（约 265 秒） | |
| `--ui3-multitarget-benchmark` | **退出码 1**，27 组运行每组 `failures=1~2`（约 410 秒） | 既有失败，属时延预算类断言；且本次与 Codex 同时运行。后续只比较「不比这更差」，时延数字不作硬性判据 |
| `--ui3-golden-dump` | 两个独立进程各跑一次，OK 场景逐字节一致 | 场景数与跳过项见进度记录 |
| `--ui3-bottom-dock-transition-test` | **退出码 1**，`summary failures=28 result=FAIL` | 2026-11-17 S6b-1 时才发现这个入口未进基线。用任务开始前的旧构建（主检出目录 11-16 11:16 的 `Inkeys.exe`）补测：同样 28 条，逐用例失败数相同（普通用例 0，PPT/白板模式用例 3–4，`left-150` 3）——**既有失败，非本任务引入**。旧结果存于仓库外 `impl/olddock/stderr.txt`；用 `dock-cmp.sh <标签>` 比对逐用例汇总行，判据是与旧结果逐行一致。动主栏的步骤（S6b-3、S7）必须跑 |
| `--page-control-hidden-test`、`--ui3-workspace-centering-test` | 退出码 0（S6a 可执行文件上补测） | 同样是 S6b-1 时补进验证范围；前者会创建不可见的真实窗口，是白板背景生产提交路径唯一的覆盖 |

执行细节（供后续步骤沿用）：

- 构建、测试、Codex 调用、格式规范化、基线比对、机械迁移的脚本都在仓库外 `%TEMP%\inkeys-codex\ui3\`（`build.ps1`、`runtests.sh`、`codex-run.sh`、`fixfmt.py`、`cmp-golden.sh`、`migrate.py`），简报与报告在其 `impl\` 子目录。可丢失，丢失后按本文件 §2 重建。
- Codex 沙箱不能启动子进程：文件整体移动由主 Agent 用 `migrate.py`（内部 `git mv`）预先完成，Codex 只改内容。
- **垫片的实际做法**（对 `design.md` §2.5 的落实方式）：S1 实施时确认 `UI/Bar/` 的老核心代码（`Bar.UI`、`Bar.Animation`、`Bar.Metrics`、`Bar.Button`、`Bar.Rendering`、`Bar.Theme`、`Bar.State`、`Bar.Layout`、`Bar.Format`、`Bar.Main` 大部分）位于**全局命名空间**，只有较新的叶子模块在 `Inkeys::UI::Bar`（调研未覆盖这一点）。因此：迁入 UI3 的符号一律进 `Inkeys::UI::UI3` 且暂不改名；各消费者翻译单元加两行 `using namespace Inkeys::UI::UI3;` 与 `namespace Inkeys::UI::Bar { using namespace Inkeys::UI::UI3; }`（using 指令不随模块导出，所以每个翻译单元各写一次）。留在 Bar 的代码保持它原来所在的命名空间不动。S7 统一改名，并把非 Bar 消费者里的垫片换成正常的限定或单行 `using namespace Inkeys::UI::UI3;`。
- 每个阶段结束：`fixfmt.py` → 构建 → 指定测试 → 基线比对 → 导出 diff 交 Codex 审阅并修复 → 再构建与比对 → 更新进度记录。

## 6.0 断点与交接（2026-11-17 上午，用户将执行上下文压缩；压缩后先读这一节）

**用户的最新指令**（2026-11-17 上午，压缩完成后）：核对状态并读取交接内容后，**批准继续进行到完成**——此前「S6a 完成后先暂停」的要求已解除，S6a 验收后直接按下方顺序做完 S6b–S8 与最终双审阅。分工、不并行、未授权提交等约束不变。

**暂停记录**：2026-11-17 12:30 用户要求暂停，同日用户通知「暂停结束，继续」后已恢复：S6b-3 子代理在段 A 完成并全部验证通过的稳定点停下（回退点 `impl/s6b3-stageA/Bar.RenderLoop.cpp`），恢复后做段 B。主 Agent 对段 B 的裁定：约 2000 行绘制块允许用一次性脚本逐字节搬运（哈希核对）；坐标映射的光效外扩填 0；调试帧率与调试框留在调用方；`BarPenPreviewRenderer` 改用传入的绘制器；`DrawMainBarContent` 为协调器成员函数。定时恢复任务已取消。

**第二次暂停**：2026-11-17 13:11 用户要求暂停到当天 14:01 之后继续；14:01 用户通知「时间已经到了，现在继续」后已恢复，定时恢复任务已取消。S6b-3 子代理暂停时处在段 B 中间：绘制块（105 行 + 1907 行）已用脚本原样搬进 `DrawMainBarContent` 且哈希一致，参数未补，**工作区在它交回之前不可构建**（段 A 可构建版本在 `impl/s6b3-stageA/Bar.RenderLoop.cpp`）。若压缩/重启后发现子代理已不存在而 `s6b3-mainbar.report.md` 没有写出：先向用户说明，再决定是从工作区现状接着做还是退回段 A 回退点重做段 B。主 Agent 已逐行审过段 A（见仓库外 `impl/final-review-notes.md`），无偏差。

**当前断点**

- S0–S5 全部完成并通过各自的阶段审阅。最近的检查点：`s5`（tree `24a17c44a5fd9646204b24125faa73be91650731`）。所有改动都在工作区，**未提交**（约 150 个文件）。
- **S6a 正在进行**：由 Claude 的 `trellis-implement` 子代理实现（2026-11-17 上午派出，后台运行，完成时会有通知）。它按 `%TEMP%/inkeys-codex/ui3/impl/00-claude-impl.md` + `s6a-frametarget.md` 工作，自己构建、跑测试、比对像素，并把报告写到同目录的 `s6a-frametarget.report.md`。
- 若压缩后收到它的完成通知（或发现报告文件已存在）：
  1. 读报告，核对它声称执行过的验证；
  2. 主 Agent 自己复核一次：`fixfmt.py` → `build.ps1 -LogName s6a-verify` → `runtests.sh s6a golden:s6a eraser inputworker` → `cmp-golden.sh baseline s6a` 必须 `same=35 diff=0`；慢测试（geometry、shape、multitarget、headless）若子代理报告已跑且结果行可信，可不重复；
  3. 通过后 `checkpoint.sh s6a`，把下表 S6a 状态改为完成；
  4. **然后暂停，向用户汇报 S6a 结果并等待指示。**
- 若压缩后既没有通知也没有报告文件：子代理可能仍在运行或已中断。先看 `git status` 是否出现 `Inkeys/Inkeys/UI/UI3/UI3.Host.cppm`、报告文件是否存在；不要重复派出第二个实现代理，除非确认前一个已结束且工作未完成（那种情况下先向用户说明）。

**分工（用户 2026-11-17 上午确定，S6 起生效）**

- 实现由 Claude 完成（主 Agent 直接改，或一次一个 `trellis-implement` 子代理，**不并行**）。
- Codex 只做审查：`codex-run.sh read <sN-review> 00-review-only.md`（只读沙箱、只报告）。修复由 Claude 做。
- 全部完成后：主 Agent 独立审查（含 S0–S5 由 Codex 写的代码，逐行读）→ Codex 审查 → Claude 修复。
- Codex 配置：`gpt-6.1-sol` + High；Fast **仅本会话**可用（`codex-run.sh` 已是 `service_tier=fast`）；Astra 不可用。
- 本轮允许 GUI 但可能遇到锁屏，仍以无窗口测试为主。未授权提交。

**S6a 之后剩余的工作（顺序）**

1. S6b（`s6b-hosts.md`）→ S6c（`s6c-hostinput.md`）→ S6d（`s6d-hosttest.md`）：每步由 Claude 实现并自验；每步后 `checkpoint.sh`。
2. S6 阶段审阅：导出 `git diff -M <s5 tree> <当前 tree> > impl/s6.diff`，在 `s6-review.md` 末尾追加实测结果，`codex-run.sh read s6-review 00-review-only.md`，按报告修复。
3. S7 机械改名：`python -I rename.py plan` 看映射与冲突（冲突写进 `impl/s7-overrides.json`），`rename.py apply`；删除垫片第二行并把 `Bar::` 限定的 UI3 符号改为 `UI3::`；构建、全量测试、像素比对（必须仍逐字节一致）。
4. S8：日志复核（对照 `design.md` §8 事件表）；按 `design.md` §10 更新 `.trellis/spec/`（用 `trellis-update-spec`）；全量验证（构建、Headless、四个离屏入口、多目标、第二宿主测试、像素基线）；更新父任务 `prd.md` 的任务地图。
5. 最终双审阅与修复；向用户交付实机检查清单（本文件 §4）与待决事项。

**主 Agent 独立审查的进度（最终审查时不必重复）**

- 已逐行审并认定等价（S4，`Bar.Interaction.cpp`）：主栏按钮点击与清空的原生双击（识别器的 `lastClicked_` 与 `lastClickedMainBarButton` 同点置位/复位）；主按钮右键关闭（次键默认 OnPress）；More 面板按钮与 More 关闭 X（移出即取消、目标上抬起才激活）；11 个预设色块（按下即改色、跨项改色、留白不动作、抬起不追加、命中优先级与触觉收尾不变）。
- 已逐行审并认可：S5 审阅中 Codex 对 `ColorPicker::SetHover` 的修复。
- 尚未逐行审：S1–S3 的迁移内容、S4a 按钮拆分、S4c 手工控件的绘制侧、S5b 光照通知、S5c 取色器提交时机、S5d 面板输入归属、S6 全部。

**留给用户决定的事项（最终报告里要列出）**

- 浅色常驻填充透明度注释写 0.88、实际 0.92，哪个是预期值（未改数值）。
- 是否提交（未授权，未提交）。

## 6.1 子步骤拆分与执行状态（压缩/重启后先看这里）

每个子步骤的流程：`migrate.py`（如需移动文件）→ `codex-run.sh write <简报名>` → `fixfmt.py` → `build.ps1` → `runtests.sh` + `cmp-golden.sh baseline <目录>` → `checkpoint.sh <名>`。每个阶段（S 编号）结束再跑一次 Codex 审阅（S0–S5 为 `write` 模式审阅并修复；S6 起为 `read` 模式只审查，见下方分工变更），然后重建、更新本文件进度记录。简报、报告、日志都在 `%TEMP%/inkeys-codex/ui3/impl/`（`<名>.md` / `<名>.report.md` / `<名>.log`），检查点 tree 哈希在其 `checkpoints/`。像素基线在 `Build/ui3-golden/baseline/`（35 个 OK 场景）。

用户对本轮的要求（2026-11-17）：已批准开工；可以用单独子进程，但**不能多个子 Agent 并行**；每个阶段结束派 Codex 审阅并修复；全部完成后主 Agent 独立审阅 + Codex 审阅；本轮允许启用 GUI，但可能遇到锁屏，仍以无窗口为主；Codex 的 Astra 不可用；Fast 起初不可用，用户于 2026-11-17 早晨说明**仅本会话内**新开的 Codex 可用 Fast（`gpt-6.1-sol` + High + `service_tier=fast`，`codex-run.sh` 已改；换会话后恢复 `service_tier=default`）；未授权提交。

**分工变更（用户 2026-11-17 上午提出，自 S6 起生效）**：S0–S5 的代码改写主体由 Codex 完成（主 Agent 负责设计、裁定、简报、脚本化迁移、编译修复与验证，大部分 diff 未逐行读）。用户要求改为：**实现主要由 Claude 完成，Codex 只做审查**；最后 Claude 独立审查（含 S0–S5 由 Codex 写的部分，逐行读）→ Codex 审查 → 修复。落实方式：

- 实现：主 Agent 直接改，或派 Claude 的 `trellis-implement` 子代理（一次只开一个，不并行）；子代理自己构建、跑测试、比对像素基线。已写好的 `s6a`–`s6d` 简报内容照用，但其中「你无法编译 / 沙箱不能启动子进程 / 文件移动已由主 Agent 完成」等针对 Codex 的话不适用于 Claude 子代理。
- 审查：`codex-run.sh read <sN-review>`（只读沙箱），`00-review-common.md` 里「审完直接修复」的规则改为只报告；修复由 Claude 做。
- S5 阶段审阅在此变更前已以「审阅并修复」方式派出，其修改需由主 Agent 逐条核对。

| 子步骤 | 简报名 | 内容 | 状态 |
| --- | --- | --- | --- |
| S4a | `s4a-button` | 按钮模型拆分，公共入口迁入 `:Button` | 完成 |
| S4b-1 | `s4b1-interaction` | `UI3.Interaction`（含 `PointerGestureRecognizer`）、`UI3.Progress` 与单元测试 | 完成 |
| S4b-2 | `s4b2-adopt` | 图形长按、两处取色器长按、Main/More 点击/次键/双击改用统一单元；扩展内容合同 | 完成 |
| S4c | `s4c-controls`、`s4c2-controls` | 手工自绘控件改用标准按钮（两轮；固定按钮新增内容回调、独立反馈层、命中与可视分离；8 类控件全部迁移，静止像素不变） | 完成 |
| S4 审阅 | `s4-review` | | 完成 |
| S5a | `s5a-list` | `:List` 容器 + `UI3.ListLayout` 独立模块（含单元测试） | 完成 |
| S5b | `s5b-lighting` | `:Lighting` 状态机、发布订阅、指针通知（跨表面调用方与 UI3 不再持有主栏 HWND；SelectionOverlay 唤醒改为形状编辑侧静态订阅） | 完成 |
| S5c | `s5c-colorpicker` | `:ColorPicker` 统一取色器（C1–C5；静止像素不变）。保留为场景参数的调研未列差异：面板外框鼠标光倍率（主栏 0.30/样式栏 1.0）、提示/色系反馈曲线、按钮背景推进时长与关闭期间显隐、按钮推进采样点、marker 精度、样式栏色板矩形四角命中。`BarUISetSvgEnum` 中原亮暗图标的两个序号保留为占位（呈现探针以序号作标签，`Ui3SvgMapOrdinalMax` 固定为 31） | 完成 |
| S5d-1 | `s5d1-panel` | `:Panel` + `UI3.PanelStack`（含单元测试）；Geometry/Eraser（主体与灵敏度菜单）/Recall/ConstructionPrompt、翻页三层、形状样式栏四层采用 | 完成 |
| S5d-2 | `s5d2-panel` | 主栏 Main/More/取色器/两个 tooltip/底栏提示/DrawAttribute 采用并登记主栏叠层；输入归属查叠层（状态指示器未采用：原实现不阻挡条目间留白；Thickness/FineDial 的激活范围与色块释放判定保留「当前布局」口径） | 完成 |
| S5 审阅 | `s5-review` | | 完成 |
| S6a | `s6a-frametarget` | `:Host` 帧目标/帧事务/坐标映射；绘制器与目标分离（`FrameTarget` 三种形态；绘制器只留设备缓存与内容绘制，经通告入口得知设备与帧状态；302 处调用点改接；帧事务与坐标映射只定义未采用）。主 Agent 复核：构建、35 场景逐字节一致、橡皮、输入 worker 通过；子代理另跑 geometry/shape/multitarget/headless 均不变差 | 完成（检查点 `s6a`） |
| S6b-1 | `s6b1-page`（总简报 `s6b-hosts`） | 翻页与白板背景接入帧事务；`BarPageSurface` 接口收窄（帧目标移到 `PageControl` 的 `SurfaceState` 与 `Whiteboard.cpp`；场景只留 `BindFrameTarget`/`PrepareDeviceGeneration`/`ReleaseDeviceResources`）；`FrameTransaction::Commit` 增加分步观察与三个结果标志；`FrameTransactionLogState` 日志闩锁。主 Agent 逐段读过差异并复核：构建、35 场景一致、橡皮/翻页离屏/输入 worker/`--page-control-hidden-test` 均 0 失败 | 完成（检查点 `s6b1`） |
| S6b-2 | `s6b2-overlay` | SelectionOverlay 与形状编辑接入帧事务；`ShapeEditorSurface` 接口收窄（帧目标为 `SelectionOverlay.cpp` 文件级变量；场景只留 `BindFrameTarget`/`PrepareDeviceGeneration`/`Draw(设备上下文)`/`ReleaseDeviceResources`）。主 Agent 把子代理写成两份的呈现事务合并为生产与测试共用的 `Bar::PresentShapeEditorFrame`。`Bar.Scene.cppm` 仍依赖根模块（`FrameTarget`、`BarContentExtent`），**不恢复**进 `InkeysHeadlessTests.vcxproj`。复核：构建、35 场景一致、橡皮、shape 331032/0、输入 worker、`--shape-editor-native-input-test` 通过 | 完成（检查点 `s6b2`） |
| S6b-3 | `s6b3-mainbar` | 主栏接入帧事务（段 A）；抽出 `BarRenderLoopCoordinator::DrawMainBarContent`（段 B：约 2000 行绘制块脚本逐字节搬运、哈希核对；参数为状态、内容绘制器、设备上下文、`FrameCoordinateMapping`、`BarMainBarContentFrame`、可空探针；`BarPenPreviewRenderer` 改收绘制器）。主 Agent 逐行审段 A、用消去移动行的差异独立核对段 B（真实改动 16 行），并随带修了两个小项（光照通知序号 32 位回绕、取色器冗余代码）。复核：构建、35 场景一致、橡皮、shape 331032/0、输入 worker、`dock-cmp.sh` SAME-AS-BASELINE、居中 PASS、隐藏窗口 0 失败；子代理另跑 geometry/multitarget/headless 不变差 | 完成（检查点 `s6b3`） |
| S6c | `s6c-hostinput` | `:HostInput`：共享输入邮箱、表面注册表（键为 HWND + 内容实例，缺省 0）、worker 泵、离屏 worker 迁入（脚本逐字节搬运）；主栏经 `MainContentHandler` 接入，主栏消息为不透明载荷；`Bar.InteractionWorker.h` 删除；三处阻塞日志改非阻塞并新增注册/注销/溢出日志。主 Agent 把子代理引入的「UI3 包含输入诊断头」改为宿主登记的诊断出口（`SetSurfaceInputDiagnosticSink`）。复核：构建、输入 worker、35 场景一致、橡皮、shape、dock、居中、隐藏窗口、原生输入路由均通过 | 完成（检查点 `s6c`） |
| S6d | `s6d-hosttest` | `--ui3-host-offscreen-test`（`Bar.Host.Test.cpp`）：同一帧主栏内容经「内容重放」测试桥画进三个第二宿主（普通位图帧目标、外部上下文两种裁剪方式，各用新建绘制器），与现有 GDI 兼容路径逐字节比较；另查范围、多帧稳定、设备缓存归属、全程无窗口，共 30 项。`:Host` 与内容绘制代码未改；生产路径只多一处「回调非空才进入」的判断。子代理反向验证改坏 8 处、发现 6 处（未发现：不同步字号帧、漏调 `NotifyExternalEndDraw`，静止场景下不影响像素）。主 Agent 逐行读过差异并复核：构建、host 30/0、35 场景一致、橡皮、输入 worker、dock、居中、隐藏窗口均通过 | 完成（检查点 `s6d`） |
| S6 审阅 | `s6-review` | 差异 `impl/s6.diff`（相对 `s5` 检查点）；Codex 只读审查，无阻塞项，1 条应修 + 4 条建议，全部由 Claude 处理：帧事务在提交端/观察回调抛异常时漏 ReleaseDC/EndDraw（已修，新增 `transaction-unwind.*` 回归，修复前失败、修复后通过）；`BindRenderer` 对带旧缓存的绘制器先清理；表面输入诊断出口增加启用查询（关闭时不格式化）；第二宿主测试加字号帧就绪断言；`:Host` 接口注释写明关闭帧目标裁剪时宿主须自己裁到视口。未补：字号变化过程中的跨宿主比较、漏调 `NotifyExternalEndDraw` 的发现能力 | 完成（检查点 `s6`） |
| S7 | （主 Agent 用脚本 `rename.py`、`s7-shim.py`） | ① 先合并取色器重复常量：`ColorPickerPanelWidth/Height` 由 `UI3.ColorPicker.cppm` 导出为唯一定义，主栏侧 8 个 `BarColorPicker*` 定义与 extern 删除（6 个已是死代码），UI3 本地的 `BarDrawAttributeSurfaceOpacity` 改名 `ColorPickerSurfaceOpacity`。② 机械改名：129 个 `Bar[A-Z]…` 标识符去前缀（映射表 `impl/s7-map.json`；无冲突；这些标识符不出现在任何字符串字面量里；`BarPageLoadState` 是主栏产品类型，跳过）。③ 垫片清理：66 个文件的 `namespace Inkeys::UI::Bar { using namespace Inkeys::UI::UI3; }` 全部删除；经垫片解析到 UI3 的限定名（约 80 个名字）改为 `…UI3::X`，判据是「UI3 接口有声明且主栏侧没有同名声明」，主栏有同名包装的 9 个名字保持 `Bar::`；全局作用域的 `using namespace Inkeys::UI::UI3;` 作为产品层约定保留（67 个文件，写入 spec）；两个只导入 UI3 独立模块的测试源删除残留的 `using namespace Inkeys::UI::Bar;`。④ 边界检索：`UI/UI3/` 内无 `import Inkeys.UI.Bar`、`Inkeys::UI::Bar`、`IdtMain.h`、`IdtState.h`、`barUISet`、`floating_window`、`currentBarThemeStyle`；唯一剩下的 `Bar[A-Z]` 记号是一处注释里的 `BarPageLoadState`。⑤ **追加改名（用户 2026-11-17 晚指示）**：UI3 里 44 个带 `Bar` 中缀的函数名去掉中缀（`DrawBarButtonVisual` → `DrawButtonVisual`、`HitTestBarButton` → `HitTestButton` 等；脚本 `rename-infix.py`，映射表 `impl/s7b-map.json`，无冲突）；主栏侧的同名包装重载随同一记号改名；`recall_attribute_tests.cpp` 里读取源码文本做断言的 3 个字符串同步改名；规范文档同步（7 个文件 40 处）。未改：`UI3.PresentationProbe` 的字段 `trueBarAttemptSerial`（不是函数名，探针协议字段） | 完成（验证结果见 §7） |
| S8 | （主 Agent） | ① 日志复核（对照 `design.md` §8）：`UI/UI3/` 内 40 余处日志全部是 `TryFormat`/`TryWrite`、Module 全部为 `UI3`；补上缺的一类——SVG/PNG 图元的资源提取、解码、尺寸解析、栅格化、上传失败（Error；栅格化与上传每图元每设备代次一次，资源提取每资源每进程一次）。② 规范：新增 `native-desktop/ui3-layers.md` 并加入索引；更新 `cpp-conventions.md`（头文件判定标准、独立模块与分区的选用、不得跨模块前置声明、预处理效果不能导出、BOM+CRLF）、`directory-structure.md`、`rendering-and-ui.md`、`errors-logging-and-resources.md`、`ui3-render-diagnostics.md`（改掉旧 async logger 描述）、`ui-pointer-session.md`、`whiteboard.md`；规范里被改名的标识符同步替换（9 个文件 72 处）。③ 最终双审阅：主 Agent 逐行核对「还原机械改名后」的差异（485 行，全部是限定名改写、垫片删除、常量合并、新增日志）并补审 S4a、S4c 绘制侧、S5d-1 面板；Codex 只读审阅无阻塞项，3 条发现全部处理：`--ui3-host-offscreen-test` 增加测试专用日志会话并断言 UI3 生命周期记录落盘（闭合日志验收标准）、SVG 资源提取与尺寸解析失败日志、规范三处表述修正 | 完成（验证结果见 §7） |

实施中确立、简报里反复使用的经验：

- Codex 遇到设计与代码事实矛盾会零改动返回并说明，主 Agent 裁定后把裁定追加到简报再重跑即可（S1a 命名空间、S2b PresentationSource、S4a 分隔项门控都是这样解决的）。
- 「依赖模块符号的普通头，在 import 之后再 include」不可靠（同一头在接口与实现单元各包含一次会重复定义），一律转成模块。现存例外只有三个 `StartupPreview.*.h`。
- 导出分区的全局片段里若带进含默认 `operator<=>` 的头，所有导入方都需要 `<compare>`：不需要对外的分区做成内部分区。
- 新符号避开 Windows SDK 已有名字（`GestureRecognizer` 撞名）。
- 多目标基准 `--ui3-multitarget-benchmark` 的既有失败构成：每组 `culled=0`（`mainCulledDraws==0` 断言）1 个 + 时延预算（p95≤16.67ms、p99≤33.4ms，Debug+WARP 下常超）1 个；判断「不变差」时看是否出现这两类之外的失败。
- **不要在脚本运行期间修改它**（bash 边读边执行，`codex-run.sh` 运行中被改会在 Codex 结束后以 127 退出并截断日志；报告文件不受影响）。
- Bash 工具里的 Python heredoc 会吃掉反斜杠，含 Windows 路径的内容用 Write 工具落盘。

## 7. 进度记录

| 步骤 | 日期 | 构建 | 测试 | 像素基线 | 备注 |
| --- | --- | --- | --- | --- | --- |
| S0 | 2026-11-16 | 通过 | 四项既有测试 0 失败；多目标基准既有失败（见 §6） | 基线建立：37 场景，35 OK、2 SKIP（撤回面板读真实时钟、取色器紧凑态是过渡态），两进程逐字节一致 | Codex 审阅修了 3 个护栏问题（tooltip 锚点锁存、静止帧复用旧像素、缺目标区域像素证据）并补 7 个场景；主 Agent 补 2 处漏掉的 include。基线目前画不到：hover 脉冲、沿框进度、状态指示器、Freeze 背景、Slider/FineDial、动态光标光 |
| S1 | 2026-11-17 | 通过 | Headless 退出码 0 | 35 场景与基线逐字节一致 | UI3 现有 11 个独立模块 + 根模块。`Bar.LogoAppearance` 提前转为模块（「import 后再 include 的普通头」会与接口分区重复定义，这种写法不可靠，S2 不再使用）。仍以该写法存在的头：`Bar.Presentation.Source.h`（S2b 转模块）、`Ui3PresentationFixtureAuth.h`、三个 `StartupPreview.*.h`。Codex 审阅 1 项 P1（缺预先包含的 `<d2d1_3.h>`）已修；多余垫片 2 处留到 S7 |
| S2 | 2026-11-17 | 通过 | Headless 退出码 0；橡皮离屏 failures=0 | 35 场景与基线逐字节一致 | 16 个头转为模块。两处偏离原设计表并已定案：① `Bar.KeyboardShortcuts` 为**内部分区**（不经主接口导出，vcxproj 用 `CompileAsCppModuleInternalPartition`），否则所有主栏导入方都被迫包含 `<compare>`；② `Bar.PresentationSource` 的内容整体放在 `export extern C++` 块内以保持全局模块归属（定义分散在主栏主模块与 `IdtMain.cpp`），其 `.cpp` 保持非模块源；`Ui3PresentationFixtureAuth.h` 恢复自包含，`GetCompiledUi3FixtureSourceV1` 参数改为 `std::uint32_t`。检查点改用悬空 tree 对象（`checkpoint.sh`，哈希存于仓库外脚本目录的 `checkpoints/*.tree`）。Codex 审阅：1 项 P1（交互 worker 上的阻塞日志）与 1 项 P2 已修 |
| S3 | 2026-11-17 | 通过 | 橡皮 failures=0；形状编辑 scenes=549 failures=0；图形属性 failures=0（检查数与基线相同） | 每个子步骤后 35 场景与基线逐字节一致 | 四个子步骤：S3a 主题（改为「产品绑定、UI3 实时读取」，见 design §4.2 实施调整）；S3b 图元/格式/包络；S3c-1 绘制器原地解耦（无 owner 构造器、无友元，粗细预览绘制移到 `Inkeys.UI.Bar:PenPreviewRendering`，光照改收通用输入结构）；S3c-2 绘制器迁入 `:Renderer`。光照状态机暂留在绘制器内，S5 与发布订阅一起拆成 `:Lighting`。`InkeysHeadlessTests.vcxproj` 移除了 `Bar.Scene.cppm`（依赖 UI3 根模块后无法单独编译，且无测试导入它），S6 评估恢复。Codex 审阅：2 项 P1（渲染线程阻塞日志、重建失败日志未限频）已修。基线盲区（动态光标光、hover、状态指示器、Slider/FineDial）靠读码审阅，需实机确认 |
| S4 | 2026-11-17 | 通过 | 橡皮 failures=0；图形属性 failures=0；形状编辑 failures=0；输入 worker PASS；Headless 退出码 0（含新增手势/进度单元测试） | 每个子步骤后 35 场景与基线逐字节一致 | 五个子步骤（S4a、S4b-1、S4b-2、S4c 两轮）。有意变更 C6、C7 已交付且静止态像素不变（只有按压/hover 反馈变化，需实机确认）；C5 留到 S5c。保留产品侧实现：主 Logo 的 Seek/拖动/吸附判定。扩展内容范围声明已提供但现有调用方均未启用（候选：Geometry 线宽胶囊、ShapeEditor 线宽/色块/色轮/X、翻页主题预览、背景 drawContent）。现状记录（未改）：Scene/ShapeEditor 的按钮 Retarget 仍读全局 `barUISet.barStyle.darkStyle`，Configure 的 Geometry 测量仍用 `barUISet.spec`。Codex 审阅：1 项 P1（固定按钮文字拟合默认值翻转）已修 |
| S5 | 2026-11-17 | 通过 | 橡皮 failures=0；图形属性 failures=0；形状编辑 checks=331027 failures=0（含新增断言）；输入 worker PASS；Headless 退出码 0（含排布、叠层单元测试）；多目标基准无新增失败 | 每个子步骤后 35 场景与基线逐字节一致 | 五个子步骤（S5a、S5b、S5c、S5d 两部分）。有意变更 C1–C5 已交付且静止态像素不变。Codex 审阅：1 项 P1（统一取色器在连续 Move 时反复重启按钮悬停动画）已修并补回归断言，主 Agent 已逐行核对；1 项 P2（旧取色器常量残留）由主 Agent 清理。自动化覆盖不到、需实机确认：跨表面光标光时序、真实指针下的留白阻挡与输入归属、取色器 C1–C4 的过渡表现 |
| S6 | 2026-11-17 | 通过 | 第二宿主 30→36 项 0 失败；橡皮、形状编辑 331032/0、输入 worker、翻页隐藏窗口、居中、原生输入路由 0 失败；底栏吸附与任务开始前逐行一致 | 每个子步骤后 35 场景与基线逐字节一致 | 帧目标/帧事务/坐标映射、三类宿主接入、输入接入迁入 `:HostInput`、第二宿主验证；Codex 审阅 1 条应修（帧事务异常收尾）+ 4 条建议全部处理 |
| S7 | 2026-11-17 | 通过（改名后主工程一次编译通过） | 第二宿主 36/0；橡皮、输入 worker、形状编辑、Headless、居中、隐藏窗口、原生输入 0 失败；图形属性单独运行 0 失败（并行负载下曾 3 条计时失败，见 §5）；多目标 27 组均 2 条既有失败；底栏吸附与基线一致 | 35 场景与基线逐字节一致 | 129 个标识符去 `Bar` 前缀；66 个文件的垫片删除；取色器面板尺寸常量合并为 UI3 唯一定义 |
| S8 | 2026-11-17 | 通过 | 第二宿主 38/0；橡皮、输入 worker、形状编辑 331032/0、图形属性 1587890/0、Headless、居中、隐藏窗口、原生输入全部 0 失败；多目标 27 组均 2 条既有失败；底栏吸附与任务开始前逐行一致 | 35 场景与基线逐字节一致 | 日志补齐、规范更新、最终双审阅与修复；第二宿主测试增至 38 项（含日志落盘核对） |
| S7 追加 | 2026-11-17 | 通过（一次编译通过） | 全量验证被中断，并入下一行 | 并入下一行 | 44 个函数名去 `Bar` 中缀；浅色常驻透明度注释与规范修正为 0.92 |
| 实机修复 1 | 2026-11-17 | 通过 | 第二宿主 38/0；橡皮、输入 worker、形状编辑 331032/0、图形属性 1587890/0、Headless、居中、隐藏窗口、原生输入 0 失败；多目标 23 组 2 条、4 组 1 条（固定 1 条 culled=0 既有失败 + 帧耗时预算计时波动）；底栏吸附与基线一致 | 35 场景与基线逐字节一致 | 主栏取色器脏区与外框光影回归修复；取色器外框鼠标光统一为 1.0 |

**提交状态（2026-11-17 晚）**：用户先授权提交，随后在实机测试中发现取色器问题，改为：修复后重跑验证，不提交、不结束任务，继续等待实机测试。

**提交状态（2026-11-18）**：用户人工验收通过，授权提交但**不结束任务**（后续还要做脏区位置优化）。已提交（SSH 签名，未推送）：`56ed26e9` refactor(ui3)：`Inkeys/` 与 `InkeysHeadlessTests/` 全部改动（与检查点 `s7c` 同一棵树）；`8a1ac516` docs(spec)：`.trellis/spec/` 改动。本任务目录文档与父任务 `prd.md`/`task.json` 未提交，按惯例在任务归档时提交。
