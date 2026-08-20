# OpenCV 视觉能力设计

## Architecture

- 依赖层：根 manifest 固定 OpenCV，三套现有 triplet 提供静态 CRT/静态库。
- 封装层：未来建立 `InkeysCV`，将 OpenCV 头文件、对象和异常限制在实现内部。
- 能力层：ShapeRecognizer、CameraCapture、DocumentScanner 和 ImageProcessor 只交换 Inkeys 数据结构。
- 业务层：Draw3/UI/Document/Renderer 依赖 InkeysCV 契约，不依赖 OpenCV ABI。

## Compatibility

- Windows 7 路径使用 DirectShow，Windows 8+ 路径可使用 MSMF。
- OpenCV 与 CRT 都静态链接；不启用 world、FFmpeg、GUI、DNN 或外部并行框架。
- 当前先接受官方 port 生成的额外静态模块归档；这不等于未使用对象会进入最终 EXE。

## Delivery Order

1. 完成 vcpkg 依赖与许可声明。
2. 设计 Inkeys 自有图像/墨迹数据契约并实现 ShapeRecognizer。
3. 实现 CameraCapture 与系统后端选择。
4. 实现文档边缘、透视矫正和图像增强。
5. 执行 Windows/架构/设备矩阵验证，再根据体积数据决定是否维护 overlay-port。
