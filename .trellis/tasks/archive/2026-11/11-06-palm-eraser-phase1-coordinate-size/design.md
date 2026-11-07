# 技术设计：RTS 坐标与手掌定尺（2026-11-06 已批准）

## 0. 2026-11-07 修订（优先于下文）

第2、3、6节（HDC 窗口映射、映射代次/Host 失效链、生产 CPU probe）已撤回，代码恢复 42f4b9a8 的 RTS 生命周期、热路径与首 context 位置倍率。仍生效的只有第4节公式（inkToCanvasScale 即现有共享位置倍率，inkToDigitizerScale 即当前 context 已缓存的 contactScale）和第5节手掌定尺。验证改为 Headless SpeedEraser/Palm 用例、RtsSharedScaleCompatibilityForTesting 新期望及 `--draw3-palm-eraser-test`。

## 1. 已定范围与方案

用户于2026-11-06选择 A：识别面积仍为 W×H，阈值/配置/设置预览口径不变；圆形橡皮直径改为 sqrt(W×H)。输入宽高纠正可能改变实际触发结果，但不额外引入 π/4 识别系数。

同一任务内按“窗口坐标映射 → 长度换算 → 手掌定尺 → 联动回归”实施。三者共同依赖画布像素契约，暂不拆分多个独立子任务。

## 2. 位置映射：目标客户端 HDC 定义 ink→pixel

### 选择与依据

使用绑定 RTS 的主 Drawpad HWND，通过 GetDC(hwnd) 取得客户端 HDC，读取 LOGPIXELSX/Y，计算：

```text
Sx = double(LOGPIXELSX) / 2540.0
Sy = double(LOGPIXELSY) / 2540.0
Px = inkX × Sx
Py = inkY × Sy
```

这是 Microsoft 原生 RTS CustomRenderer 示例使用的单位来源，精度采用 Microsoft Learn RTS plug-in 示例中的浮点比例；不复制旧 C++ 示例把2540/dpi强转LONG的截断，也不复制查询失败后硬填96的行为。原始链接和适用边界见 research/mapping-design-evidence.md。

IInkRenderer::InkSpaceToPixel 是另一条官方转换路径，但本项目没有业务 object/view transform，当前交付不新增该COM对象、跨apartment共享或整数基向量近似。HDC比例已经足以明确单位方向，并减少额外依赖。不得据此声称已经实测所有多显示器/驱动组合。

### 数据与资源合同

- 插件绑定的 HWND 与 RealTimeStylus::put_HWND 使用同一个窗口；不从首 context、主显示器、EDID、用户标定、设置UI缩放或GetDpiForWindow推导S。
- HDC只在初始化/生命周期或显示映射变更的低频路径读取；同线程GetDC/ReleaseDC，用RAII覆盖早退，不长期保存或跨线程共用HDC。不修改共享DC的映射模式、原点或业务变换。
- LOGPIXELSX/Y须为正；保留实际HRESULT/Win32错误及失败原因，不能以1:1或96DPI伪装成功。纯比例转换不引入renderer平移；RTS绑定HWND的位置已是对应客户端的ink坐标，不再减屏幕/显示器原点。
- 保持现有进程DPI初始化（IdtMain.cpp:142），Win7仍由SetProcessDPIAware回退。对可能运行于系统线程的查询，在可用系统上动态取得GetWindowDpiAwarenessContext和SetThreadDpiAwarenessContext，以短作用域匹配窗口并恢复原线程状态；两者均缺失的老系统使用既有进程感知模型。入口缺失不能阻止Win7启动；可用入口调用失败不得猜测正确环境。
- 不把每屏effective DPI替代LOGPIXELS。显示物理标尺及Palm DIP回退依旧使用其已有独立合同。
- 缓存明确命名为inkToCanvasScale；当前contactScale在涉及到的局部字段/签名/诊断中改为inkToDigitizerScale，避免反向误用。只做必要命名，不全仓库清理。
- Mouse/Pen/Touch的RTS X/Y共用此映射；Win32Mouse已是客户端像素，不转换。InAir光标、Down/Move/Up和诊断需一致；不通过屏幕裁剪掩盖倍率错误。

## 3. 生命周期、显示变更与故障边界

### 同代发布

保留RtsDecoderCache、activeBindings、callback lease及writer/reader gate。新S、当前context的C和属性元数据构建完成后，通过既有PublishStaged/PublishIncremental边界发布；宽高transform在发布时预计算。位置与面积使用同一decoder generation。

启用、UpdateMapping、TabletRemoved后的全量重建不再查询首context倍率；TabletAdded正常增量沿用当前有效S，新context使用自身C。所有恢复/补建入口均检查映射有效性，不能残留QuerySharedPositionScale的枚举首项路径。

### HWND/DPI变化通知

- 保留RTS UpdateMapping作为权威生命周期通知，沿用取消旧接触→清缓存→重建的边界。
- 补齐Host::ForwardMessage的WM_DPICHANGED/WM_DISPLAYCHANGE及跨显示器WM_WINDOWPOSCHANGED到输入映射的失效通知。只传有界dirty/revision及ControlWake，不在WndProc同步调用RTS COM、等待writer gate或持有窗口/Host锁等待输入线程。
- 同一显示器的WM_WINDOWPOSCHANGED及相同值的WM_DPICHANGED合并，不因每次普通窗口消息取消笔划。WM_DISPLAYCHANGE一律失效：相同分辨率仍可能发生旋转/拓扑变化，不能仅按消息分辨率去重。物理标尺配置更新不自动成为RTS坐标变更。
- 显示变更使旧revision不再接受新位置包；绘制owner唤醒后，通过现有plugin生命周期保活入口在writer边界取消旧接触、使decoder失效，仅做缓存/接触操作，不跨线程调用原始RTS COM指针。
- 在接下来的UpdateMapping/InRange/Down等已有慢路径，使用回调的source重新获取S与context元数据并发布；Packets/InAir只检查revision和读取有效缓存，禁止因dirty每包重新查询COM/GDI。缺少后续回调时保持取消态，无残留活动橡皮；新Down可以恢复，不要求重启。
- 查询期间若窗口revision再次变化，丢弃过期构建结果，保留下一次慢路径重建请求；不在回调内无限重试。原Up仍须完成对应取消/回收，不得因面积或映射无效泄漏contact。
- 生命周期保活复用diagnosticsPlugin/diagnosticsMutex已有模式，或在锁内取得强引用后释放锁再操作；不得持有生命周期锁执行可能回调同锁的代码。实现时以竞态测试验证shutdown与refresh交错。

### 失败策略

- 启动前若无法取得可信窗口映射：归类为公共初始化失败，清理RTS后走Host既有Win32Mouse回退，不为同一公共失败重复尝试BasicXY；无需新增用户设置。可增一个准确的FailureStage诊断值。
- 运行期查询失败：旧接触取消、旧映射失效，不猜倍率、不回退首context、不在活动RTS中并行启动Win32Mouse。后续慢路径/映射通知按既有恢复方式重建。
- 当前BuildContextDecoder对非法C整体拒绝的行为暂时保留，不借本任务扩大设备准入。纯面积转换函数则返回明确InvalidTransform类状态，不能产生CanvasPixels。
- 若实施发现上述刷新投递无法在现有owner/保活边界安全完成，回到规划解释具体冲突；不能用新的后台线程或更改停产协议绕过。

## 4. 宽高：补齐digitizer→ink

ResolveContactLengthTransform增加当前context的inkToDigitizerScale参数；参数不提供默认值，强制所有调用方说明空间。

```text
spanToDigitizer = axisResolution / spanResolution × spanUnitCm / axisUnitCm
spanToCanvas = spanToDigitizer × abs(inkToCanvasScale / inkToDigitizerScale)
widthPx  = rawWidth  × widthTransform.spanToCanvas
heightPx = rawHeight × heightTransform.spanToCanvas
```

- 保留英寸/厘米换算、presence、resolution、声明范围、finite和float输出范围检查，增加C的finite/非零校验。生产C正值约束沿用查询层；纯长度helper的符号按绝对线性长度处理，负值测试与生产准入分开。
- 长度不减logicalMin、不加位置平移；WIDTH/HEIGHT必须使用对应X/Y轴和同一tcid的C。
- rawContactSize不变，contactSize为解释清楚的画布像素宽高；字段缺失/异常只保留原始数据和原因，不伪装有面积。位置不得再除C。
- 转换在decoder创建/重建时完成，输入包只乘缓存系数。调试红框、Area px、手掌和笔速面积辅助消费同一修正结果。
- 更新诊断中的positionMeaning/lengthMeaning/extraContextScaleApplied等旧说明，按既有开关和有界输出记录S、C、元数据、generation与换算系数；不新增逐包同步日志或完整新采集系统。

## 5. 手掌：只更改定尺几何模型

State::Observe保留recognitionArea=W×H、最大面积判断和原阈值比较，diameter=sqrt(recognitionArea)。若适用，移除仅因此不再使用的局部include；不顺手整理其它代码。

保留长宽比≤3.5、观察窗、最大样本、第一次Up锁定、续接继承、邻域策略及工具转换。ExclusionRadius继续根据实际新直径计算，因此邻域尺寸会自然随橡皮变小，公式本身不改。旧擦除点的半径保持，后续点/光标同用新直径；不改历史文件格式。

设置页面展示的矩形仍表达识别阈值，不改成橡皮圆形预览，不改文案/翻译/配置。尺寸规则测试同时防止误把识别面积乘π/4。

## 6. 生产验证接缝

主产品Headless目前不编译RealtimeStylus，独立demo的同名helper不能作为生产验证。新增一个窄的生产CPU probe（计划名RunRtsCoordinateProductionProbe），与真实查询结果解释、缓存发布、DecodeSnapshot及失效状态操作共用实现。由IdtMain早退CLI --draw3-rts-coordinate-test调用，严格参数数量，不进入配置、单实例或HWND初始化，不全局开启DRAW3_TESTING。

Windows查询采用窄provider/结果注入接缝，覆盖真实helper消费成功/失败结果；不实现测试专用第二套公式。直接调用生产decoder/cache覆盖Mouse/Pen/Touch、枚举顺序、代际、Up/取消、shutdown/refresh交错。真实HDC和系统驱动行为不由这种注入证明。

复用Headless的真实SpeedEraser/Palm代码测试，以及已有--draw3-palm-eraser-test做controller消费回归。不改独立demo源码/测试工程，不为验证创建新构建系统。

## 7. 兼容、风险与回退

- Win7 SP1 + KB2670838为硬基线。GetDC/GetDeviceCaps/ReleaseDC均早于Win7；新增较新的DPI API仅动态解析并有缺失路径。系统DLL加载遵守现有绝对System32路径规则，不依赖KB2533623。
- 不新增IInkRenderer COM可用性要求，不替换RTS、D3D或Win32Mouse后端；现有构建工程依赖保持。
- Surface的C可能小于1，纠正后尺寸可能增大；这是必须记录的回归风险，不能把修复等同统一缩小。基于历史隐藏窗口参数的算术回放不冒充实机尺寸。
- 椭圆只是一种定尺模型，不保证圆与真实掌面轮廓一致；极端比例仍用既有拒绝规则。
- 本机编译、纯CPU回归、导入检查、真实设备/Win7运行分别列证据；默认不启动任何GUI。无实机结果时最终明确未验证项目。
- 若需要撤回，窗口映射/面积变换及其诊断/tests/spec是同一单位合同，必须作为一致组合回退；不能只撤除C而留下新预期。手掌直径调整可独立回退，但同时恢复对应合同。
