# Inkeys3-Draw3

Inkeys3-Draw3 是面向下一代 Inkeys3 的 Windows 原生墨迹渲染实验项目。当前仓库用于验证 C++20、Google Ink Stroke Modeler、D3D11/HLSL 与透明窗口呈现组合下的低延迟画笔、荧光笔和橡皮行为；它仍是原型工程，不是完整的 Inkeys3 应用。

## 当前能力

- 鼠标左键单指笔画输入，按每笔固定的工具类型处理。
- Google Ink Stroke Modeler 平滑与 Kalman 末端预测。
- 普通笔的模拟压感、实时笔锋和预测保护窗口。
- 固定宽度平头荧光笔，支持解析矩形 body、圆角 join 和确定性的短划。
- 圆角橡皮，使用覆盖率并集和 destination-out 等价操作擦除。
- `L2` 最终画布、`L1` 稳定前缀、`L0` 实时内容三层合成。
- 源码包含 DirectComposition、DWM 扩展玻璃和 UpdateLayeredWindow 透明呈现回退链。
- 脏矩形更新、窗口缩放内容保留以及硬件 D3D11 到 WARP 的设备回退。

## 运行结构

```text
Win32/HiEasyX 消息
        |
        v
WindowController
        |
        v
DrawingController
        |
        +--> ink_prediction：建模、预测、笔宽、L0/L1 几何
        |
        +--> InkRenderer：D3D11 资源、操作层、CPU/GPU 数据上传
        |
        `--> TransparentPresentationController
             DirectComposition -> DWM extended frame -> UpdateLayeredWindow
```

程序入口是 `inkStrokeModelerTest/main.cpp`。核心实现按 C++20 模块拆分在 `inkStrokeModelerTest/draw3/`：

| 模块 | 职责 |
|---|---|
| `draw3.window_control` | 窗口创建、消息转发和跨回调请求 |
| `draw3.graphics_initialization` | D3D11/DXGI 设备、适配器和 factory 初始化 |
| `draw3.ink_prediction` | 墨迹建模、预测、笔宽、荧光笔几何和脏矩形 |
| `draw3.renderer` | GPU 缓冲区、三层画布、操作层和着色器资源 |
| `draw3.transparent_presentation` | 透明呈现模式、交换链和回退路径 |
| `draw3.drawing_controller` | 单笔帧循环、分层提交、合成和呈现协调 |
| `draw3.diagnostics` | 帧耗时、HRESULT、Win32、DXGI 和 DWM 诊断 |

CPU 与 GPU 的共享结构和寄存器契约定义在 `inkStrokeModelerTest/draw3/renderer.cppm`、`inkStrokeModelerTest/draw3/renderer.cpp` 和 `inkStrokeModelerTest/ink.hlsli`。HLSL 源由 MSBuild 转换为供 FXC 使用的无 BOM 临时副本，编译后的 `.cso` 再通过资源脚本嵌入程序。

## 构建

项目当前使用：

- Visual Studio 2022 / MSVC v143
- C++20 模块
- Windows SDK `10.0.26100.0`
- Direct3D 11、DXGI、DirectComposition、DWM 和 WinMM
- Win32、x64、ARM64 的 Debug/Release 配置

在 Windows 11 ARM64 开发机上，优先从 ARM64 Native Tools 环境使用 ARM64 版 MSBuild 构建整个解决方案：

```powershell
MSBuild.exe .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

不要单独编译某个 `.cppm` 或 `.cpp` 文件；模块依赖、HLSL 临时副本和资源嵌入均由工程文件编排。仓库内的 `additional/`、`HiEasyX/` 和 `lib/` 是当前工程直接引用的第三方源码或预编译库，本项目没有包管理器安装步骤。

## 操作

| 按键 | 行为 |
|---|---|
| 鼠标左键 | 开始并完成一笔 |
| `0` / 小键盘 `0` | 清空画布 |
| `1` / 小键盘 `1` | 选择普通笔 |
| `2` / 小键盘 `2` | 选择荧光笔 |
| `3` / 小键盘 `3` | 选择橡皮 |
| `9` / 小键盘 `9` | 退出 |

当前测试参数中，普通笔基准直径为 `100px`，荧光笔和橡皮为 `50px`；这些值由 `DrawingController::DrawMouseStroke` 固定，是实验参数而非稳定产品配置。

## 仓库边界

- `inkStrokeModelerTest/`：主解决方案中的实际应用工程。
- `inkStrokeModelerTest/draw3/`：当前 Draw3 核心模块。
- `inkStrokeModelerTest/additional/`：Google Ink Stroke Modeler 与 Abseil 源码快照。
- `inkStrokeModelerTest/HiEasyX/`：窗口和消息适配代码。
- `ResTest/DirectInkPresenter/`：透明窗口呈现参考项目，不属于主解决方案。
- `main2.cpp`、`main3.cpp`、`renderer2.h`、`shader.hlsl`：工程中仅残留 `None` 引用，当前工作树中不存在对应文件；用途和弃用状态待专门任务确认。
- `.cso`、`.aps` 和各平台输出目录：生成或构建产物，不作为架构规范来源。

## 已知范围

当前仅实现鼠标单笔输入，没有 RTS、多点触摸、按 `pointerId` 管理并发笔画、正式墨迹持久化或自动化测试工程。Windows 7 SP1 + KB2670838 是 Inkeys 的正式项目级兼容目标，但当前测试程序的 DWM、透明呈现和 resize 路径尚未在该环境完成验证，不能视为已保证能力。

更详细的第一阶段历史设计与计划见 `Inkeys_D3D11_Ink_Renderer_Refactor_Plan_Phase1_Update.md`；面向后续 AI 开发与审查的约束见 `.trellis/spec/`。当阶段说明与当前源码不一致时，源码记录现有行为，阶段说明记录历史设计或计划；必须明确标记差异并等待当前需求或专门架构决定，不自动把任一方提升为最终规范。
