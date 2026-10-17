# 参数化图形基础与通用渲染

【源码直接确认；2026-10-17】本轮只提供内部能力，不新增UI按钮、选择窗口或基础图形用户创建入口。

## 模型与所有权
- 共享核心为 inkStrokeModelerTest/draw3/shape_core.h/.cpp，namespace draw3::shapes，不依赖Windows、D3D或UInk。ShapeObject的16字节ID与revision独立于contentId、undoId、历史索引。
- Kind、CreationMode、ShapeStyle独立；Compound为有序部件。冻结Registry统一Validate/BuildGeometry/BuildRenderData/HitTest/ControlPoints。
- 生产InkStroke持有optional ShapeObject；通用StoredInkType::Shape=7，旧0..6数值保留。不可变GeometryResult随最终项缓存，普通笔不走图形生成。
- InkCanvas::AppendShape仅接收Create命令，经纯ApplyCommand校验后追加一个逻辑历史项。编辑命令的用户交互/运行期历史接线留Phase3。
- 旧四形适配保留兼容渲染；所附Shape必须与旧端点/样式一致。UUID失败拒绝本次创建。超出共享核心可表示范围的旧有限坐标仍可沿原有无参数fallback读取，不冒称完整参数化编辑。
- 几何像素单位沿现有画布约定；曲线屏幕误差≤0.25px。圆只接受等比几何变换。简单凹多边形支持，无孔洞/自交填充；椭圆描边采用误差受控路径避免细长端部近似距离失真。
- 兼容例外：旧矩形Geometry.radius保存名义4 DIP，BuildGeometry按BuildOptions.displayScale转换并钳制半短边；通用矩形radius是画布像素。调用旧矩形命中/控制点查询时须传当前DPI比例。

## GPU合同
- 新shapeVertexShader/shapePixelShader/shape.hlsli独立于原ink与Win7 guarded程序。资源ID307/308；沿现有FXC临时副本/CSO/RC链。
- GpuPrimitive为64 bytes，align16，占t0中四个16-byte InkPoint槽。p0/p1/p2/p3位于0/8/16/24，pathStart/pathLength位于32/36，halfWidth/radius位于40/44，kind/flags位于48/52，padding位于56。所有偏移有C++静态断言。
- 独立b4=64 bytes，screen+bufferOffset+pass为首16bytes，color偏移16，origin/dashOffset/dashKind偏移32，rect偏移48。原b0..b3与旧32-byte primitive保持不变。
- 每材质MAX/MIN覆盖并集后有序叠加Add+Retain*Below，不对不同颜色直接MAX。临时材质层和对象聚合层在设备/尺寸变化时释放重建；同步失败前不改调用方目标。
- renderer按id/revision/displayScale缓存派生RenderData，最多128项。几何或样式变化必须增加revision；临时绑定退出时解除。NO_OVERWRITE能力不足逐批DISCARD且偏移归零。

## UInk与冷历史
- 继续UInk10标准Shape(type5)，线/折线/矩形/圆/椭圆/多边形优先标准表示，旋转使用弧度；非正交变换可标准多边形降级。
- extra.inkeysShape version1保存精确身份、revision、kind、mode、geometry、transform、style及parts。复合首部件持完整根，后续只允许严格六字段version/id/revision/reference/partIndex/partCount；wire contentId连续、共同undoId，恢复为单逻辑项。
- 支持版损坏、重复ID、引用不一致、缺部件拒绝恢复；未知版本恢复标准几何并返回诊断。旧无元数据文件首次导入生成UUID，后续保存稳定。
- 桌面/PPT/白板快照均复制Shape；白板历史写v2读v1/v2，隐藏redo、Clear fallback/continuation保留精确身份。只在候选完整验证后安装。

## 验证边界
内部--draw3-shape-core-test验证共享核心及生产文档/白板历史；--draw3-shape-renderer-test使用正式renderer、嵌入资源及离屏纹理，不创建HWND。共享测试--shape-core-only、--uink-file-only覆盖模型、协议与文件事务。完整构建和真机验证状态见本轮任务validation.md，Win11离屏测试不替代Win7/Win10实机。
