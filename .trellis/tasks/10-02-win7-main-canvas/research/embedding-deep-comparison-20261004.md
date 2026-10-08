# 原 Inkeys3-Draw3 → 嵌入版：进一步静态对照（2026-10-04）

## 结论

继续比较实际原仓库与当前嵌入产品，未发现能因果解释本次 Win7“同帧 Cursor Draw 已发出、ULW mapped source 却全零”的产品新增代码缺陷。这个结论比仅核对呈现描述更窄也更具体：本轮核对了 Renderer 函数体、Cursor 成功路径的实际 context 调用和常量赋值、预热、帧门控、Host 接入及原窗口线程。

没有证明 GPU 已经产生非零像素。当前范围仍是 Win7 WARP 的数据/执行/输出，以及最终纹理到 staging 的 Copy/Map；不能仅将 USER32 最后一跳或嵌入层认作根因。产品帧门控、颜色、双 HWND 确有新增行为，按下面的触发路径逐项评估，而不是因“新代码”而排除或判错。

## 基线与方法

- 原独立仓库：`D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3`，main ref `8d045298eaaac76f752b4f8b5f3303b3520e50b7`，实际比较 `inkStrokeModelerTest/draw3/` 当前文件。
- 产品：`D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/`；HEAD `529722e6877a35d2aa5e804657e55543436610ef`，保留既有 dirty tree。
- 40 对 `.cpp/.cppm` 文件生成逐行 diff；只归一化 module/namespace 和四个 shader resource identifier，不改原文件。11 对归一化后逐行相同。其余文件的差异不能等同故障；尤其 DrawingController 从5620行扩到13950行，包含大量存储、场景、指标和测试入口。
- 对四个 renderer implementation 文件的54个同名 `InkRenderer` 函数，按大括号抽取函数体，去注释/空白后比较token，42个相同、12个不同；另有3个新增 Laser Bake 方法。此为源文本证据，不能冒称完整编译产物/ABI/运行状态等价。
- Cursor另做独立检查：从合法 appearance 后的成功路径提取15个 context 调用及9个 b0赋值，原版/产品的次序与参数全部相同；`SetOMTarget`本身也在42个相同函数之内。GPU getter诊断lambda不混入成功绘制序列。
- 差异、函数范围与原始保护hash保存在：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-embedding-deep-20261004/`，主要索引为 `diff-index.json`、`renderer-function-comparison.json`、`cursor-success-path-comparison.json`。

## 1. 哪些 GPU 执行部分没有因嵌入改变

下面的“相同”指上述函数体token比较；shader字节/执行chunk证据仍引用前轮独立报告，不重复冒称本轮新测。

| 直接影响像素的入口 | 原文件与行 | 产品文件与行 | 比较结果 |
| --- | --- | --- | --- |
| backbuffer GetBuffer(0)、RTV、L2/L1/L0资源 | renderer.cpp:262 | Draw3.Renderer.cpp:308 | CreateSizeDependentResources函数体相同 |
| viewport宽高和RSSetViewports | renderer.cpp:129 | Draw3.Renderer.cpp:129 | SetScreenSize相同 |
| 单/双RTV及depth状态 | renderer.cpp:152、159 | Draw3.Renderer.cpp:152、159 | SetOMTarget/SetOperatorTarget相同 |
| dirty纹理Copy | renderer.cpp:53 | Draw3.Renderer.cpp:53 | CopyResource相同，包括源box与目标offset |
| L1/L0到最终RTV解析 | renderer.cpp:65 | Draw3.Renderer.cpp:65 | ApplyOperatorLayers相同 |
| 激光Tip上传与绘制 | renderer_laser.cpp:322 | Draw3.RendererLaser.cpp:349 | DrawLaserDots完整函数体相同 |
| 激光coverage、rect resolve与粒子Draw | renderer_laser.cpp:107、183、365 | Draw3.RendererLaser.cpp:134、210、392 | DrawLaserCoverage/DrawLaserRectPass/DrawLaserParticles相同 |
| 激光与形状预热 | renderer_laser.cpp:408、renderer_primitives.cpp:219 | Draw3.RendererLaser.cpp:435、Draw3.RendererPrimitives.cpp:228 | 两个WarmUp函数相同，包括保存/恢复viewport |
| 平移/可信L2快照 | renderer_snapshot.cpp | Draw3.RendererSnapshot.cpp | 整个文件归一化后相同 |
| GPU history缓存及粒子系统 | ink_history_gpu.cpp/.cppm、laser_particle_system.cpp、laser_particles.cpp | 对应Draw3文件 | 归一化后相同 |
| Win7 Pointer兼容stub | win7_compat.cpp | Draw3.Win7Compat.cpp | 归一化后相同；输入正证据已有，不重开输入调查 |

## 2. 12个变化函数和3个新增方法逐项解释

### 2.1 普通笔/形状的 NO_OVERWRITE 回退

`Draw3.Renderer.cpp:476-480`查设备支持；`RendererPrimitives.cpp:64-70`及`:177-183`能力不足时逐批DISCARD、m_bufferHead=0。原版只有环形空间不足时DISCARD。

这是产品真实新增的Win7适配。但Cursor原版`:253-261`与产品`:343-358`都用WRITE_DISCARD；LaserTip原版`:330`与产品`:357`也使用DISCARD。因此这项回退不独立解释橡皮Cursor和LaserTip共同无像素。普通笔的失败可另由Map/分批处理引起，不能以Cursor正证据笼统证明所有stroke正确。

### 2.2 Cursor增加诊断，没有更换其成功绘制序列

`DrawTransientDrawingCursor`从52行扩到147行，新增合法性/资源/Map错误记录和限频GPU状态查询。

实际写入仍是两个InkPoint：中心/半尺寸与outline/fill。两个Map依旧DISCARD；m_bufferHead=2；b0的width、height、shape、offset=0、RGBA、Draw operator与三个outline通道均相同。随后SetOMTarget(backbuffer)，TRIANGLELIST、同VS/PS/b0/t0、ONE/SRC1的resolve blend、同rasterState、Draw(6,0)，最后解绑VS t0。15个context调用和9个常量赋值逐项比较通过。

没有因产品接入将Cursor画到L0、停放document或辅助DIB。已有tick56460693的before/after绑定/viewport正证据与随后1296源像素全零对应，合法geometry已进入这段路径；仍未直接核验GPU实际读到的t0/b0字节。

### 2.3 激光颜色和烘干事务

`ConfigureLaserStyle`改成按产品颜色配置，但半径与各材质alpha仍有明确正值；这不控制白色橡皮Cursor。`DrawLaserDots`本体相同，颜色通过b1独立配置，不依赖完成stroke的history。

新增BeginLaserBake/LaserBakeTarget/CommitLaserBake只操作laserCompositedColor与独立laserBakeScratchColor；swap不包含backBufferTexture。新增OM解绑发生在Bake/清理，后续Tip/Cursor各自再次SetOMTarget(backbuffer)。未找到Bake之后直到同帧ULW之间又清空Cursor的分支。

`ResolveLaserStrokeCoverage/ResolveLaserCompositedColor/ClearLaserCoverageRect`从void改bool，转发真实DrawLaserRectPass结果；实际pass本体相同。`ReleaseSizeDependentResources/ReleaseResources`追加scratch或能力状态清理，`ClearAllLaserCoverage`追加OM解绑和释放scratch。稳定故障帧没有该资源释放/重建的对应日志。

### 2.4 Init和shader resource读取

Init新增设备/context空检查与NO_OVERWRITE能力查询，调用ConfigureLaserStyle增加默认颜色参数；创建RTV、blend/raster/depth和动态buffer的核心语句沿用原版。LoadShaders只是对应产品资源ID读取。

实际旧故障EXE与新候选的嵌入301–304已经前轮datafile方式核验；shader执行chunk与本仓Demo对应，不能把资源重命名或profile升级列为本轮发现的新根因。详见 `shader-win7-contracts-static-20261004.md`。

## 3. Controller新增行为是否跳过Draw或覆盖像素

### 3.1 新增rasterSubmissionFailed门控

原controller:5477以后直接合成并绘制visual再Present。产品:13635以后检查rasterSubmissionFailed、compositeSucceeded和Laser pass返回值，失败时保留CPU权威状态、跳过本次Present并重建/退避。

这项改动理论上会导致“document存在而帧未提交”；但不能单独解释已观察的同帧Cursor upload-success→before-draw→after-draw-issued→ULW success/source全零，因为该样本已通过这些门控。若未来发现 `[Draw3.Raster] submission failed`，应单独分析失败帧，而不是拿当前ULW成功样本推翻所有门控风险。

### 3.2 调用顺序和metrics没有移除visual

当前顺序为L2与L1/L0合成（13655-13684）→粒子/trail（13687-13704）→LaserTip（13708-13714）→Cursor（13715-13716）→CPU指标记录（13717-13729）→PresentFrame（13731）。原controller:5484-5499具有同样的高层順序。

MetricsLaserCall:2044-2060无条件调用传入callback；没有关闭metrics即跳过draw的分支。FreezeCandidates:2497-2584读CPU文档/history和指标状态，返回候选数量；调用处不依据该数量阻止Present，也不调用D3D清屏/Copy。不能把“指标拒绝内容证明”理解成“指标清掉了画面”。

PresentFrame:7056先执行presentation.Present，再在7081附近调用observer.presented。新增Host回调与ready回执发生在ULW source值已读取之后，无法作为此前源统计为零的单一原因。

### 3.3 产品颜色设置没有解释白填充Cursor

产品RGBA进入ColorForTool:247-264，确实可能使某些笔颜色alpha很低。此次最强实例是32px白色EraserGrip、opacity=.5、fill=1，而且尺寸/dirty/viewport已记录；该光标不取普通笔RGBA。因此“产品颜色alpha解析/默认配置为零”不能统一解释本次共同故障。用户认可的系统光标也不经过该GPU链。

## 4. 原版与产品Host/窗口线程

原版WindowController.Initialize:245-246通过_beginthreadex创建HWND owner；原main:87以后在wmain创建D3D并执行controller。**原版本身也不是HWND owner线程直接执行全部Draw/ULW。** 产品变成Window Service owner加Draw3绘制jthread，不应仅凭跨线程HWND就宣布嵌入兼容回归。

产品Host:1458创建独立graphics；:1487用该graphics创建presenter；:1555将同一renderer传给controller；:1575同线程ClearCanvas；:1625同线程Run；没有把UI3设备/context作为参数传入。窗口消息ForwardProductMessage→Host.ForwardMessage→WindowController.HandleExternalMessage；WM_SIZE和paint只发布resize/full请求，未找到生产GUI线程的context Draw/Copy writer。生产以外HiddenWindowTest/Run*Probe的GPU调用不会在普通Host启动时执行。

窗口由独立Demo的topmost popup迁移到产品owner链、双HWND和样式callbacks，确实可能影响桌面可见性。它们的故障表型应与源统计分开：现在CPU mapped staging在ULW前已经全零，纯Z序/USER32显示失败不能单独解释它。若晚间证明staging/DIB非零而窗口不显，再把窗口部分提升优先级。

## 5. 原仓历史补充

只读git log/show查到operator-layer dual-source解析已于原仓`78e644c`（2026-07-17）引入，在8d045298嵌入基线之前。原仓`dfe606c`（2026-08-04，Win7 compatibility提交）在transparent_presentation里主要改系统DLL绝对路径和resize时Copy边界，没有把这条ULW链换成另一套GPU实现。

这些是源代码演进事实，不能推断具体日期的原版Win7实机结果或driver。既不否定用户过去可能测试成功，也不把本轮读取的源码等同于那个旧EXE；这里不重复寻找用户手头旧包作为本轮前提。

## 6. 判断与下一步

本轮比较进一步降低“产品迁移把核心Draw/ULW语句写错、预热留下零viewport、指标截断Present、或者Host共享UI3 context”的优先级。没有把它们全局排除：运行期资源内容、驱动执行和状态仍须实证。

现有晚间六例仍是最小必要分界，包无需更新：

- offscreen.clear正常、backbuffer.clear异常：优先最终交换链资源/Copy/Map边界。
- 两Clear正常，正式Cursor异常：优先数据/VS/PS/正式混合，结合两个blend_disabled对照及pipeline计数。
- 六例都正常：独立基准尚未覆盖生产预热、32px Grip shape6、1920x1080、非原点dirty Copy及保持RTV绑定的blocking Map。这时再按单变量对照产品Run路径；原/产品WarmUp函数相同也不证明Win7运行正常。保留原先计划，不先更换FLIP或改shader。

## 本轮验证与保留

仅静态读源、原仓read-only Git历史、函数/成功路径比较及保护hash检查；没有构建或运行GPU/GUI。新写研究和任务进度，产品源码/CLI/采集脚本/8个CSO与已交付ZIP保持本轮开始时hash。任务继续in_progress，无commit/push/archive。
