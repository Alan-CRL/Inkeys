# Draw3 水平矩形识别实施计划

## Ordered Checklist

1. 审计 Draw3 `InkDocument`、`InkHistory`、`InkHistoryGpu`、`DrawingController`、现有 Shape 绘制和 HeadlessTests 登记，确认所有字段/调用者与原文件编码。
2. 新增 `Inkeys.CV.ShapeRecognition` 接口与实现，完成有界采样、OpenCV hull/quad/line 分析、固定门槛和置信度；先用纯 CPU 正反样本锁定行为。
3. 扩展 `InkStroke` 与 runtime history，将 undo active 和 effective visibility 分离，实现条件尾组反向计算、状态预演及新分支条件标记清除。
4. 新增 Draw3 适配模块，收集最大连续 Pen 后缀、核对样式、逐后缀调用识别器并生成 OutlineRectangle 修正计划。
5. 扩展 GPU history transaction，使修正、Undo、Redo 和冷恢复按原稿/Shape footprint union 局部重放，并在失败时保持或恢复 CPU/GPU 一致。
6. 在 DrawingController 的最终 Stroke 提交与全接触抬起门之后接入一次性识别，复用现有 Shape shader 和样式路径。
7. 在 `Inkeys.vcxproj` 与 `InkeysHeadlessTests.vcxproj` 登记新模块/实现；不修改 HLSL、资源 ID 或参考快照。
8. 增加识别正反样本、条件历史、Undo/Redo/新分支/页面隔离及触发门测试。
9. 增加默认关闭的 Debug 数据集诊断：记录 CV 阶段指标/阈值、适配层候选边界和逐后缀结果，通过独立配置控制控制台；关闭时不构造报告或格式化输出。
10. 运行静态检查、ARM64 完整 Solution 构建和两套无窗口测试；审查 OpenCV 静态链接、输出 DLL、编码/CRLF 和变更范围。
11. 用真实数据集日志校准手绘容差：移除压力点宽样式门槛，将绝对容差改为 DIP，多尺度评估四角，并用粗糙矩形、压力变化、低采样、1x/2x DPI 等价与既有负例共同锁定阈值。
12. 扩展控制台数据集协议：增加启动/session/pen-up/candidate/stroke 标识和分笔 DIP 路径；诊断模式继续计算所有安全指标并输出完整失败集合、边覆盖缺口、残差与 Stroke 边贡献；验证可与 `-rts-trace` 同时重定向采集。

## Validation Commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
.\Build\ARM64\Debug\Inkeys.exe --draw3-hidden-test
git diff --check
```

- 用 vcpkg installed 元数据与 linker map/import 检查确认使用 `opencv_core4`、`opencv_imgproc4` 及必要 zlib 静态库。
- 递归检查最终输出中不存在 `opencv*.dll`、`opencv_world*.dll`、FFmpeg 或 OpenCV plugin DLL。
- 检查所有既有修改文件保持 UTF-8 BOM/无 BOM 与 CRLF/LF 原状；新 C++ 产品文件遵循相邻模块的 UTF-8 BOM + CRLF。
- Headless 覆盖诊断 accepted/reject reason、阈值/指标、候选前置边界和开关默认值；静态核对关闭路径传空指针且不进入 formatter。
- Headless 覆盖启动标识只输出一次、ID 单调且可关联、DIP 路径保持 Stroke 边界、首次拒绝后仍保留可计算指标，以及 `-rts-trace` 参数不与 ShapeRecognition 诊断互斥。

## Risk And Rollback Points

- History active/effective 分离是最高风险点；先完成纯 CPU 状态预演测试，再连接 GPU transaction。
- GPU union restore 任何一步失败都不得提前提交 visibility。调试时优先保留 ordered replay 兜底，不扩大 renderer API。
- 触发门只在已有状态汇合点接入；若无法证明某状态已结束，默认跳过识别。
- OpenCV 识别与 Draw3 适配保持分层，可分别回滚；不通过调整 shader 或修改 Stored point 数据来绕过失败。
