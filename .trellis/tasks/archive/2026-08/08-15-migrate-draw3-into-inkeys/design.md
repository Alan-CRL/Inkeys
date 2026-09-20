# Draw3 迁移技术设计

## 1. 边界与原则

Draw3 作为 Inkeys Drawpad 的新实现进入主进程。Window Service 唯一拥有主 `WindowRole::Drawpad` 和 presentation-only `WindowRole::DrawpadPresentation` sibling HWND、owner 层级和窗口线程；Draw3 仍只有一套 Host、墨迹输入、独立 D3D 资源、交换链、document/history 与绘制线程。

迁移采用“先完整导入、再最小适配”的方式。源仓库 `.git` 历史不直接嵌套进工作树；以来源清单和必要许可文件保留追溯性，当前工作树继续使用 InkeysRepo 的单一 `.git`。

## 2. 目录与工程集成

- Draw3 产品代码放入 `Inkeys/Inkeys/Drawing/Draw3/`，模块名保持 `Inkeys.Drawing.Draw3.*` 或使用项目既有前缀统一重命名。
- Draw3 自带的 Ink Stroke Modeler/Abseil 头文件放入 `Inkeys/additional/`；产品按平台链接源快照的固定版本 `ink_stroke_modeler_merge.lib`，不编译第三方 `.cc`。
- 着色器源码进入 Draw3 子目录；CSO 由现有工程构建步骤生成或以明确资源项登记，不依赖源仓库输出目录。
- 独立 `main.cpp`、实验窗口入口和可见测试程序不进入产品编译。
- Draw2 文件保留，但从 `ClCompile`/module 项中移至 `None`；`IdtState` 只提供兼容 facade，转发到 Draw3 bridge，不再维护 Draw2 绘制全局。

文件映射固定为 `source draw3/*.cpp|*.cppm -> Inkeys/Inkeys/Drawing/Draw3/Draw3.*`、shader/HLSL -> `Inkeys/Inkeys/Drawing/Draw3/Assets/`、`ink_stroke_modeler`/`absl` 头文件 -> `Inkeys/additional/`、固定库 -> `inkStrokeModelerTest/lib/lib{32,64,Arm64}/`；源 `.git`、`.vs`、构建输出、第三方 `.cc`、demo `main.cpp`、窗口性能 HUD 和独立测试工程均排除。产品 shader 资源 ID `301..304` 分别对应 ink pixel、ink vertex、laser update CS、laser emit CS，统一在 `Inkeys/resource.h`/`Inkeys/Inkeys.rc` 登记。

## 3. 生命周期

产品启动顺序：

1. Window Service 创建主 Drawpad 及固定 layered/transparent 的辅助呈现 HWND，并预置主 presenter 所需扩展样式。
2. `drawpad_main` 的兼容入口改为创建 Draw3 host/runtime，不再启动 Draw2 detached workers；失败时只记录错误并结束，不回退到第二个 Drawpad。
3. Draw3 host 以两个外部 HWND 初始化共享 renderer/backbuffer 的主 presenter 与辅助 ULW target；RTS 和 mailbox 仅绑定主 HWND。
4. Bar/PPT 通过桥接发布工具状态和命令，Draw3 绘制循环消费。
5. 退出时先停止命令生产与 RTS，再唤醒并结束绘制循环，随后释放 presenter/swap chain/device，最后由 Window Service 销毁 HWND。

Draw3 host 不调用 `CreateWindowEx`、`DestroyWindow` 或独立窗口线程；窗口 resize、style 与三态可见性修改通过 Window Service 或 Drawpad WndProc 所在线程执行。

窗口层级合同：`MagnifierHost -> Freeze -> {DrawpadPresentation, Drawpad}`，辅助窗低于主 Drawpad/PPT/Bar，PPT/Bar 仍 owned 到主 Drawpad。Draw3 不调用 `SetWindowPos`、不设置 topmost、不改 owner；Window Service 同步两窗 bounds，并批量保证 `Primary/Presentation/Hidden` 中至多一窗可见。

## 4. 窗口消息与命令桥

保留 `DrawpadMsgCallback` 作为 Window Service 注册的 WndProc 入口，但将其变为薄适配器：

- 生命周期和兼容消息交给现有 Window Service/产品逻辑。
- `WM_SIZE`、DPI、display/device change、pointer/cursor 和关闭唤醒转发给 Draw3 host 的线程安全 mailbox。
- 不从 Bar/UI 线程直接调用 renderer 或 D3D context。

建立一个最小 `Draw3Bridge`：读取既有 `stateMode`/颜色/宽度等产品状态，并发布 Draw3 tool/command。桥接只处理 Draw3 已实现能力；未实现入口保持隐藏。

## 5. 输入所有权

Draw3 `RealTimeStylusInput` 是 Drawpad 的唯一 RTS producer。Draw2 `IdtRts` 不参与产品编译或至少不再初始化。Draw3 RTS 绑定现有 Drawpad HWND，发布到 `ContactInputCoordinator`；绘制线程独占消费。

RTS 保持 `SetAllTabletsMode(TRUE)`，选择模式不通过丢弃 RTS contact 或输入门禁实现；鼠标消息穿透由辅助窗固定 `WS_EX_TRANSPARENT` 提供。

保留 Draw3 速度橡皮（`SpeedEraserOcController`）和固定橡皮；删除旧 Draw2 压感橡皮的宽度计算、常量、配置与 UI 入口。普通 Pen/Highlighter 的压力宽度仍按 Draw3 压力链处理，倒置笔端只映射到当前 Draw3 橡皮模式。

## 6. 图形设备与透明呈现

`GraphicsDeviceResources` 保持独立 hardware-first/WARP-fallback 的 D3D11.1 设备创建，不使用 `Inkeys.UI.RenderPipeline`。

透明呈现优先级保留为 DComp -> DWM/Win7 -> ULW：

- DComp：Drawpad 创建时预置 `WS_EX_NOREDIRECTIONBITMAP`，使用 Draw3 自有 composition swap chain、DComp device/target/visual。
- DWM2/DWM/Win7：使用 Draw3 自有 HWND swap chain 与兼容 alpha fallback。
- ULW：使用 staging texture、top-down 32-bit DIB、premultiplied alpha 和 dirty rect；透明像素不得被提升成可见不透明背景。
- 选择辅助窗：长期持有第二个 ULW readback target，但读取同一最终 backbuffer，不创建第二套 swapchain、renderer、history 或绘制线程；最终帧包含 L2、L1/L0、Laser 和粒子，选择态不合成绘制光标。
- 目标 HWND 已由 Window Service 创建，presenter 不直接重写 owner/z-order；样式切换通过 Window Service 线程执行。

输出切换采用单调 generation。隐藏目标必须先完成全量 Present；只有请求/就绪目标和 revision 一致且已呈现当前内容 revision 时才能换窗。选择无历史内容时，Laser/粒子继续在辅助窗淡出，最后强制完整 ULW Present，只有全帧 alpha 为零才进入 `Hidden`。

模式降级必须可重复初始化且可完整释放。创建期 `WS_EX_NOREDIRECTIONBITMAP` 在绑定 DComp 后可能不可清除：DComp 启动失败时，Window Service 在任何窗口显示及 Setting 初始化前停止整条隐藏窗口链，再顺序重建 legacy-compatible 主 HWND 与辅助 sibling；新 Draw3 Host 跳过 DComp 并按 DWM2 -> DWM -> ULW 回退。两个主 HWND generation 不同时存在。设备丢失时重建主 presenter 与辅助 ULW target，HWND 所有权不变化。

## 7. 历史、页面和未完成功能

首阶段使用 Draw3 当前内存文档与 GPU history/cache。仅接入已存在的 undo/redo、清屏和页面命令；保存、超级恢复、直线拉直、输入测试不在迁移中实现。产品设置仅在能力可用时显示，避免空入口进入用户路径。

## 8. 测试与可观测性

- 将纯逻辑测试并入 `InkeysHeadlessTests`，覆盖输入队列、文档/history、桥接映射、透明模式合同和 shutdown 状态机。
- 图形测试使用无可见 HWND 路径或纯资源/策略测试；不得调用可见窗口控制。
- 保留轻量诊断计数供测试断言，删除实验 HUD、独立性能窗口和仅供 demo 的调试输出。

## 9. 兼容与回滚

- 每阶段保持解决方案可构建；工程导入、Draw3 host、桥接、Draw2 隔离分别形成可检查边界。
- 若 DComp 初始化失败，必须在显示前由 Window Service 顺序重建 legacy-compatible HWND 后降级到 DWM/ULW；不得为启用 DComp 破坏 Win7 fallback。
- 回滚时可恢复 Draw2 工程登记和 `drawpad_main` 调用，导入文件本身不影响原窗口服务。

来源任务历史已移动到目标 `.trellis/tasks/archive/2026-08/draw3-source/`，并以 `active/`、`archive/2026-07/`、`archive/2026-08/` 保留源状态和月份，不覆盖目标任务。ARM64 Debug/Release 完整构建、无窗口动态测试和真实透明合成验证由阶段 5 记录。
