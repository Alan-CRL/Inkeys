# UI3 同窗简易颜色选择器 — Implementation Plan

## Implementation Checklist

- [x] 阅读 native-desktop 与共享思考指南，确认 UI3 设备资源、动画、输入和构建约束。
- [x] 在 `Bar.State.cppm` 增加颜色选择器跨线程状态，并在 Bar 生命周期清理路径复位。
- [x] 在 `Bar.Main.cppm` 追加第 12 色块、面板控件枚举及 `TryQueueColorPickerKeyboardInput` 导出契约。
- [x] 在 `Bar.Main.cpp` 实现预设 RGB 判定、HSV/线性混色、精确/最近点反投影和面板布局。
- [x] 实现第 12 色块、亮/暗色板、RGB 只读显示、选点、关闭按钮、保持提示和预览气泡绘制。
- [x] 将指针/键盘颜色预览固定到顶部「亮/暗色系」右侧预览槽，并清理仅用于旧跟手布局的状态。
- [x] 将顶部色预览改为面板常驻控件：展开即显示、无独立淡出，并移除预览区域内 RGB 文字与键盘 3 秒预览计时。
- [x] 加入设备级渐变缓存、generation 重建、dirty union、光源可见区域和动画调度。
- [x] 实现自定义色块与面板命中、指针/触摸捕获、越界夹紧、稳定锁色和松手提交。
- [x] 实现 Bar 键盘消息排队与 WASD/方向键状态机；在 `IdtDrawpad.cpp` 钩子中前置接入并保护旧行为。
- [x] 核查折叠、工具失效、捕获丢失、退出及设备丢失清理路径。

## Validation

- [x] 运行 `git diff --check` 并检查修改范围、文件编码和 CRLF。
- [ ] 依据 PRD 手工走查预设/自定义状态、面板关闭语义、亮暗端点、指针/触摸、保持锁色、键盘重复和 3 秒预览。
- [ ] 静止打开 30 秒检查无持续渲染；拖动期间检查无逐帧渐变/位图分配；触发 device generation 重建后复测。
- [x] 使用 ARM64 Host MSBuild 构建完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时至少 300 秒。
- [x] 执行 Trellis 全量检查，确认代码复用、跨层输入流与既有 UI3 光影/渲染租约无回归。

> 手工 UI 走查、30 秒静止观察与 device generation 切换需在可交互运行环境中完成，暂保留未勾选。

## Review Gates

1. 状态与输入：所有 `SetPenColor` 都来自 Bar 交互线程，连续变化与最终提交语义正确。
2. 渲染与性能：缓存资源生命周期正确，静止选择器不维持渲染，动画关闭时可立即收敛。
3. 兼容性：面板关闭时键盘路径不变，旧 UI2 和现有 11 个预设色逻辑未改写。

## Rollback Points

- 键盘钩子接入可独立回滚，选择器仍保留指针功能。
- 面板交互可与第 12 色块显示独立回滚。
- 渐变和预览资源均为无持久状态的设备资源，可整体移除而不涉及数据迁移。
