# OpenCV 视觉模块与图形识别

## Goal

为 Inkeys 建立可持续演进的 OpenCV 视觉能力：先完成稳定、静态、跨 x86/x64/ARM64 的依赖接入，再通过 Inkeys 自有数据结构承载墨迹图形识别、摄像头展台和文档扫描能力。

## Requirements

- R1：OpenCV 作为根 `vcpkg.json` 中的项目级固定依赖，复用现有三套 v143 静态 triplet。
- R2：未来视觉代码收口到 `InkeysCV` 边界；`Draw3`、UI、Document 和 Renderer 不得直接暴露 `cv::Mat`、`cv::Point` 或包含 `opencv2/opencv.hpp`。
- R3：墨迹图形识别覆盖 Circle、Ellipse、Rectangle 和 Triangle，输入输出均使用 Inkeys 自有类型，返回图形类型与置信度。
- R4：后续摄像头能力覆盖设备枚举、实时预览和截图；文档扫描覆盖纸张边缘、四角、透视矫正与图像增强。
- R5：Windows 7 SP1 + KB2670838 使用 DirectShow，Windows 8 及以上优先验证 MSMF；不依赖 WinRT Ink、Windows ML 或 Win10-only API。
- R6：第一阶段使用官方 `opencv4` port，不修改 `Vcpkg/`、不引入 overlay-port 或 `BUILD_LIST`；只有实测体积超标时才另立任务评估模块精简。
- R7：OpenCV 的 Apache License 2.0 声明与中英文文档保持一致，复用现有许可证文件。

## Task Map

- `08-20-opencv-vcpkg-integration`：当前阶段，完成 vcpkg 静态依赖、第三方声明与 ARM64 构建。
- `08-20-draw3-axis-aligned-rectangle-recognition`：当前实施阶段，建立 OpenCV 隔离识别层、Draw3 水平矩形修正与条件渲染历史语义。
- 后续子任务：`InkeysCV` 契约与 ShapeRecognizer、摄像头采集、文档扫描/图像处理、Windows 版本与设备验证。

## Acceptance Criteria

- [x] OpenCV 依赖接入子任务已完成并有可重复的构建记录。
- [ ] 视觉业务只通过 Inkeys 自有契约对其他层暴露。
- [ ] 四类墨迹图形有自动化样本与置信度验收。
- [ ] DirectShow/MSMF 、文档扫描和图像处理在各自子任务中有设备/系统验证记录。
- [ ] 最终产物不依赖 `opencv_world.dll`、FFmpeg DLL 或 OpenCV 插件 DLL。

## Current Scope Boundary

当前实施 `draw3-axis-aligned-rectangle-recognition` 子任务，只建立水平/竖直矩形识别与 Draw3 条件渲染闭环；倾斜矩形、其他图形、摄像头运行时和扫描 UI 仍延后。
