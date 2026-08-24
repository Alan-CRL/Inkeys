# PPT 翻页控件主栏化：实施计划

## 0. 开工门禁与基线

- [x] 在开始产品代码前重新阅读收敛后的 `prd.md`、`design.md` 和 `.trellis/spec/native-desktop/rendering-and-ui.md` PageControl 合同。
- [x] 为主栏当前 `oneOne/twoOne/twoTwo` 布局建立共享 metrics 基线，并在重构前运行 ARM64 `--no-window`；hover/press/draw/hit 通过 Main Bar 与 Scene 的同一产品入口结构性复用，不复制输出模型。
- [x] 静态记录并清理当前错误所有权：`BarSurfaceScene` 的本地 hover/pressed、SVG/文字偏移、独立 draw/hit，以及 PageControl 的 Bar 障碍/Whiteboard drag 路径。

**Rollback 0**：仅增加等价基线测试；若测试无法稳定描述主栏现状，停止抽取并先补齐可测接口，不修改 PageControl。

## 1. 抽取主栏唯一按钮/背景运行时

- [x] 从 `Bar.RenderLoop.cpp`、`Bar.Interaction.cpp` 和现有 Rendering/Button 类型中抽取共享 metrics、hover/press、draw 与圆角 hit；动画值继续使用同一 `BarUi*` advance，宿主只保留坐标映射和 present damage union。
- [x] 抽取主栏背景 draw，并让主题、圆角、边框、光源参数直接读取主栏单一来源；Main Bar 和 PageControl 保持独立资源。
- [x] 先把 Main Bar 迁移到共享入口，并以共享 metrics/headless、完整构建和静态调用点证明 `oneOne/twoOne/twoTwo`、5 秒 hover、按压衔接与 draw 合同保持。
- [x] PageControl 接入前后均执行完整 ARM64 构建和 headless 测试；Main Bar 与 Scene 最终调用相同符号。

主要文件：`Bar.Button.cppm/.cpp`、`Bar.Layout.cppm`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp`、`Bar.Rendering.cppm/.cpp`、必要的新 Bar module、Bar headless tests。

**Rollback 1**：共享入口与 Main Bar 迁移作为独立检查点；若等价断言失败，只回滚该层，不触碰 PageControl/PPT/Whiteboard。

## 2. PageControl 稳定按钮与内容接入

- [x] 每个 surface 建立稳定 Previous/Page/Next `BarButtonClass`；Bottom 跨 PptCompact/WhiteboardExpanded 保持实例，Middle 仅切 Hidden/PptCompact。
- [x] PageControl 只计算 surface 拓扑、横竖/镜像锚点和 PPT 专属 Page 内容请求；标准按钮 metrics、hover/press、draw 和圆角 hit 委托共享 Bar 运行时，宿主自行映射 visual damage。
- [x] 实现横向混合字重页码整体测量、竖向上下页码和 Whiteboard 标准 `2x2` 内容槽，删除 PageControl `±13` 等固定偏移，并保留 PPT `-`/`/-` 占位与 Bottom/Middle `9999/999` 显示上限。
- [x] Previous/Next 保留同一 `barMore` SVG；仅 Arrow/Add 真实语义变化时调用共享内容转换，并把几何、SVG、标签加入同一批次。
- [x] PageControl 外层背景改用共享主栏背景入口；外框/按钮圆角、边框和内边距直接读取主栏单一来源。
- [x] 将 `BarSurfaceScene` 收窄为资源/拓扑/输入适配器：移除本地 hover/pressed visual state、专用 draw 和矩形按钮 hit，按钮行为委托共享 Bar 入口；PPT-only DragHandle 与 Whiteboard Freeze 保持独立语义。

主要文件：`PageControl.cppm/.cpp`、`Bar.Scene.cppm/.cpp`、Bar shared runtime、`page_control_tests.cpp`、`whiteboard_ui_tests.cpp`。

**Rollback 2**：PageControl 接入可单独回滚到共享运行时已存在、Main Bar 已迁移的状态；不得恢复分页专用按钮状态机作为兼容分支。

## 3. 输入与工作区状态机

- [x] DragHandle 改为 PPT-only 独立 shape/hit region；Whiteboard 不创建该命中，Page 永远 no-op。
- [x] 实现 Enter 起始 capture 撤销/命中关闭、DragHandle 槽位同批收拢淡出，以及 Exit 同批展开淡入并在成功稳定呈现后恢复命中。
- [x] PPT 保留 drag/scale/persist/long-press/wheel/keyboard flash；Whiteboard 只保留普通 click/tap 与标准 hover/press，拒绝 PPT 专属输入。
- [x] Whiteboard switching 只关闭 interactive，锁存未变化的 Previous/Arrow/Add/文字视觉；反向重入从当前背景、按钮、DragHandle、内容和 HWND bounds 重定向。
- [x] PPT 底栏可见时从当前实际位置同步移动/形变到 Whiteboard 固定位置；底栏不可见时直接使用最终几何渐显；Exit 返回 PPT 最新运行时位置。

主要文件：`PageControl.cppm/.cpp`、`Whiteboard.cppm/.cpp`、`Ppt.cppm/.cpp`、page/whiteboard input tests。

**Rollback 3**：保留稳定按钮和共享运行时，单独回滚工作区事务；不得重新引入双 renderer owner 或隐藏中间 owner。

## 4. PPT 碰撞与位置隔离

- [x] 从 `ResolveRuntimePageControlLayout` 及调用方删除 `mainBarObstacle` 参数、`MainBarObstacle()`、Bar HWND 查询和 Bar 布局变化通知。
- [x] Whiteboard 固定布局不再读取 `PptLayoutState`；删除 Whiteboard drag clamp、persist 和碰撞测试。
- [x] 手动拖动只约束命中 pair 与另一 pair，碰撞时保留最近 feasible candidate，不推动另一 pair。
- [x] 自动纠偏固定 bottom 优先、middle 最近位置/极端运行时缩放回退；断言输入快照和保存配置不变。

主要文件：`PageControl.cppm/.cpp`、可能的 `Bar.WindowGeometry.h`/通知调用、`page_control_tests.cpp`。

**Rollback 4**：碰撞重构独立于按钮视觉；失败时恢复 PPT-only 四控件旧求解，不得恢复 Bar 障碍。

## 5. 清理、规范与回归

- [x] 删除 PageControl 对 WidgetSpec 标准按钮 offset/size 的赋值、local hover/pressed visual state、专用 draw、矩形 button hit 和独立 `240ms` duration；保留 generic WidgetSpec 作为共享 metrics 的 surface adapter。
- [x] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md`、任务 PRD/design/implement 与代码中的稳定注释；静态确认不再存在 Whiteboard drag 或 main bar obstacle 产品合同。
- [x] 更新测试：共享 Bar metrics、SVG 语义、输入矩阵、固定 Whiteboard 位置、divider Drag 间距和 PPT-only 碰撞；既有 EndShow/A2、状态反向与窗口生命周期测试继续执行。
- [x] 回归确认 `PptExitShow` 不返回生产路径，EndShow/A2、旧 JSON、COM/WPS、`PptInfoStateBuffer`、页级墨迹与 owner/Z 序不变。

## 6. 质量门

- [x] `git diff --check`。
- [x] 静态 `rg` 确认 PageControl 不再设置标准按钮 icon/text offset，BarSurface 无本地 hover/pressed visual state，碰撞路径不读取 Bar HWND。
- [x] 使用 ARM64 版 `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，超时不少于 5 分钟。
- [x] 运行 `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`。
- [x] 记录未执行的真实 PowerPoint/WPS、鼠标/触摸、光影、DPI 与连续工作区切换设备验收；按仓库约束未启动 GUI。
