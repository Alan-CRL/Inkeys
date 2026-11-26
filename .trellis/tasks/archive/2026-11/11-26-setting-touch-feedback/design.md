# 设计：设置窗口禁用触摸右键与点击反馈

## 现状

- 设置窗口 `WindowSpec`（`Inkeys/IdtMain.cpp` 约 3882 行）：`bindMessages=false`，没有 `created` 回调，未 `RegisterTouchWindow`。
- 窗口过程 `ImGuiWndProc`（`Inkeys/Inkeys/UI/Setting/Setting.cpp` 约 1580 行）把鼠标/键盘交给 `ImGui_ImplWin32_WndProcHandlerEx`，其余交 `DefWindowProcW`；系统长按右键由 `DefWindowProc` 处理 Pointer/Tablet 手势时派生。
- 仓库已有同类先例：
  - `SelectionOverlay.cpp:231`：`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回 `TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK | TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS`。
  - `Draw3.WindowControl.cpp:70`：画布同类 flags（额外含多点与 Touch UI 强制关闭，设置窗口不需要）。
- 仓库内尚无 `SetWindowFeedbackSetting` 调用。

## 方案

两层开关，均只作用于设置 HWND：

1. **Tablet 手势状态（Win7+）**：`ImGuiWndProc` 新增 `case WM_TABLET_QUERYSYSTEMGESTURESTATUS`，返回
   `TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK | TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS`，与 SelectionOverlay 一致。该消息在接触开始时按点查询，命中非客户区时系统仍会询问同一窗口；返回值只关闭长按右键/反馈/flick，不影响标题栏拖动与按钮（它们走 NCHITTEST，不依赖 press-and-hold）。
2. **窗口反馈设置（Win8+）**：创建后对设置 HWND 调用 `SetWindowFeedbackSetting(hwnd, type, 0, sizeof(BOOL), &false)`，类型：
   `FEEDBACK_TOUCH_CONTACTVISUALIZATION`、`FEEDBACK_TOUCH_TAP`、`FEEDBACK_TOUCH_DOUBLETAP`、`FEEDBACK_TOUCH_PRESSANDHOLD`、`FEEDBACK_TOUCH_RIGHTTAP`、`FEEDBACK_PEN_BARRELVISUALIZATION`、`FEEDBACK_PEN_TAP`、`FEEDBACK_PEN_DOUBLETAP`、`FEEDBACK_PEN_PRESSANDHOLD`、`FEEDBACK_PEN_RIGHTTAP`、`FEEDBACK_GESTURE_PRESSANDTAP`。
   - 通过 `GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowFeedbackSetting")` 动态获取；Win7 不存在时跳过。枚举值用 SDK 头即可（只是整数，Win7 运行不受影响）。
   - 单项失败只忽略，不影响窗口创建。
   - 是否关闭 `CONTACTVISUALIZATION`（手指接触圆点）：主栏观感为“无反馈”，默认一并关闭；如实机发现主栏仍有接触圆点而用户希望保留，再删掉这一项。

### 落点

- 在 `Setting.cpp` 匿名命名空间新增 `ConfigureNativeTouchFeedback(HWND)`，由 `ImGuiWndProc` 的 `WM_CREATE` 调用（Window Service 以 `spec.windowProc` 注册窗口类，`WM_CREATE` 必经此过程）。
- 实施时由原计划的 `WindowSpec.created` 回调改为 `WM_CREATE`：不需要导出函数、不改 `Setting.cppm`（该文件无 BOM，避免整文件编码变化）与 `IdtMain.cpp`，改动只在一个文件。
- 设置窗口每次重建都会收到 `WM_CREATE`，无需在显示时重复设置。

## 与 pointer-route 的关系

pointer-route 落地后，接触阶段 `WM_POINTER*` 由设置路由消费并 `return 0`，Win8+ 上系统本就不再派生长按右键；本子任务的两层开关仍保留，作为 Win7 回退、Pointer 查询失败时交 `DefWindowProc` 的兜底，以及视觉反馈的唯一关闭手段。

## 风险与回滚

- 风险低：只返回窗口查询值与设置窗口属性。回滚即删除新增 case 与 `created` 接线。
- 若 `TABLET_DISABLE_FLICKS` 影响某些笔滚动手势，实机发现后可单独去掉该位。
