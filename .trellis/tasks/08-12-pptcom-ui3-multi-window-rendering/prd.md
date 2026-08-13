# PptCOM UI3 五窗口与共享串行渲染

## Goal

将 `IdtPlug-in.cpp` 中依赖全屏 `ppt_window` 的五个 PptCOM 控件迁移到 UI3 窗口与渲染体系，在不改变现有 PowerPoint/WPS 操作体验和配置兼容性的前提下，避免 PPT 控件窗口遮挡未来的 flip-mode 绘制窗口，并建立可扩展到 Settings UI 的共享串行渲染机制。

## Background

- 当前 PPT 控件共同绘制在覆盖屏幕的 layered `ppt_window` 中，窗口覆盖范围与实际控件范围不一致。
- Bar UI3 已具有独立 layered HWND、D2D 1.1 渲染和脏区机制；本任务以另一线程完成后的 Bar 最终代码为集成基线。
- UI3 当前只需调度 Bar 与五个 PPT 窗口；Settings 和 draw3 flip-model 后续接入，但接口需要允许增加客户端。

## Requirements

- R1：删除生产路径中的全屏 `ppt_window` 和 `WindowRole::PptControls`，创建五个独立 layered HWND：底部左翻页、底部右翻页、中部左翻页、中部右翻页、底部中间结束放映。
- R2：五个 PPT HWND 与 Bar HWND 都由 `Drawpad` 直接拥有，均为不激活的 owned popup 并位于画布上方；Bar 始终高于五个 PPT 窗口，最近交互的 PPT 窗口位于其余 PPT 窗口之上但不得越过 Bar。
- R3：PPT 窗口使用固定 ULW backing 尺寸；普通动画和拖动只更新窗口位置及内容，仅在 DPI、显示缩放或真实控件尺寸改变时重建目标资源。
- R4：六个窗口共享同一个 D3D11/D2D 1.1 device，且每个窗口拥有独立的 device context、target bitmap、GDI interop 和 ULW 提交资源；全部绘制在同一个 UI3 渲染线程中串行执行。
- R5：每个客户端具有独立请求位，状态变化只标记受影响窗口并唤醒一个共享等待对象；每帧仅计算、更新和呈现已请求、仍在动画或等待失败重试的窗口，全部 idle 后统一休眠，整体帧率上限为 60 FPS。
- R6：将 PPT UI 状态、布局、绘制和输入从 `IdtPlug-in.cpp` 迁入 UI3 PPT 模块；插件层保留 COM、放映业务命令和窄状态桥。
- R7：保留当前浅色视觉、尺寸、颜色、图标和动画；保留鼠标、触摸、滚轮、400 ms 长按连续翻页、页码点击、结束放映确认、绘制模式处理、拖动、边界吸附、五控件排斥和位置记忆。
- R8：底部左右及中部左右继续共享各自的成对位置配置；拖动一侧时同时、且仅请求对应的两个窗口。
- R9：`PptInfoStateBuffer` 更新只请求四个页码窗口；Settings 配置写入精确请求受影响窗口；全局鼠标钩子直接写入 PPT 交互队列；放大镜排除列表包含五个 PPT HWND。
- R10：保持 `pptcom_configuration.json` 字段及兼容行为、PptCOM C# ABI、COM 服务和页级墨迹逻辑不变。
- R11：每个 PPT 窗口支持红色活动脏区、绿色 idle 前最终脏区和蓝色 HWND 边界诊断，不显示 FPS。
- R12：共享调度器必须预留未来 Settings 客户端，Bar 直移等局部暂停不能阻塞或饿死其他客户端。

## Acceptance Criteria

- [ ] 运行时不再创建或使用全屏 `ppt_window`，五个 PPT 控件各自显示在独立 HWND 中且 owner、可见性、销毁顺序正确。
- [ ] Bar 固定处于 PPT 窗口上方；PPT 窗口交互前置不越过 Bar；所有六窗都处于 Drawpad 上方且不夺取激活状态。
- [ ] 单一窗口状态变化只触发该窗口的计算、更新和 ULW 呈现；成对位置及共享页码状态只标记明确关联窗口。
- [ ] 多窗口请求无丢失且按固定顺序串行执行；动画窗口续帧、局部失败重试、共享设备重建和全部 idle 休眠行为可测试。
- [ ] 全局调度保持最多 60 FPS，Bar 的暂停或直移不会阻塞 PPT 控件更新。
- [ ] 五窗完整保留现有 PowerPoint/WPS 翻页、长按、拖动、吸附、排斥、结束放映和配置行为。
- [ ] 五窗可分别显示红、绿、蓝诊断框且不出现 FPS 文本。
- [ ] Headless 测试覆盖调度、窗口层级、几何与脏区事务；PowerPoint/WPS 关键路径完成手工验收。
- [ ] `InkeysHeadlessTests` 通过；ARM64 host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug|ARM64` 通过。

## Out of Scope

- 不迁移 Settings UI，不改造 draw3 flip-model 绘制窗口；仅预留未来调度接入接口。
- 不清理在工程中以 `None` 保留的历史 `IdtWindow.cpp/.h` 和 `IdtFloating.cpp`。
- 不重新设计 PPT 控件视觉，不迁移配置格式，不改变 PptCOM ABI、COM 协议或页级墨迹业务。
- 不覆盖或回退其他线程已经完成或仍保留的用户改动。

## Constraints

- 实施必须以 `08-12-ui3-draw3-win32-himsg` 和后续 Bar 动态 HWND/脏区提交后的代码为基线。
- 修改保持最小范围，并保留原文件编码与 CRLF 换行；关键迁移和并发边界使用简短中文注释。
- 完整构建必须使用 ARM64 MSBuild，目标为 `InkeysRepo.sln` 的 `Debug|ARM64`，超时至少五分钟。
