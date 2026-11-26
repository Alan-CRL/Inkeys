# 调研：现成 ImGui 惯性滚动/越界回弹库（2026-11-26）

目的：实施前确认能否直接引入现成库，减少惯性滑动（含越界回弹）的实现工作量。

## 本地

- `Inkeys/additional/imgui`（1.92.7）、`Inkeys/additional/imfluent`：无 kinetic/inertia/overscroll 实现。
- ImGui 上游无内建惯性滚动；`ocornut/imgui#3379`（拖动滚动）、`#2675`（惯性滚动）为长期开放讨论。

## 候选

| 候选 | 形态 | 结论 |
| --- | --- | --- |
| `pthom/imgui_touch`（MIT，v0.1，2026-11 新建） | 独立层，`ImGuiTouch::Setup()` 后经 context hook 每帧运行；swipe 滚动 + 惯性 + 内容橡皮筋回弹，另含长按右键、捏合、细滚动条、手机键盘 | 最接近，但不宜直接引入（见下） |
| `dkosmari` 的 ImGui fork（基于 1.92.6，#3379 中提及） | 修改 ImGui 本体的拖动滚动 + kinetic | 需替换 ImGui 本体；本仓库 ImGui 有本地扩展（如 `imgui_offset_rect.h`），不可行 |
| `#2675` 片段 | 改 `UpdateMouseInputs()`，每帧固定 ×0.95 衰减 | 帧率相关，仅示例 |
| `ChristianHohlfeld/imgui-direct` PR #6 | 帧率无关的 touch flick | 绑定其自身框架；已知问题：`IsAnyItemActive()` 时不能拖，Selectable 列表无法拖动滚动 |
| egui PR #8662 | 非 ImGui；指数衰减（UIScrollView 0.998/ms）逐帧精确积分 | 仅作模型参考 |

## imgui_touch 细查（读源码 `imgui_touch_scroll.cpp` 约 40 KB）

不宜直接引入的原因：

1. **版本**：声明要求 Dear ImGui 1.93+；本仓库 1.92.7。所需 `ImGuiConfigFlags_IsTouchScreen`、`ImGuiContextHookType_RenderPost` 在 1.92.7 已存在，可能小改即可编译，但未验证；滑块按轴自动接管依赖 Dear ImGui Bundle fork 的 `ItemDragAxisCallback`。
2. **按下暂扣再重放**：每个触摸按下都先暂扣，判定为点按后再重放给控件（默认 `HoldSeconds=0.15`）。这会改变设置里所有控件的按压时序，并与现有机制正面冲突：`BeginDragScrolling/ApplyDragScrolling` 的单宿主捕获、`##content-drag-button` 内容型按钮越阈值转滚动、`DragEligible` 的导航/浮层屏蔽、组件编辑器 `DragSession` 与 0.45 s 长按抓起。引入它等于移除现有拖动引擎并重新适配组件页。
3. **只认手指**：`ScrollMode_Auto` 只对 `ImGuiMouseSource_TouchScreen` 滑动；笔不滑动。`ScrollMode_Always` 又会让鼠标也能拖动，与“屏蔽鼠标滑动”相反，须改源码。
4. **越界方式不符**：回弹是内容橡皮筋，在 `ImGui::Render()` 之后平移该窗口的顶点与裁剪框；没有关闭开关。用户已决定“内容硬停 + 资源管理器式窗口位移”，且顶点平移可能与设置页的 DX11 离屏退场组、`PageBlockScope` 顶点处理冲突。
5. **默认长按即右键**（`LongPressIsRightClick=true`，可关），与本任务目标相反，须配置。
6. **成熟度**：v0.1、刚发布数天、API 声明可能变动、无外部使用记录。

可借鉴的部分（按 MIT 自行重写，不拷贝代码）：

- 惯性：抬起后 `speed *= exp(-INERTIA_DECAY·dt)`，`INERTIA_DECAY=2.0/s`（iOS 风格，每秒保留约 13.5%）；WinUI3 默认 0.95 衰减率相当于 `ln 20 ≈ 3.0/s`（每秒保留 5%，停得更快）。两者都是指数衰减，可做成同一常量实测选择。
- 释放速度：抬起前 `FLICK_WINDOW=0.05 s` 内的位移/时间；最小速度 `3 em/s`（低于即不启动/结束）。
- “点按停住”：按下时持有一个哨兵 ActiveId，下方控件拿不到这次按下（与现有 `##scrolldraggingoverlay` 捕获同一思路）。
- 回弹弹簧：临界阻尼 `ω=12/s`（约 0.4 s 收敛）。我们走系统 panning feedback，`EndPanningFeedback(TRUE)` 自带回弹动画，一般用不上；若研究发现系统动画不可用，可用它驱动窗口位移回位。
- 速度采样直接用每帧 `io.MousePos` 与 `g.Time`，没有输入时间戳也能得到可用手感。

## 结论与对设计的影响

- **不引入外部库**。现有拖动引擎已经解决了最难的部分（单宿主捕获、点按与拖动区分、内容型按钮转滚动、导航/浮层屏蔽、组件页长按协作），惯性只需在其上增加“释放速度估计 + 指数衰减推进”，复用现有 `##drag-scroll-position/frame` 写回路径；工作量小于引入并改造 imgui_touch。
- **用户 2026-11-26 决定：按原路径，保留 pointer-route 的 R6 样本轨迹**，不采用下面的简化；以下仅保留为备选。
- 备选简化（未采用）：参照 imgui_touch，速度用每帧 `io.MousePos` 采样（50–100 ms 窗口）即可。这样 scroll-inertia 不再依赖 pointer-route 的 R6 样本轨迹，pointer-route 可删去 R6，两个子任务解耦，pointer-route 范围更小。代价是高刷/低帧率时估速精度略降，实机手感若不足再补轨迹。
