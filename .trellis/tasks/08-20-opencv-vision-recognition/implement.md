# OpenCV 视觉能力实施路线

1. 完成子任务 `08-20-opencv-vcpkg-integration`，固定依赖、features、许可声明和静态构建基线。
2. 单独规划 `InkeysCV` 公共契约与 ShapeRecognizer，包含类型、置信度、失败语义和墨迹样本测试。
3. 单独规划 DirectShow/MSMF 摄像头枚举、预览与截图，不将 `cv::VideoCapture` 泄漏到封装外。
4. 单独规划文档边缘、四角、透视矫正和增强管线。
5. 在 Windows 7 SP1 + KB2670838、Windows 8+ 和 x86/x64/ARM64 上记录真实运行验证；未执行的组合不宣称支持已验证。

每个后续阶段作为独立子任务进入规划和验收；父任务不直接承载产品代码实施。
