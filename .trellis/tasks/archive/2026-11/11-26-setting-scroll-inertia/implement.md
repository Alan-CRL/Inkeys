# 实施计划：设置页面滑动封装与触摸/笔惯性

前置：`11-26-setting-pointer-route` 已完成并提交（需要 `io.MouseSource` 与 `PointerInput::ReadTrack`）。

## 步骤

0. **研究**（不改代码）：完成 design §6 三项，结论写入 `research/`，据此确定 `InertiaParams` 初值与 panning feedback 隔离方案；如结论改变设计，先更新 design.md 再继续。
1. **搬移封装**（提交①，行为不变）
   - 新增 `Inkeys/Inkeys/UI/Setting/Setting.Scroll.h/.cpp`，把 `DragEligible`、`WindowStateId`（如需共享则内联到头文件）、`CaptureDrag`、`BeginDragScrolling`、`ApplyDragScrolling`、`AllowContentDrag` 主体搬入；`Design::` 原入口改为转发。
   - `ScrollbarStyleScope`、`BeginScrollChild(resetToTop)` 改为调用 `Scroll::ConsumePendingScroll/ResetWindow`。
   - `Inkeys.vcxproj` / `.filters` 加入新文件。
   - 构建 + `setting_design_tests` 相关用例；实机鼠标/触摸拖动滚动与改动前一致。
2. **鼠标屏蔽 + 惯性**（提交②）
   - `Scroll` 纯逻辑：`VelocityEstimator`、`InertiaCurve`、`ClampWithOverpan`、`InertiaParams` 常量。
   - 宿主：鼠标不 `CaptureDrag`；接入轨迹读取与帧回退采样；抬起启动惯性；停止条件表（design §4.5）；`Animating()` 驱动持续请求帧（在 `Setting.cpp` 帧末现有请求处接入）。
   - `InkeysHeadlessTests` 新增 `setting_scroll_inertia_tests.cpp`：拟合速度、停住判定、上下限、解析衰减与帧率无关（不同步长积分到同一时刻位置一致）、停止阈值、越界阻尼与上限、方向符号。
3. **边界窗口位移**（提交③）
   - 动态获取 uxtheme panning feedback；渲染线程原子邮箱 + 私有消息；`ImGuiWndProc` 在窗口线程调用 Begin/Update/End。
   - 按研究结论隔离副作用（位置保存、交互状态、外置缩放边框）。
   - API 缺失时只硬停。
4. `git diff` 检查 BOM + CRLF、无无关改动。

## 验证

- `InkeysHeadlessTests`：新增惯性测试 + `setting_design_tests` 中滚动/导航相关用例（先只跑相关函数缩短时长）。
- ARM64 原生 MSBuild 构建 `InkeysRepo.sln` Debug|ARM64（≥ 5 分钟超时）。
- 实机（用户确认）：PRD 验收清单全部项，尤其惯性手感与边界窗口位移；参数按反馈调整后重测。

## 审查关口

- 提交①：diff 只能是搬移与转发，`StateStorage` 键名与时序不变。
- 提交②：鼠标路径无惯性、无拖动；惯性中点按吞点击规则正确。
- 提交③：回弹后窗口位置不漂移、配置不写入异常位置。

## 回滚点

- ①②③ 各自独立提交；③可单独回滚为仅硬停。
