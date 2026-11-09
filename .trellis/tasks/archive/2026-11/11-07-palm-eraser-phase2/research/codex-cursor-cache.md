## 事实

只读完成，未修改、构建或启动 GUI。命令行子进程被环境拒绝，`get_context.py` 未运行，已直接读取规范。**分支引用实际为 `a2785d6c`，非指定 `babdecf6`**；以下按当前工作区取证。

- **渲染链**：Appearance→主光标 resolver／逐 Touch runtime visual→RendererPrimitives→VS quad→PS。type 4/5/6 为圆／矩形／抓手圆，21/22 为笔／箭头；阶段 A 的 **23 是擦除几何，不是光标**，当前动态光标仍写成等宽高圆。[Controller:14414](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14414)、[Touch:14504](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14504)、[PS:362](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:362)、[23:538](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:538)。
- **没有光标栅格缓存**。每次实际合成均绘制全部当前 visual：N 枚对应 N 次参数上传、N 次 CB Map、N 次 Draw；通常共 **2N 次 Map**，DEFAULT 上传路径为 N 次 UpdateSubresource＋N 次 Map。每枚上传32字节点数据、48字节常量。[Primitives:339](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp:339)、[上传:36](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:36)。
- Hover 移动、尺寸／外观／触点集合变化合并新旧 bounds；其他墨迹脏区也会触发全部光标重绘，避免半透明累积。静止无脏区可休眠；常规帧合成后一次 Present。已有 previousVisuals 和 backbuffer 保留，不等于纹样缓存。[dirty:18602](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:18602)、[Present:18743](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:18743)。
- **logo 仅一份** `src/UI/logo1.svg`。09-11 归档已将旧 SetLogoVisual 多层方案改为单 SVG；当前 RenderLoop→Resolve→ApplyAttributes。无指示器端点取 `Resolve(1,0,任意色)`／`Resolve(0,0,任意色)`：浅色笔 RGB43、黑屏幕9%；深色白笔、白屏幕22%；两者 indicator、outline 均为0。[外观:32](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.LogoAppearance.h:32)、[应用:5198](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:5198)。lunasvg 固定3.5.0，现链为解析→改节点→CPU栅格→D2D位图上传。[版本:37](D:/Project/Inkeys/Repo/Inkeys/vcpkg.json:37)、[栅格:551](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.UI.cpp:551)。
- **设置**：“绘制→光标”含四设备预览、注入识别、信息条、四透明度滑块、精细控制；透明度默认50/50/25/50%，范围25–100%。笔速页含演示、警告、自动／灵敏度／五入口及条件精细控制。[光标:729](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:729)、[笔速:984](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:984)。

## 推荐方案

**推断，高置信度**：

| 方案 | 改动与取舍 |
|---|---|
| a GPU预烘焙 | 推荐结合 b 的 logo 栅格。扩展形状／纹样后一次绘入离屏RT；现接口固定 backbuffer，须适配目标、viewport、混合状态。 |
| b CPU栅格上传 | 独立 lunasvg 文档可在初始化或绘制 owner 使用；整枚做SVG可绕开烘焙pass，但需另维护形状／描边数学。现D2D步骤只是上传。 |
| c SDF＋logo纹理 | 改动较少、任意尺寸边缘清晰；仍逐帧计算轮廓和抓手，不完全满足整体缓存目标。 |

新增精灵采样路径；Appearance、等价比较、bounds 纳入外观身份，主／Touch 共用资源，动态尺寸只改变 quad。多指可批量上传。设置快照经现有 Bridge 发布，**RTS同步回调不增加工作**。光标页放明暗下拉＋纹样预览按钮；橡皮页的外观项仅保留形状＋i，既有功能控制保留。

## 风险

- 缓存省的是栅格计算；移动仍须采样绘制、清旧位置及 Present。等比缩放同时改变描边／圆角；放大可能模糊，缩小可能丢细节。
- 用预乘 BGRA8/RGBA8、独立线性 clamp sampler，不能复用现有 point sampler；图集需防串色。自动 mip 要 RT＋SRV＋GENERATE_MIPS。[微软要求](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-generatemips)。
- Win7更新版WARP上限FL11_0且不支持共享表面，不能借Bar设备纹理；Draw3自行上传。修改SDF时同步正式／guarded shader。[微软兼容说明](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)。
- 18枚256² RGBA约4.5MiB，含完整mip约6MiB。设备重建须重上传；位置／动态尺寸不失效，设置切片切换须唤醒，DPI超过栅格档位再重建。保留CPU配方。
- 标准 note 在普通说明为空时不画i，形状行须保留说明。[规范:20](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/settings-attention-notes.md:20)。以上未做运行验证。

## 需用户决定

- **6 DIP圆角**：参考尺寸后随动态同比放大（单缓存）；或始终6 DIP（需九宫格／参数化SDF）。
- **星光**：静态纹样（单帧缓存）；或动画（增加帧图及持续唤醒）。
- **“纯色logo”**：保留主栏9%／22%屏幕层次；或全部统一实色，辨识度与主栏外观会不同。