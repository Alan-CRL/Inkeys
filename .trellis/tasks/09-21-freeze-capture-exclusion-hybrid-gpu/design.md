# 定格 Magnification 排除列表修复设计

## Change Boundary

- 行为差距：过滤列表是启动时的一次性 HWND 快照，且来源混合 legacy 全局句柄与 Window Service，无法保证抓帧时仍对应当前窗口。
- 实际归属：列表生成、`MagSetWindowFilterList` 和 `MagSetWindowSource` 都位于 `IdtMagnification.cpp`，因此修复应收敛在该文件。
- 必要修改：新增局部列表构建/提交函数，并在初始化和每次抓帧前调用；如需声明仅同步 `IdtMagnification.h`。
- 明确不做：不处理双捕获、跨线程状态、显卡黑屏、后端替换、owner 链或其他窗口行为。

## Filter Set

按以下固定角色顺序从 `Window::Service::Handle()` 读取：

1. `MagnifierHost`
2. `Freeze`
3. `DrawpadPresentation`
4. `Drawpad`
5. `PptBottomLeft`
6. `PptBottomRight`
7. `PptMiddleLeft`
8. `PptMiddleRight`
9. `Bar`
10. `Setting`

固定顺序便于审查，不依赖 owner 链枚举，也不会把第三方顶层窗口误排除。

## Validation

候选 HWND 仅在满足下列条件时加入：

- 非空且 `IsWindow(hwnd)`；
- `GetWindowThreadProcessId` 返回当前进程 ID；
- `GWL_STYLE` 不含 `WS_CHILD`；
- 列表中尚无相同 HWND。

`MagnifierChild` 由 Magnification API 自身处理且是 child；`DisplayObserver` 是 message-only window，均不加入。

## Refresh Timing

- `MagnifierThread()` 完成既有创建等待后调用一次提交函数，成功后维持当前 `magnificationReady` 行为。
- `UpdateMagWindow()` 在每次 `MagSetWindowSource` 前再次调用提交函数。
- 提交失败时本次 `UpdateMagWindow()` 立即返回，避免在已知过滤状态不正确时抓取新画面。

这能覆盖 Setting 的独立生命周期、Window Service 重新创建窗口以及 legacy 镜像未及时同步，不改变其余捕获时序。

## Failure Handling

- 保留既有 `MagSetWindowFilterList` 失败诊断逻辑，抽入统一提交函数，避免初始化和更新路径复制代码。
- 空/无效候选被安全跳过；若 Magnifier Child 本身无效，提交直接失败。
- 不新增 `MagGetWindowFilterList` 回读、逐 HWND 日志或平台探测，符合本轮不扩展诊断的边界。

## Validation Boundary

自动验证可以证明列表来源、过滤规则、调用时机和构建正确，但无法证明特定 Windows/驱动实际遵守 Magnification 过滤。最终视觉行为交由用户人工测试。
