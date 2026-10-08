# Research: Setting 自绘标题栏拖动恢复

- Query: 核实选项窗口自绘标题栏原有拖动行为，定位当前缺口，并给出兼容 Drawpad owner 链的最小恢复方案。
- Scope: internal
- Date: 2026-09-20

## Findings

### Files found

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：Setting 的 ImGui 自绘窗口、标题栏和 Win32 WndProc。
- `Inkeys/IdtMain.cpp`：Setting HWND 创建规格，确认它仍是 `WS_POPUP | WS_CLIPCHILDREN`、`WS_EX_APPWINDOW`。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：Setting 的 owned/unowned popup、焦点、任务栏和窗口线程合同。

### Current code patterns

- `Setting.cpp:561-628` 的 `ImGuiWndProc` 处理固定尺寸、关闭、移动位置记录等消息，但没有 `WM_NCHITTEST`；未处理消息最终落到 `DefWindowProcW`（`Setting.cpp:628`）。由于窗口没有原生 caption，当前没有任何区域返回 `HTCAPTION`。
- 当前标题栏是 ImGui 自绘区域，注释说明标题行高为 `32px`，另有 `8px` 间隔（`Setting.cpp:1397`）；唯一标题栏按钮是关闭按钮，位于 `x=914..960`、`y=0..32`，尺寸和值均乘 `settingGlobalScale`（`Setting.cpp:1405-1406`）。
- Setting 创建时保持无框顶层 popup 和 app-window 扩展样式（`IdtMain.cpp:1816-1818`）。这正是需要通过命中测试模拟 caption 的场景，不能依赖 `WS_CAPTION`。
- owner 链合同要求 Setting 始终保持顶层 owned/unowned popup，并仅动态切换 Drawpad owner（`rendering-and-ui.md:1562-1563`）。返回 `HTCAPTION` 只启动该 HWND 自身的系统移动循环，不修改 owner、style、ex-style、焦点或任务栏合同，因此与新 owner 链兼容。

### Historical behavior

- `82928330`（`feat(ui): finish Fluent2 settings migration`）已使用 `WM_NCHITTEST`：按缩放后的标题高度和 `46 DIP` 按钮宽度解析命中，关闭按钮返回关闭命中，其余可拖动标题行返回 `HTCAPTION`；当时布局还排除了左侧切换控件。
- `225575d2`（`Implement client-drawn Setting title bar`）把标题栏命中按解析后的几何细分：caption 按钮、版本区、系统图标、身份区和 drag region 分别返回对应命中；其注释明确由系统非客户区移动循环提供窗口拖动/Snap。
- 这两个提交都不是当前 HEAD 的祖先，因此当前分支上不存在一个可直接认定的“删除拖动代码”的单一回归提交；它们是其他历史开发线中已验证过的行为依据。当前缺口是合流后的自绘标题栏保留了视觉层，却没有带入对应 `WM_NCHITTEST` 合同。

### Smallest safe restoration

- 仅在 `ImGuiWndProc` 增加 `WM_NCHITTEST`，把屏幕坐标转换到 Setting 客户区。
- 对 `0 <= y < 32 * settingGlobalScale` 且不在右侧关闭按钮 `x >= 914 * settingGlobalScale` 的点返回 `HTCAPTION`。
- 关闭按钮区域及全部内容区保持 `HTCLIENT`，让现有 ImGui `TitleBarClose` 继续收到鼠标消息；当前布局不应返回 `HTCLOSE`，否则会绕过现有隐藏窗口和 Bar 状态同步逻辑。
- 继续让 Win32 系统移动循环执行拖动，不实现手工 `SetWindowPos`/鼠标 capture；这样自然保留跨显示器移动、系统捕获取消和 Snap 行为，也不引入跨线程窗口写操作。
- 逻辑应直接引用现有 `settingGlobalScale` 与 `32/914` 几何常量，或提取一个无状态命中函数供隐藏 HWND/纯几何测试覆盖。至少验证：标题中央为 `HTCAPTION`，关闭按钮与内容区为 `HTCLIENT`，缩放后边界正确；owned 和 unowned 两种状态下返回值一致。

### Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:1562-1563`：Setting owner 切换与顶层可激活窗口合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1603`：Setting Window 测试范围；恢复后宜补充标题栏命中测试要求。

## Caveats / Not Found

- 历史实现来自不属于当前 HEAD 祖先链的本地提交，无法把回归归因于当前分支上的单个 commit。
- 没有执行 GUI 拖动；需要实现后通过隐藏消息测试验证命中返回值，并由人工验收实际拖动、跨屏/Snap 及绘制模式 owner 链内拖动。
