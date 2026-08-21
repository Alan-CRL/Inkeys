# Draw3 水平矩形识别 MVP

## Goal

为 Draw3 增加保守的水平/竖直矩形自动修正：用户完成最多六条连续普通画笔墨迹并全部抬起后，系统只在高置信度时隐藏原稿并绘制同样式的标准矩形，同时用可序列化的条件渲染语义保证撤回可恢复原稿。

## Background

- OpenCV 已通过父任务的 `opencv-vcpkg-integration` 子任务以静态依赖接入。
- Draw3 已有 `OutlineRectangle` 图形路径、append-only Stroke/RenderItem、每页 runtime history、热前像、composition cache、ordered replay 和隐藏窗口测试。
- 本任务以 `draw` 为基线，在 `feature/recognition` 分支实施；现有 OpenCV 子任务保持未归档状态。

## Requirements

- R1：新增 C++20 模块 `Inkeys.CV.ShapeRecognition`；导出 `ShapeType { Unknown, AxisAlignedRectangle }`、自有点/Stroke view/轴对齐矩形和 `ShapeResult`，以及 `RecognizeInkShape(strokes, dpiScale) noexcept`。模块接口不得出现 `cv::` 类型，OpenCV 头文件只允许存在于实现单元。
- R2：新增 `Inkeys.Drawing.Draw3.shape_recognition` 适配模块，负责从当前 Canvas 收集以最新笔画结尾的候选、核对样式并生成修正计划；Draw3 不直接 include OpenCV。
- R3：触发只发生在最后一个物理绘制 contact、手势手指、重连候选和活动导航全部结束，且最终 Pen Stroke 已提交后；同一轮全抬起只识别一次。
- R4：候选由最近最多 6 条连续、样式一致、有效且未撤回的 Pen 墨迹构成，必须包含最新笔画。遇到 Shape、Eraser、Highlighter、隐藏分支或样式变化即截断；从最大后缀向小后缀尝试。
- R5：支持 1 至 6 笔、任意边顺序和方向、单笔闭合、多笔拼接及重复描边；结果仅限水平/竖直 Outline Rectangle。
- R6：识别总采样最多 4096 点、每笔最多 1024 点，并使用 OpenCV `convexHull`、`approxPolyDP` 和 `fitLine`。非法坐标、非法 DPI、超限输入或 OpenCV 异常必须返回 Unknown。
- R7：保守门槛固定为：短边至少 `max(32 DIP, 8 × 中位笔宽)`；长宽比不超过 `6:1`；凸包矩形度至少 `0.88`；以周长 2% 简化后恰好四角；长线段最大偏轴 `15°`、加权平均不超过 `8°`；每边覆盖至少 `80%`、总周长覆盖至少 `88%`；非边线长度不超过 `12%`；每条输入笔画至少 `80%` 落在边带；总长度为周长的 `0.8–2.5` 倍；综合置信度至少 `0.82`。
- R8：置信度综合矩形度、最差边覆盖、轴向、直线度、四角闭合和离边比例。低于阈值、与负类冲突或没有覆盖最新笔画时不得替换。
- R9：识别结果沿用候选当前产品画笔的颜色、透明度和粗细，复用现有圆角 `OutlineRectangle` shader 路径，不修改 HLSL。
- R10：`InkStroke` 增加 `renderOnlyWhenLatest` 元数据；它与 undo 状态分离。History 必须分别维护未撤回状态与实际参与合成状态，并按 `.uink` 末尾条件组语义反向计算有效可见性。
- R11：修正时原始 Pen Stroke 设置条件标记、修正 Shape 不设置标记，因此结果存在时隐藏原稿；撤回 Shape 一次恢复全部原稿，随后原稿仍按原笔画逐笔撤回；Redo 对称恢复 Shape 并隐藏原稿。
- R12：撤回修正结果后开始新分支时，先清除已恢复原稿的条件标记，再丢弃 Redo，确保新普通内容不会再次隐藏原稿。
- R13：修正结果的 footprint、dirty rect、热前像、局部 composition restore 和 ordered replay 覆盖原稿与矩形 Tile 的并集。GPU 修正失败保留原稿；可能部分改写 L2 时恢复受影响 Tile，恢复失败则请求权威刷新。
- R14：页面切换、Clear、Undo/Redo 和历史缓存继续遵守现有每页隔离与绘制线程所有权，不新增窗口线程 D3D 操作。

## Acceptance Criteria

- [ ] 标准四笔、单笔闭合、反向/乱序、轻微抖动、断角容差和重复边样本能识别为水平矩形，并生成同色、同透明度、同粗细的 `OutlineRectangle`。
- [ ] 小图形、开放 U/C、圆/椭圆、三角形、平行四边形、倾斜矩形、X、缺边、过大转角、内部乱画及最新无关笔画均不替换。
- [ ] 仍有任一绘制 contact、手势手指、重连候选或活动导航时不识别；最后全部抬起后仅执行一次。
- [ ] 修正、Undo、原稿逐笔 Undo、Redo、撤回后新分支、页面隔离和条件组 Tile 可见性均有自动化覆盖。
- [ ] OpenCV 类型不出现在导出接口或 Draw3 源码；产品与测试工程只登记任务所需的新 module/实现单元。
- [ ] `InkeysRepo.sln Debug|ARM64` 使用 ARM64 Host MSBuild 构建通过，`InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test` 通过。
- [ ] 最终链接只使用静态 OpenCV；输出目录不存在 OpenCV/world/FFmpeg/插件 DLL。
- [ ] `git diff --check`、变更范围和原文件 BOM/CRLF 检查通过；不启动可见窗口、不创建 commit、不归档任务。

## Out Of Scope

- 倾斜矩形、圆、椭圆、三角形、平行四边形及多分类竞争。
- `.uink` 文件读写、摄像头、扫描、识别开关、停笔手势和任何可见 UI。
- 修改 OpenCV vcpkg 配置、overlay-port、HLSL 或参考快照 `inkStrokeModelerTest/`。
