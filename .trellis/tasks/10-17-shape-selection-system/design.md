# 技术设计（用户已批准）

## 现状与边界
Draw3 的 InkStroke 不可变、Canvas append-only，CanvasRuntimeHistory 管理索引/显隐/Clear，CompositionRangeTree 已有 UpdateItemGeometry。UI3 颜色/宽度经 IdtState/Bridge 投影，窗口由 Window Service 管理。DrawingController 是文档与几何的 owner；保存 worker 只收快照。UInk codec 支持 type5 的 line/polyline/rectangle/square/ellipse/circle/polygon，但 Draw3 导入限制为两点线及单样式轴对齐矩形。

## 共享核心契约
共享头与实现放 inkStrokeModelerTest/draw3，纯 namespace draw3::shapes，不依赖 HWND/D3D/JSON/UInk module。ShapeObject{id[16],revision,kind,creationMode,geometry,transform,style,parts}。Geometry 使用局部参数；Style 独立 stroke/fill/opacity/width/dash；Compound 有序、部件 ID 稳定。GUID 创建通过调用方注入系统 UUID，核心不使用索引作身份。统一 Validate/BuildGeometry/HitTest/ControlPoints/BuildRenderData 接口；静态注册表冻结。缓存依赖对象修订和显示比例。曲线细分误差≤0.25px；简单凹多边形耳切，无孔洞/自交填充。通用变换保持圆的等比约束。

## 历史与持久化
InkStroke 增加 optional ShapeObject，不另建顺序容器；旧枚举不重排，增加通用 Shape 类型。原四类适配器保留旧 primitive 绘制。新对象 one item/one create commit，预览不入文档。命令保存前后快照和 expectedRevision，纯应用及 inverse 测试，Phase3 才接线交互编辑。
UInk 标准几何承担可显示降级；extra[inkeysShape] version1 携带完整身份参数/变换/分组。复合各部件共享 undoId，一个逻辑 ShapeObject。旧文件生成一次新 GUID，重存稳定。支持版本损坏/重复身份拒绝安装；未知版本保留标准外观并给诊断。白板历史读v1写v2，补 shape；Clear/redo/fallback 不丢身份。

## GPU
独立 shape VS/PS/HLSLI，不修改原 ink shader、Win7 guarded 字节。新 primitive 固定64 bytes=4 InkPoint slots，复用 t0 与 Map capability。新样式 CB 独立16字节对齐，旧 b0..b3 协议不变。线段、椭圆、圆角矩形解析覆盖，路径累计长度控制 dash，不生成 CPU 短划。三角形填充及边界AA。同材质 MAX/MIN 覆盖并集；不同材质/填充描边有序合成 Add+Retain*Below。复用 SetOperatorTarget/ClearOperatorLayer/ApplyOperatorLayers 和设备生命周期。新资源纳入现有 FXC/CSO/RC 链。

## 后续边界
创建会话 Idle→Creating→Preview→Commit/Cancel，未来逐点/多阶段支持 Back。SelectionOverlay 未来位于画布上/Bar下，与PPT同层，复用Window Service及UI3，不在本轮创建。SnapEngine仅查询；永久约束由独立依赖图维护，引用稳定对象及特征。

## 风险与回退
GPU布局、全画布与tile坐标、透明叠加、复合展开后的contentId、白板redo是重点。保留旧四形兼容路径，可撤回新增通用路径而不改变旧文件。Win7/Win10真机与笔触手感未实测须单独标记。禁止为环境错误污染源码或修改工具链。

## P2完成后的接口与后续依赖
ShapeObject增加可选Construction，ToolSpec区分11类Preset、创建模式与模板参数；Construction的起终点/边数/矩形局部圆角为真值，几何由MakePresetShape生成并校验。UInk10使用inkeysShape v2保存构造语义，兼容v1。业务新状态按版本整包发布，Down冻结，真实Up最终化；原四类兼容路径保留。UI3采用独立GeometryAttribute子树并复用既有精调/颜色。P3编辑需从Construction重新生成几何，不得只修改派生顶点；P3实现绘制提示/完成/回退/取消后，独立shape-point-tools再实现逐点折线和闭合多边形。完整P2合同与验收见对应design.md/validation.md。
