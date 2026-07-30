# Runtime and Rendering

## Ownership And Flow

`main.cpp` 的初始化顺序是：

1. 创建 `ContactInputCoordinator`
2. `WindowController::Initialize` 创建隐藏窗口，随后把 coordinator 绑定到窗口控制唤醒
3. `InitializeGraphicsDevice`
4. `TransparentPresentationController::Initialize`，内部初始化 `InkRenderer`
5. `RealTimeStylusInput::Initialize` 启用同步 RTS 插件
6. `DrawingController` 创建，先提交透明画布并显示窗口，再进入消息循环

不要交换窗口、设备、交换链和 renderer 的依赖顺序。`TransparentPresentationController` 保存对 `GraphicsDeviceResources` 和 `InkRenderer` 的非拥有指针，调用者必须让这些对象覆盖 presenter 生命周期。

退出时先停止 RTS 并解除 `WindowController` 的 coordinator 指针，再销毁 coordinator。RTS 的 COM 对象只由完成 MTA 初始化的主线程创建、禁用、移除插件并释放。

## Input And Thread Boundary

`WindowController::HandleWindowMessage` 将 resize、清屏、全量呈现、DWM 变化和退出写入原子状态。主循环通过 `Consume*` 方法消费请求。

- 窗口回调不重建 D3D 资源。
- `pendingResizeWidth_/Height_` 先写入，`resizeRequested_` 最后 release 发布。
- `DrawingController::ProcessPendingResize` 在绘制线程依次 resize renderer、resize presenter，两个步骤都成功后才 `CommitSize`。
- RTS 同步回调只完成 packet 解析、contact 状态发布和唤醒，不调用 D3D、presenter 或 stroke modeler。
- 控制请求先写 sticky 原子标记，再通过 coordinator 队列唤醒；消费方在阻塞前二次 dequeue，避免清 pending 与入队交错造成丢唤醒。
- 无活动 contact 时使用 blocking dequeue；活动 contact 仍按帧更新停笔预测。
- Down、Up/Cancelled 和控制请求递增 wake generation 并触发 Win7 可用的 event；Move 只更新合并快照，不把 240Hz packet 变成无界帧驱动。
- 绘制线程使用 `THREAD_PRIORITY_ABOVE_NORMAL`，活动末段按 QPC deadline 核对 wake generation；完全空闲仍阻塞在队列 semaphore，不自旋。

## Tool State

单笔兼容路径在 `WM_LBUTTONDOWN` 时复制工具。多 contact 路径只在空闲批次的首个 Down 读取 1/2/3/4 选择，并把同一工具锁定到该批所有 contact；活动期间的选择只影响下一批。

当前行为：

| Tool | Width | Prediction | Width mode | Live layer |
|---|---:|---|---|---|
| Pen | 5px base; hardware 1–7px | active configured mode | per-device fixed/simulated/hardware | real tail + prediction + taper |
| Highlighter | 6.25×50px fixed vertical nib | enabled from Down | fixed | rectangle sweep primitives, no taper |
| Eraser | 50px | disabled | fixed | real points directly committed to L1 |
| Laser | 5px solid (1.67px core) + 5px diffuse/side | active configured mode | Pen laser pressure; Mouse/Touch fixed | stable premultiplied color + per-stroke coverage scratch, never L2 |

这是当前实验实现。预测时长、目标帧率、笔宽、live-tip 和几何阈值默认都是实验参数；只有公开接口、持久化格式或明确兼容要求已经依赖某值时，该值才升级为兼容契约。

## Scenario: Laser Pointer Glow Trail

### 1. Scope / Trigger

修改 `DrawingTool::Laser`、Laser coverage、激光粒子/笔尖、留存计时或其多 contact 主循环时，必须应用本契约。

### 2. Signatures

- `DrawingTool::Laser`，数字键/小键盘 `4`
- `StrokeModelConfiguration::laserParticlesEnabled`、`laserParticleConfig`、`laserHoldDurationSeconds`
- `DrawingController::SetLaserParticlesEnabled/GetLaserParticlesEnabled`
- `DrawingController::SetLaserHoldDurationSeconds/GetLaserHoldDurationSeconds`
- `InkRenderer::AcquireLaserParticlePath/AppendLaserParticleRealPathPoints/ReplaceLaserParticlePredictionPathPoints/SetLaserParticlePathInputSpeed/EndLaserParticlePath`
- `InkRenderer::SimulateLaserParticles/EmitLaserParticles/ResetLaserParticles/DrawLaserParticles`
- `InkRenderer::DrawLaserCoverage/ClearLaserCoverageRect/ResolveLaserStrokeCoverage/ResolveLaserCompositedColor/DrawLaserDots`

### 3. Contracts

- Laser 在 Down 时锁定到当前批次，支持 Pen、Mouse 和多 Touch；跳过断触 reconnect、倒转笔尾橡皮覆盖和触觉反馈。
- 已完成 Laser 使用独立 `R8G8B8A8_UNORM` 预乘颜色层；每支未烘干轨迹先独立写入可复用的单笔 `R8G8B8A8_UNORM` coverage scratch，四通道为白芯、白红散射、红色实体外套和外部漫反射，再按 Down 顺序 source-over。任何 Laser 几何都不得进入 L2。
- 96 DPI 默认实体总直径为 5px，白芯直径约 1.67px，红色实体轮廓外每侧固定扩散 5px，因此完整视觉直径为 15px；漫反射 coverage 在实体边界为 1，使用平方曲线向外单调衰减并在 5px 外缘为 0，红粉外缘高光只混合 RGB 而不额外增加 alpha。`LaserDot`、Touch 笔尖和固定宽度轨迹复用同一尺寸契约。Pen 使用 `0.65 + 0.75 * clamp(p, 0, 1)` 缩放实体、白芯和内侧散射，外部 5px 漫反射只随 DPI 缩放；无效压力保持上一宽度，prediction 继承最后真实实体半径，Mouse/Touch 固定基准宽度。
- 多支 Laser 按 Down 顺序分层，后 Down 的整支轨迹位于上层；较早结束的 contact 保留最终 CPU 几何直到同批最后一支抬起，同一笔自交仍以 coverage 并集避免重复加深。
- 粒子默认开启，但只使用 FL11_0+ 的 D3D11 Compute Shader 固定池：`2048` 粒子、`32` 路径、每路径 `16384` 个点。路径点由不可变追加式 `realPoints` 前缀和每帧替换的当前 `predictedPoints` 尾组成；新真实点覆盖旧预测尾后再重写预测尾，真实点优先占用容量。整笔逐帧重绘不得重建 GPU 粒子。
- Down 只申请路径并建立时间发射基线，不爆发。发射 `dt <= 1/30s`，速率为 `min(48, 6 + max(组合路径新增弧长, 0) / dt / 4)` 粒/秒：静止每 contact 每秒 6 粒，移动约每 `4 DIP` 一粒，全局每帧最多 `96` 粒；预算超额、卡顿和预测回缩均不积压。运行中重新开启从当前组合路径末端建立基线。
- 每粒子保存 path slot/generation、组合弧长、segment cursor、平滑位置/切线、横向偏移、速度、固定寿命和呼吸参数。基础目标速度为 `clamp(12*dpiScale + 0.08*filteredInputSpeed, 12*dpiScale, 96*dpiScale)`，附加固定 `±12%` 差异并以 `80ms` 时间常数追随；实际推进速度乘 `1-smoothstep(0,1,age/3)`，运动 `dt <= 1/30s`，寿命使用实际时间。
- 出生弧长取当前组合路径末端，CPU 出生锚点正常直接跟随 L0 前端；位移超过 `max(6*dpiScale, 2*filteredInputSpeed*dt + 2*dpiScale)` 时以 `120ms` 时间常数靠近。预测尾缩短时粒子弧长立即钳制，持久位置/切线使用正常 `40ms`、异常 `120ms` 响应靠近新路径，不追赶丢弃位移。
- 出生点位于白芯内部，约 `120ms` 向随机侧扩散至红色实体边缘外最多 `4.5 DIP`。固定寿命 `3.0s`，Alpha 同样使用 `1-smoothstep(0,1,age/3)`；基础亮度随机 `0.68–1.0`，`0.8–1.4Hz` 随机相位呼吸以 `0.12` 振幅在 `0.2s` 渐入且只改变 RGB。
- Up 只停止发射和路径追加并冻结最后输入速度；存量粒子继续沿最终组合路径减速淡出，不再执行 75/25 收束。带粒子的路径在 Up 后保留 3 秒再回收。CPU 不保存、更新或回读粒子位置，只维护时间发射余量、路径上传状态、出生锚点和固定容量保守脏区。
- Compute 顺序固定为 `VS t8 unbind -> CS t0/t1 + u0 -> Dispatch -> CS unbind`；绘制为 shape `10` 的 `DrawInstanced(6, 2048, 0, 0)`，死亡槽生成退化图元。粒子在激光主体/实时墨迹之后、Laser tip 之前绘制，PS 输出预乘 Alpha 的纯白核与 `(1.0, 0.55, 0.62)`、峰值 Alpha `0.24` 的 `3 DIP * dpiScale` 浅粉红辉光。
- 最后一根 Laser Up 才记录 `lastAllUpQpc`；默认满亮保持 `3.0s`，固定 `0.8s` smooth fade。当前批次实际安排过粒子时，有效 Hold 为 `max(公开设置值, 3.0s)`，但 setter/getter 值不变。新 Down 在 Hold/Fade 中把整组 opacity 恢复为 `1` 并重新计时。
- `SetLaserHoldDurationSeconds` 只接受 finite non-negative 值；运行中调整须由 control wake 唤醒并相对最后一次全部 Up 立即重算。粒子 setter 同样发布 control wake；关闭后下一帧 reset GPU 状态并 union 旧保守 bounds。
- Hold 静态期不持续 Present；Fade、接触、prediction 和粒子动画才驱动帧。resize 保留稳定颜色层左上角交集，未烘干层从 CPU 几何重建；clear/Present 失败恢复必须同步重建或清理稳定颜色、scratch 和新旧 bounds。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Down/Up 多 contact | 只有最后一根 Up 启动 Hold；中间 Up 不改变 opacity |
| Hold/Fade 新 Down | 旧稳定颜色层全组恢复满亮并重新计时 |
| 非法 hold seconds | setter 返回 `false`，旧值保持不变且不发布设置 |
| Laser Up | 清除 prediction；同批最后 Up 才有序烘入稳定颜色层，不 reconnect、不 resolve 到 L2 |
| 粒子关闭 | 下一帧 reset GPU 池/路径 generation、清空保守脏区并 union 旧 bounds |
| FL < 11_0 或 CS/SRV/UAV/buffer 创建失败 | 只记录一次诊断并将粒子系统标记不可用；激光主体、瞬态层和 Present 继续 |
| 32 个路径槽占满或单路径超过 16384 点 | 停止对应 contact 的新粒子；不得覆盖仍被 generation 引用的路径 |
| 卡顿或当前路径末端 | 寿命按实际时间；沿线运动最多推进 1/30s，末端多余位移丢弃且不积压 |
| Hold 静止且无存活粒子 | 不产生额外 frame/Present；deadline 或 control/input wake 才恢复 |
| Hold 中仍有存活粒子 | 粒子保守 dirty 驱动帧；最后一粒到期后停止持续 Present |
| resize/clear/Present 失败 | 稳定颜色交集保留或按请求清空，未烘干层重建，旧/新 glow bounds 都进入 dirty |

### 5. Good / Base / Bad Cases

- Good：白芯与厚红边在深色、混合背景可见，浅粉红粒子辉光在白底可辨；粒子沿急转弯和 L0 prediction 尾运动，预测修正与真实前缀增长都不使旧粒子跳变。
- Base：Hover 为静态 LaserDot，活动 Touch 各自显示独立 tip；GPU 不可用时只有粒子降级，抬笔后已有粒子继续按各自剩余 3 秒寿命减速淡出。
- Bad：把 prediction 固化进不可变真实前缀、预测回缩后补发或瞬移已有粒子、每帧按重绘路径重建粒子、同时绑定粒子 UAV 与 VS SRV，或用 GPU readback 决定 dirty。

### 6. Tests Required

- 断言按键 4/枚举、最后 Up 计时、3.0s Hold、0.8s fade、运行中设置变化和非法输入。
- 断言 Laser 不进入 reconnect/L2，压力 `0/0.5/1` 只缩放实体且 prediction 半径继承正确，coverage bounds 覆盖 15px 基准完整视觉直径、最大压力实体和固定 5px 漫反射；静态核对漫反射 alpha 从实体边界的 1 单调衰减到外缘的 0，resize/clear/Present failure 无残影。
- 断言默认开启、Down 零爆发、静止时间发射、移动每 4 DIP 密度、预测回缩不补发、全局 96 上限且不积压、速度目标/80ms 平滑、固定 3 秒生命周期及曲线单调性、亮度呼吸不改 Alpha、弧长末端丢弃、L0 极端跳变平滑、组合尾替换、generation/容量溢出、Up 后继续运动、有效 Hold 下限和保守 dirty 到期。
- Debug/Release ARM64 全解决方案构建、VS/PS/两个 CS 均以 SM5.0 编译并嵌入；人工覆盖慢/快画、急弯、长按、路径追加、多接触、Resize、ULW、开关和 Hold/Fade。

### 7. Wrong vs Correct

Wrong：`每帧 CPU 更新 vector<Particle> -> 上传 InkData -> Draw`，或 CS dispatch 时仍让同一粒子缓冲绑定在 VS `t8`。

Correct：`追加真实前缀并替换 prediction 尾 -> GPU Update/Emit -> 显式解绑 -> DrawInstanced 固定池`；last Up 有序烘干主体，GPU 粒子沿最终路径继续按时间减速淡出，只在 Fade、输入或保守粒子 dirty 活动时刷新，L2 保持不变。

当前源码描述现有行为，阶段说明描述历史设计或计划。两者出现数值或工具语义差异时，应并列记录，不得自动用源码覆盖历史目标，也不得自动让实现恢复为阶段说明。

## Scenario: L0-Frame Transient Drawing Cursor

### 1. Scope / Trigger

修改 RTS range/in-air 坐标、Pointer authority、`WM_SETCURSOR`、工具光标外观、瞬态 backbuffer 绘制或 cursor dirty bounds 时，必须应用本契约。

### 2. Signatures

- `RealTimeStylusInput::Initialize(HWND, ContactInputCoordinator&, DrawingCursorEventSink*)`
- `DrawingCursorEventSink::PublishPenCursorSample/ClearPenCursorSample`
- `WindowController::ConfigureDrawingCursor/ConsumeDrawingCursorRenderRequest`
- `ResolvePrimaryDrawingCursorVisual`、`MakeTouchEraserDrawingCursorVisual`
- `DrawingCursorVisualBounds`、`InkRenderer::DrawTransientDrawingCursor`

### 3. Contracts

- 自定义 Pen/Highlighter/Eraser 光标属于 L0 帧的最终瞬态视觉：先把 dirty 区域按 `L2 + L1 + L0` 合成到 backbuffer，再逐枚绘制 cursor。禁止把 cursor 写入共享 `layerL0`、L1、L2、ActiveStroke、contact payload、reconnect 或 metrics。
- shader shape type `4/5/6` 分别表示 Cursor Circle、Rectangle、EraserGripCircle；复用两项 `InkPoint`、48 字节全局常量和 resolve dual-source blend，直接输出 premultiplied Add 与 Retain。尺寸变化只能更新常量/primitive，不创建尺寸相关纹理或 `HCURSOR`。
- Pen 直径为 `max(当前基准画笔粗细, 5px * dpiScale)`；Highlighter 为 6.25x50px 固定竖直矩形。二者使用当前 RGB、25% fill Alpha 和 `#B8B8B8` 细内描边；fill Alpha 不得降低 outline Alpha，压力不改变 cursor 尺寸。
- EraserGripCircle 直径直接复用 50px 画布擦除宽度，不乘 DPI、不设最小值；主体纯白，圆环宽度为 4%D，两条圆头竖线宽度为 10%D、中心偏移为 12%D、半高为 24%D，结构颜色为 `#CFCFCF`。Hover 整体 Alpha 0.5，Contact 整体 Alpha 1.0。
- RTS InAir/Down/Packets/Up 发布 X/Y/QPC、inverted 和 contact；InAir/Packets 只解码批次最后一个包。样本使用 writer latch + sequence 一致发布，并以 sticky control wake 合并；RTS 回调不得调用 D3D 或 `SetCursor`。
- Windows 8+ 动态解析 Pointer API，并区分 `Unknown/Pen/Mouse/Touch`；`WM_POINTERENTER/UPDATE` 使用 `GetPointerInfo/GetPointerPenInfo` 直接发布 Pen Hover 坐标，不能依赖首个 RTS Down。每个有效 RTS Pen 样本都必须明确取得 Pen authority。旧系统由 RTS Pen 样本和非 promoted Mouse 消息回退。Pointer authority 仍为 Pen 且 Pen 样本有效时，低优先级 `WM_MOUSE*` 不得抢占；缺少 Pointer API 时同样以有效 RTS Pen 样本维持该优先级。Mouse 使用 `TrackMouseEvent/WM_MOUSELEAVE` 清理。
- Pen/Touch 离开后应清除其可见样本，但保留最后设备 authority 作为“当前无光标”状态；`WM_POINTERLEAVE` 无法取得 pointer type 时，只要旧 authority 或有效样本表明是 Pen，仍按 Pen 离开处理。禁止将 authority 立即改为 Unknown 而使旧 Mouse 样本复活。只有新的非 promoted `WM_MOUSE*` 才能明确切换到 Mouse 并恢复鼠标。
- Pen/Highlighter：Pen Hover 显示应用 cursor，Pen Contact 只隐藏系统 cursor；Mouse 保留 `IDC_ARROW`；Touch 不显示笔尖 cursor。
- Eraser/倒转笔尾：Pen/Mouse Hover 显示 Alpha 0.5，Contact 显示 Alpha 1.0，并隐藏系统 cursor。每个活动 Touch eraser contact 独立显示一枚 Alpha 1.0 cursor，不存在 Touch Hover；多指不得互相覆盖状态。
- 活动主指针使用 Down 锁定的有效工具；没有匹配主指针但仍有活动批次时使用批次 `selectedTool`。Touch cursor 直接读取各自 runtime 的一致 `lastModelSnapshot` 和有效 `tool`。
- 当前和上一帧全部 cursor bounds 的并集必须加入 `frameDirty`；隐藏、离开、Up、工具切换、resize、clear、重新暴露和 Present 恢复都沿用该规则。即使 cursor 未变，只要其他几何会触发 Present，也必须把当前 cursor bounds 合入脏区并从 `L2 + L1 + L0` 重建，禁止在上一帧半透明 cursor 像素上再次叠加。静止且几何/状态不变时不得单独重复 Present。
- 窗口线程只允许 `SetCursor(nullptr)` 或非拥有的系统 `IDC_ARROW`；禁止 `CreateIconIndirect`、`SetSystemCursor`、窗口类全局 cursor 和计数式 `ShowCursor`。私有刷新必须先用 `WindowFromPoint` 确认当前 HWND 所有权。
- sink 是非拥有指针；RTS shutdown 必须先禁用并移除插件，再清理 Pen 样本和 sink。disabled、error、tablet removal、out-of-range 同样清理旧 visual。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Pen Hover + Pen/Highlighter | 隐藏系统 cursor，显示对应 Circle/Rectangle |
| Pen Contact + Pen/Highlighter | 隐藏系统 cursor，不绘制应用 cursor |
| Pen/Mouse Hover + Eraser | 隐藏系统 cursor，绘制 Alpha 0.5 EraserGripCircle |
| Pen/Mouse Contact + Eraser 或 inverted Pen | 隐藏系统 cursor，绘制 Alpha 1.0 EraserGripCircle |
| N 个 Touch Eraser Contact | 同时绘制 N 枚 Alpha 1.0 EraserGripCircle |
| Touch 使用 Pen/Highlighter | 不绘制应用 cursor |
| Mouse 使用 Pen/Highlighter | 使用 `IDC_ARROW`，不绘制应用 cursor |
| Cursor 消失或移动 | dirty 包含旧 bounds 与新 bounds，正常图层重建后无残影 |
| Contact 完成/L2 resolve | cursor 像素从未进入 operator layer，L2 只接收墨迹 |
| Pointer API 不存在 | authority 为 Unknown，按有效 Pen 优先、Mouse 次之回退 |
| Mouse 已移动后 Pen Enter | 立即用 Pointer 客户区坐标绘制 Pen Hover，不等待 Down |
| Pen 离开且无新 Mouse 消息 | 清除 Pen visual 与系统 cursor，不显示旧 Mouse 位置 |
| Pen 离开后 Mouse 再移动 | 切换 authority 为 Mouse；Pen/Highlighter 恢复箭头，Eraser 显示 Mouse 橡皮 visual |
| Renderer 尚未配置 | 不提前隐藏系统 cursor；应用初始化继续按顶层失败契约处理 |
| RTS disabled/error/removal/shutdown | 清理 Pen 样本、唤醒绘制线程并清除旧 bounds |

### 5. Good / Base / Bad Cases

- Good：两指同时擦除时显示两枚白色不透明抓手圆；其中一指 Up 只清除对应旧区，另一枚继续移动。
- Good：MPP Pen Enter 直接显示彩色笔尖，Down 后应用笔尖消失但系统 cursor 仍隐藏，Up 后在最新位置恢复；离开后保持无 cursor，直到鼠标再移动。
- Base：Mouse 在 Pen/Highlighter 下保持箭头，在 Eraser 下切换为应用圆；Windows 7 路径标记待真机验证。
- Bad：把多枚 cursor 画进共享 L0 后随 contact Up resolve 到 L2，或为每次宽度变化重建 `HCURSOR`/纹理。

### 6. Tests Required

- 自动断言 appearance 有效性、sample sequence 一致性、Pen/Mouse/Touch authority、Hover/Contact/Inverted 矩阵和系统 cursor 隐藏决策。
- 自动断言 Circle/Rectangle/Eraser 参数、Touch 强制 Alpha 1.0、旧/新 bounds、边界裁剪和多 visual 同时存在。
- 自动断言 Pen authority + 无效 Pen 样本 + 陈旧 Mouse 样本不生成 visual；真实 Mouse authority 切换后才恢复 Mouse visual。
- 静态断言 RTS DataInterest 含 InRange/OutOfRange/InAir，InAir 选择最后 packet；搜索确认无自建 `HCURSOR` API。
- 完整 `Debug|ARM64` 解决方案构建必须重新编译两个 shader 并完成 `.cso` 资源嵌入；运行控制台测试。
- 真机覆盖 Pen、Highlighter、Eraser、笔尾、Mouse、单/多 Touch、窗口边界、SDR/HDR 和白/红/黑背景。

### 7. Wrong vs Correct

Wrong：`Pen leave -> authority=Unknown -> ResolvePrimaryDrawingCursorVisual 回退到旧 Mouse 坐标。`

Correct：`Pen leave -> clear Pen sample + keep Pen authority；新 WM_MOUSEMOVE 才切换 Mouse authority。`

## Stroke Modeling Invariants

- 输入时间使用单调递增的 logical time，来源是 wall-clock delta。
- 小于 `0.25px` 的原始移动视为抖动。
- 普通笔 `SimulatedPressure` 使用相邻有效原始点的速度，不使用 modeled/predicted velocity。
- RTS 路径先按真实 snapshot 的距离/QPC 计算速度并做一次低通，再交给笔宽估算器；同一批 modeled output 不得被当成多份新的速度采样。
- 第一份有效速度只能从基准宽度渐进追随，禁止回写并瞬间改变已经可见的起笔点。当前直径时间变化上限为每秒 `3 × baseDiameter`，相邻点半径变化上限为 `0.35 × distance`。
- 预测点直接继承最后真实宽度，不使用 predicted velocity 或 predicted pressure 改写半径。
- 普通笔 `HardwarePressure` 使用模型插值后的 `[0,1]` pressure 映射基准直径的 `0.2–1.4` 倍；Down 压力缺失时整笔回退 `SimulatedPressure`，后续偶发缺失保持上一真实宽度。
- 半径变化同时受时间和距离限制；L0 taper 后再次调用 `LimitRadiusTransitions`。
- 视觉连续三帧稳定后可冻结停笔更新，移动时解除冻结。

依据：`StrokeWidthEstimator::Append`、`UpdateRawPositionAndDetectMovement`、`UpdateIdleFreezeState`、`RebuildPredictedPoints`。

## Scenario: RTS Interrupted Stroke Reconnect

### 1. Scope / Trigger

修改 RTS 物理 Up、`RuntimeStroke` 生命周期、modeler 终态、活动层重建或 contact handle 回收时，必须应用本契约。

### 2. Signatures

- `StrokeModelConfiguration::interruptedStrokeReconnectEnabled`
- `TryGetInterruptedStrokeTailDirection`
- `ResolveInterruptedStrokeReconnectMotion`
- `EvaluateInterruptedStrokeReconnect`
- `AreInterruptedStrokeReconnectIdentitiesCompatible`
- `IsBetterInterruptedStrokeReconnectMatch`
- `InterruptedStrokeReconnectResult::predictionExtrapolated`
- `InterruptedStrokeReconnectResult::selectedTerminalDirectionCorridor`
- `InterruptedStrokeReconnectResult::selectedInHorizonTerminalDirectionCorridor`
- `kInterruptedStrokeReconnectSimulationEnabled`

### 3. Contracts

- 开关默认开启且只在构造 `DrawingController` 前配置；关闭时必须保留原先 Down 出队和立即 `kUp` 顺序。
- 物理 Up 在 80ms 窗口内先作为 `kMove` 输入并冻结 modeler、真实点、prediction、L0/L1 CPU 状态、提交游标、宽度估算器和旧 handle；Cancelled 立即终结。
- 候选必须具有有效末速和末端方向，并与新 Down 的设备、批次选择工具、有效工具、宽度模式、倒转状态和压力屏蔽策略完全一致。Up 的 `kMove` 成功后必须在新 Down 出队前冻结匹配专用 prediction，保证同帧 Up→Down 使用最新状态。
- prediction 有有效位置和正时域时，按 Down 间隔在预测时域内插值，超出时域使用最后预测位置；以 `selectedPredictionPosition - modeledPositionAtUp` 得到模型预测位移，再平移到物理 Up 坐标生成预测落点，禁止直接把预测终点瞬时速度方向与整段曲线桥接弦做硬比较。
- prediction 路径要求新 Down 到预测落点的误差不超过 `4px*dpiScale + 0.35*predictedDistance + 0.75*forecastAverageSpeed*beyondHorizon`；桥接距离上限按 `referenceSpeed=max(forecastAverageSpeed,recentFilteredInputSpeed)` 计算为 `clamp(referenceSpeed*gap*1.75 + 4px*dpiScale, 64px*dpiScale, 256px*dpiScale)`，且必须在完成方向与落点诊断后才以该上限拒绝。边界比较额外允许 `0.5px*dpiScale` 数值容差；终点速度只用于诊断。
- 冻结预测端点失败后，先在预测弦可靠、桥接夹角不超过 35°、相对 `referenceSpeed` 的速度比位于 `[0.35, 2.75]` 且 Down 超过预测时域时尝试加速自适应走廊：`adaptedDistance=max(predictedDistance,referenceSpeed*gap)`，桥接向预测弦投影后的纵向误差与横向误差必须分别不超过 `4px*dpiScale + 0.50*adaptedDistance`；禁止再次叠加时域外不确定度。
- 若预测弦走廊失败且预测末端速度方向有效，以相同 35°、速度比、动态距离和纵横误差限制尝试第二方向走廊；该走廊不依赖短预测弦达到 4px，但末端速度方向自身必须有效。禁止通过提高全局角度或扩大圆形落点容差代替第二走廊；命中时设置 `predictionExtrapolated=true`，并以 `selectedTerminalDirectionCorridor=true` 标记所选方向轴；拒绝诊断也使用该字段说明当前纵横误差属于哪条走廊。
- 若冻结端点失败但新 Down 仍在预测时域内，只允许末端速度方向使用严格补救走廊：完整间隔不超过 35ms、末端方向夹角不超过 15°、速度比位于 `[0.5,2.0]`，且继续通过现有动态距离和纵横误差限制。预测弦不得进入该分支；命中时设置 `selectedTerminalDirectionCorridor=true`、`selectedInHorizonTerminalDirectionCorridor=true`，并保持 `predictionExtrapolated=false`。
- prediction 为空、位置/时域无效或工具禁用 prediction 时，回退真实尾方向与模型真实点末端时间窗速度：96 DPI 下方向回看 12px、有效方向至少 4px、绝对距离不超过 32px，沿用 35°、自适应距离和速度比规则；模型时间窗不可用时才使用滤波 RTS 末速，并按当前间隔与绝对上限裁掉无意义尖峰。
- 多候选按归一化预测落点误差、实际选择走廊夹角、距离、较新 Up 的顺序选择；回退候选首排序量仍为实际/预测距离比例误差。命中时旧 handle 回收，新 handle 接管原 runtime，新 Down 以连续时间作为 `kMove`；不得 Reset modeler 或宽度状态。
- 最多保留 8 个候选；超限先以保存的 Up 完成最旧候选。仅剩候选时结束 1ms timer period，并等待新 Down、控制 wake 或最近 deadline。
- 超时后才发送真正 `kUp`，随后沿用同帧批量 L2 resolve、活动层重建、指标提交、handle 回收和 runtime Reset。resize 重建候选，clear 最多额外等待当前候选剩余窗口。
- RTS 断触注入只用于人工测试：开启时使用固定 32 contact 状态和合成 contact id 随机生成 Up→丢弃 20–70ms Move→新 Down；关闭时必须由 `if constexpr` 选择原始 coordinator 直达分支，空模拟器不得查询频率、生成随机数、加锁或输出日志。
- `kInterruptedStrokeReconnectManualTestModeEnabled` 与 `kInterruptedStrokeReconnectSimulationEnabled` 的正式默认值均为 `false`。人工测试开关关闭时恢复笔尾倒转橡皮，并由 `if constexpr` 移除绿色桥接覆盖和拒绝诊断；模拟开关关闭时不进入任何合成 contact 热路径。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Up 无有效末速/方向 | 立即发送 `kUp`，不进入候选 |
| Cancelled | 立即终结，不可续接 |
| 设备、工具或笔状态不一致 | 新 Down 创建独立笔画，旧候选继续等待 |
| prediction 路径超过时间、动态距离上限或预测落点误差 | 拒绝续接；正常模式不输出逐候选日志，人工测试模式只输出最近候选的一行诊断，并带新/候选 contact id 与 generation |
| 冻结预测端点失败但方向/速度连续且输入末速更高 | 尝试一次纵向/横向分离的加速走廊；命中设置 `predictionExtrapolated=true` |
| 预测弦角超过 35°但预测末端速度方向在 35°内 | 使用相同距离、速度比和纵横误差约束尝试末端方向走廊；命中同时设置 `selectedTerminalDirectionCorridor=true` |
| 新 Down 仍在预测时域内且预测端点明显偏短 | 仅在 `≤35ms`、末端方向 `≤15°`、速度比 `[0.5,2.0]` 和纵横误差均通过时使用严格终速走廊 |
| 预测弦和末端速度方向都超过 35°，或末端方向无效 | 不使用第二走廊，保留原拒绝结果 |
| 加速走廊角度超过 35°、速度比越界或方向不可靠 | 不尝试加速走廊，保留原拒绝结果 |
| prediction 为空/位置或时域无效/禁用 | 使用真实尾方向、模型尾部时间窗速度和 32px 保守上限；时间窗速度不可用才回退滤波 RTS 末速 |
| 暂留 `kMove` 失败 | 立即回退真正 `kUp`，不得遗留候选 |
| 续接 `kMove` 失败 | 新 Down 正常建笔，旧候选继续等待至超时 |
| 第 9 个候选进入 | 最旧候选同帧完成，候选数恢复为 8 |
| 80ms 到期 | 用保存 Up 发送 `kUp` 并进入既有完成批处理 |
| 断触注入期间物理 Up | 不生成恢复 Down，已合成 Up 的候选自然超时 |

### 5. Good / Base / Bad Cases

- Good：Pen 沿预测曲线 50ms 后恢复 Down；预测终点切线即使已经转向，只要新 Down 落在预测位移走廊内且桥接不超过当前速度自适应上限，原 qpc origin、modeler、真实点和宽度状态连续。
- Good：预测只覆盖 17ms、实际间隔 70ms，但预测弦与桥接夹角 10°、抬笔前输入速度高于预测平均速度且速度比约 1.0；冻结端点失败后按加速走廊命中。
- Good：圆弧桥接与 19ms 短预测弦偏差 68°，但与预测末端速度方向偏差 30°，速度比和纵横误差均通过；第二方向走廊命中且不提高全局 35°。
- Good：波浪线在 30ms 内恢复，预测仍覆盖该时刻但位移严重偏短；桥接与末端速度方向偏差 1°、速度比 1.75 且纵横误差通过，严格时域内走廊命中。
- Base：未命中的普通笔、荧光笔、橡皮及 Touch/Mouse/Pen 都按独立笔画处理，旧候选按 deadline 正常收尾。
- Bad：物理 Up 立即 `kUp` 后尝试 Reset/拼接新模型，或用预测终点瞬时切线对整段曲线桥接弦做 35° 硬拒绝。
- Bad：把 `matchScore` 全局放宽以接纳加速样本，导致 90° 以上的人工重新落笔一起误连；或模拟开关关闭后仍在每个 Move 上查状态和加锁。

### 6. Tests Required

- 精确覆盖 80ms、prediction 位置时域插值/末状态、预测位移与终点切线反向仍命中、预测落点误差与数值容差、时域外不确定度、64–256px 动态预测上限、35°/32px 回退边界、DPI 缩放和 RTS 速度尖峰抑制。
- 覆盖高速直线略超基础上限、慢到快圆弧/波浪线加速走廊命中、预测弦超过 35°但末端速度方向走廊命中、时域内 35ms/15°/[0.5,2.0] 严格终速走廊、对应间隔/角度/速度比越界拒绝，并断言 `predictionExtrapolated`、两个终速走廊标记与横向/纵向误差。
- 分别以模拟开关 true/false 构建；false 的 Release 编译必须选择原始 Down/Move/Up 直接发布分支。
- 覆盖四类 `InputDeviceType`、三类工具以及全部身份字段不一致拒绝。
- 覆盖预测距离比例误差/角度/距离/Up 时间的多候选确定性选择、候选上限和超时策略。
- 同一 modeler 依次接收 `Down → Move → 暂留 Up(kMove) → 新 Down(kMove) → Up`；橡皮使用 Disabled predictor。
- 静态检查功能关闭时保留旧队列顺序，仅候选时 timer period 已结束且 deadline 可自行唤醒。
- 实体 Pen/Touch/Mouse 验证续接、主动分笔不误连、快速断触、resize、clear 和三类工具；`SendInput` 不能替代 RTS 硬件验证。

### 7. Wrong vs Correct

Wrong：`收到物理 Up 就发送 kUp；新 Down 命中后 Reset 一个模型并手工画直线桥接。`

Correct：`Up 暂作 kMove 并保存快照；命中后把新 Down 作为原模型的连续 kMove，超时才发送保存的 kUp。`

Wrong：`用目标预测状态的瞬时 velocity 方向比较 physicalUp→newDown 的整段弦；曲线跨过拐点时接近反向就拒绝。`

Correct：`Up 的 kMove 后立即冻结 prediction；把 modeledUp→selectedPredictionPosition 的位移平移到 physicalUp，按 newDown 到预测落点的误差判定，prediction 不可用时再回退真实尾和 32px 保守策略。`

Wrong：`预测端点太短时直接把圆形容差整体乘大、继续用固定 64px 提前拒绝高速直线，或无条件沿预测方向外推。`

Correct：`先执行冻结端点判定并完成诊断；仅在方向可靠、夹角≤35°、速度比有效且目标超过预测时域时，用抬笔前输入速度修正纵向距离，并分别限制纵向和横向误差。`

Wrong：`圆弧短预测弦超过 35°后直接拒绝，或把全局角度提高到 50°。`

Correct：`保持预测弦走廊不变；其失败后只在预测末端速度方向有效且仍满足 35°、速度比、动态距离和纵横误差时，尝试第二方向走廊。`

## Scenario: RTS Stylus State And Device Width Modes

### 1. Scope / Trigger

修改 RTS desired packet、tablet property metadata、`ContactSnapshot` 笔状态、Stroke Modeler 输入或普通笔宽度来源时，必须应用本契约。

### 2. Signatures

- `ContactSnapshot::{pressure, tilt, orientation}`
- `InputWidthMode { Fixed, SimulatedPressure }`
- `PenInputWidthMode { Fixed, SimulatedPressure, HardwarePressure }`
- `InputWidthModeSettingsState::Set/Get`
- `DrawingController::SetInputWidthModeSettings/GetInputWidthModeSettings`
- `ResolveStrokeWidthMode`、`HardwarePressureDiameter`

### 3. Contracts

- RTS 必须要求 X/Y；NormalPressure、X/Y Tilt、Azimuth、Altitude 是可选属性，metadata 在慢路径缓存 index 和 `PROPERTY_METRICS` 后一次发布。
- Pen snapshot 使用模型单位：pressure `[0,1]`、tilt `[0,π/2]`、orientation `[0,2π)`；未知为 `-1`。Touch/Mouse 三项保持未知。
- 压力按 `(raw-logicalMin)/(logicalMax-logicalMin)` 归一化，不写死硬件级数。角度优先 Azimuth/Altitude，缺失时由 X/Y Tilt 推导。
- 三类设备设置编码在单个 32 位原子快照中；设置更新只影响之后 Down 的普通笔，活动笔画不切换。默认 Mouse/Touch 模拟、Pen 硬件。
- 非普通笔工具始终固定宽度。Pen 硬件模式 Down 无压力时锁定回退模拟；真实直径为 `base × (0.2 + 1.2 × pressure)`。
- pressure/tilt/orientation 即使在固定或模拟宽度模式也传入模型；坐标未移动但笔状态有效变化时不得被原始移动阈值丢弃。
- 预测点冻结最后真实半径，再应用既有 L0 taper；不得用预测 pressure 改写尾宽。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| 扩展 desired packet 请求失败 | 记录慢路径诊断并重试仅 X/Y |
| 可选属性缺失、单位/分辨率无效 | 对应字段为 `-1`，输入继续工作 |
| `logicalMax <= logicalMin` | pressure 为 `-1`，禁止除零或猜测级数 |
| Pen Hardware Down pressure 无效 | 整笔使用 `SimulatedPressure` |
| Hardware 笔画后续 pressure 暂时无效 | 沿用上一有效值，不中途切换模式 |
| runtime 设置包含无效 enum | setter 返回 false，原子快照保持不变 |
| 仅 pressure/tilt/orientation 变化 | 进入模型但不更新位置速度基准 |

### 5. Good / Base / Bad Cases

- Good：MPP2.0 的 4095 logical max 与更高级数设备都归一化为同一曲线，倾角沿笔画进入模型，运行时切换只影响下一笔。
- Base：无压感 Pen、Mouse 和 Touch 继续通过固定或模拟模式绘制。
- Bad：把 raw pressure 当 4096 分母、在 Move 回调查询 COM，或用三个独立原子发布成组设置。

### 6. Tests Required

- 4095/8191 范围、越界夹取、无效范围；degrees/radians、Azimuth/Altitude、X/Y Tilt 和环绕。
- contact Down/Move/Up/竞争/复用中的三字段一致快照。
- Mouse/Touch/Pen 模式矩阵、无效 setter、Down 缺失回退、后续缺失保持。
- pressure 0/0.5/1 对应 5px base 的 1/4/7px，短点击使用 Down 半径，预测半径等于最后真实半径。
- 实体 Pen 验证轻重压、倾斜/旋转、抬笔；普通鼠标自动输入不能替代硬件结果。

### 7. Wrong vs Correct

Wrong：`pressure = packetValue / 4096.0f；每帧按当前设置重新选择宽度模式。`

Correct：`按当前 tablet PROPERTY_METRICS 归一化；Down 时从单原子设置快照解析并锁定整笔模式。`

## Scenario: RTS Inverted Pen Eraser

### 1. Scope / Trigger

修改 RTS `StylusInfo`、`ContactSnapshot` 倒转状态、笔尾橡皮开关、contact 工具覆盖或倒转 Pen 模型输入时，必须应用本契约。

### 2. Signatures

- `ContactSnapshot::isInvertedCursor`
- `StrokeModelConfiguration::invertedPenEraserEnabled`
- `DrawingController::SetInvertedPenEraserEnabled/GetInvertedPenEraserEnabled`
- `ShouldUseInvertedPenEraser`
- `ResolveStylusPressureForModel`

### 3. Contracts

- RTS Down、Move、Up 从 `StylusInfo::bIsInvertedCursor` 发布倒转状态；只有 `InputDeviceType::Pen` 可置为 `true`，且该字段属于 contact seqlock 一致快照。
- 开关默认开启并由单原子 Set/Get 更新；绘制线程只在 Down 读取，活动 contact 不切换语义。
- 开关开启时，倒转 Pen 只覆盖当前选择为 Pen/Highlighter 的 contact 为 Eraser；已选 Eraser、Mouse、Touch 不受影响。
- 每个 runtime 必须分别保存批次 `selectedTool` 和 contact 有效 `tool`。同批后续 contact 继承前者，禁止从倒转覆盖后的 Eraser 继承。
- 倒转 Pen 无论开关状态都锁定屏蔽 pressure；Down、Move、Up 模型输入为 `-1`，倾角和方位角继续传输。开关关闭后普通笔 HardwarePressure 因 Down 压力未知回退 SimulatedPressure。
- 倒转橡皮复用现有 50px 固定宽度、禁用 prediction、真实点直接提交 L1 的路径；不得提前引入压感橡皮语义。

```cpp
const bool supportsOverride = selectedTool == DrawingTool::Pen ||
    selectedTool == DrawingTool::Highlighter;
effectiveTool = ShouldUseInvertedPenEraser(deviceType, inverted, enabled, supportsOverride)
    ? DrawingTool::Eraser : selectedTool;
modelPressure = ResolveStylusPressureForModel(deviceType, inverted, rawPressure);
```

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Pen + inverted + enabled + Pen/Highlighter | 当前 contact 锁定 Eraser，pressure 为 `-1` |
| Pen + inverted + disabled | 保持选择工具，pressure 仍为 `-1` |
| Pen + inverted + selected Eraser | 保持 Eraser，不产生第二种工具语义 |
| Pen + non-inverted | 保持选择工具和原压力链 |
| Touch/Mouse 即使标志异常为 true | 不触发倒转橡皮或压力屏蔽 |
| Move/Up 标志相对 Down 变化 | 保持 Down 锁定的工具与压力策略 |
| 倒转 contact 后同批正常 contact Down | 继承原 `selectedTool`，不得继承有效 Eraser |

### 5. Good / Base / Bad Cases

- Good：MPP2.0 翻转后直接使用现有橡皮路径，关闭开关后笔尾可用模拟压感书写，同批正常 contact 仍按原工具绘制。
- Base：不支持倒转标志的 Pen、Mouse 和 Touch 保持既有行为。
- Bad：把倒转后的 `tool` 当作批次选择继续传播，或仅在 Down 屏蔽压力而在 Move/Up 恢复硬件压力。

### 6. Tests Required

- 倒转字段在 Down、Move、Up、竞争读取和 slot 复用中的一致性。
- device/inverted/enabled/tool-eligibility 策略矩阵；倒转 pressure 为 `-1`，HardwarePressure 回退 SimulatedPressure。
- ARM64 `Debug|ARM64` 完整解决方案构建与控制台测试。
- MPP2.0 真机覆盖 Pen/Highlighter 翻转擦除、开关关闭后的笔尾书写、抬笔和恢复正常笔尖。

### 7. Wrong vs Correct

Wrong：`runtime.tool = Eraser; nextBatchTool = runtime.tool; model.pressure = snapshot.pressure;`

Correct：`runtime.selectedTool` 保留批次选择，`runtime.tool` 只保存当前 contact 覆盖；倒转 Pen 整笔向模型传 `pressure=-1`。

## Three-Layer Contract

- `L2`：已完成笔画的最终 premultiplied RGBA 画布，背景真透明。
- `L1`：共享临时操作层，合并全部活动 contact 已稳定的前缀操作。
- `L0`：共享临时操作层，合并全部活动 contact 当前帧仍会变化的真实尾部、预测与笔锋；每帧只恢复一次单位操作再重画。

应用绘制 cursor 虽属于 L0 帧的瞬态视觉层级，但只在 backbuffer 最终合成阶段绘制；它不是 `layerL0` operator 的内容，也不得参与任何 L0→L2 resolve。

临时操作层表示：

```text
Result = Add + Retain * Below
```

`Add` 使用 BGRA8，`Retain` 使用 R16F；新层的单位操作是 `Add=(0,0,0,0)`、`Retain=1`。

绘制过程中，保护窗口之外的真实前缀推进到 L1。普通笔保护时间为：

```text
liveTipDuration + predictionDuration
```

contact 结束时由 `StrokeModelConfiguration::retainPredictionOnUp` 选择收尾，并始终先重建此前已经进入 L1 的稳定前缀。默认 `false`：采用模型运行到 `kUp` 后的真实尾段平滑完成笔锋，清除 prediction；设为 `true`：把 `previousL0DrawPoints` 中的真实尾部、prediction 和笔锋原样合入 L1，不再重连 `kUp` 尾段。只有 Down 后立即 Up、尚未生成建模点时，才使用初始点兜底生成点击或短段。同一帧的全部结束 contact 只执行一次 L2 resolve、一次 backbuffer composite 和一次 present。仍活动 contact 的 L1/L0 必须在清空临时纹理后从 CPU 状态重建。

## Scenario: RTS Multi-Contact Input And Rendering

### 1. Scope / Trigger

修改 RTS packet、contact 路由、跨线程队列、活动笔画、resize/clear、临时层合成或呈现时，必须应用本契约。

### 2. Signatures

- `ContactInputCoordinator::PublishDown/PublishMove/PublishUp/PublishCancelled`
- `ContactInputCoordinator::TryDequeue(ContactRecord*&)/WaitDequeue(ContactRecord*&)`
- `ContactInputCoordinator::TryReadSnapshot/Recycle/PublishControlWake`
- `ContactInputCoordinator::CaptureWakeGeneration/WaitForWake`
- `ContactInputCoordinator::DiagnosticsSnapshot`
- `RealTimeStylusInput::Initialize/Shutdown`
- `DrawingController::Run`

### 3. Contracts

- contact identity 是 `(tabletContextId, contactId, generation)`；route 必须通过 generation 和状态的原子 CAS 防止 ABA，记录内容用单写者 seqlock 发布。
- pool 容量是 `round_up_32(max(32, 2 × (SM_MAXIMUMTOUCHES + 2)))`，RTS 启用前一次性建立；每块 32 个 slot 和一个始终无锁的 `uint32_t freeMask`。CAS 清位取得 slot，record 保存不可变 owner/bit，回收顺序严格为 `ConsumerOwned → Free → release 置位`。
- ingress payload 是原生 `ContactRecord*`：非空只表示 Down，空指针只表示合并的 `ControlWake`。consumer 取到指针后立即读取 generation 并构造线程私有 handle；禁止恢复带 kind 的 command 包装。
- queue 容量是 `max(256, slotCapacity + 1)`。初始化阶段创建 32 个可 CAS 独占的 Down producer token 和 1 个 control token，并以三参数构造器禁止 implicit producer；热路径只能调用 token 版 `try_enqueue`，不得回退到会分配的 `enqueue`。
- contact 查找只遍历 `~freeMask` 的 occupied bits，但 generation/state/tcid/cid 仍是最终路由判据；位图只负责跳过空 slot，不能替代 route 校验。
- Down 成功入队后 consumer 才拥有 handle；Down 入队失败必须先关闭并排空可能已进入的 Move，再回收。Up/Cancelled 是 sticky terminal，成功关闭后后续 Move 不得覆盖。
- packet X/Y 按当前 `tcid` 的 `GetPacketDescriptionData` 返回顺序查找，但所有输入统一使用 `GetAllTabletContextIds` 首 context 的 ink-to-device scale 转为像素，保持与已广泛验证的 `IdtRts.cpp` 一致。禁止改用当前 Pen/Touch context 自己的硬件比例，否则坐标会落到画布外；同步回调不得分配、建模、绘制或记录无界逐包日志。
- RTS 多点启用是三段式契约：第一根手指按下前给 HWND 设置 `MICROSOFT_TABLETPENSERVICE_PROPERTY`，窗口过程对 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回 `TABLET_ENABLE_MULTITOUCHDATA`，并令 `IRealTimeStylus3::MultiTouchEnabled=TRUE`。只完成 COM 属性不能视为多点已启用。
- 同一组窗口标志禁用 press-and-hold、pen feedback 和 flick；可用时同时调用 `IRealTimeStylus2::put_FlicksEnabled(FALSE)`，避免笔事件被系统手势延迟或接管。
- `Disabled`、RTS `Error`、tablet 移除和 shutdown 都把生产中的 contact 发布为 Cancelled；COM 初始化、FTM 聚合、禁用、移除插件与释放全部在完成 MTA 初始化的主线程完成。
- 每个活动 contact 拥有独立 CPU runtime，其 GPU 几何共同重建到共享 L1/L0；不得提前进入 L2。完成 contact 的同帧批次只 resolve/composite/present 一次，随后重建仍活动 contact。
- resize 成功后重建活动临时层；clear 在有活动 contact 时延后。无活动 contact 时阻塞等待；1ms timer period 只在活动区间启用，且每次成功 begin 必须配对 end。
- waitable swapchain resize 必须先 `GetDesc1`，并原样传回 `BufferCount/Format/Flags`；部分驱动在传入零值时第一次 resize 成功、恢复尺寸时失败。
- 运行指标关闭时不创建会话、不启用输入计数、不写文件；开启后原始样本写入忽略的 `TestResults/`，仓库只保存环境、阈值和分位数摘要。

### 4. Validation & Error Matrix

| Event / failure | Required behavior |
|---|---|
| Down queue enqueue failure | 关闭 route、排空已进入 writer、回收；不得直接写 Free |
| pool exhausted / 32 tokens occupied | 拒绝 Down，禁止扩容、隐式 producer 或分配回退 |
| `ContactRecord* == nullptr` dequeued | 只解释为 `ControlWake` 并清 pending；不得解引用 |
| stale generation / duplicate recycle | route 校验失败且不重复置位，不得回收新一代 contact |
| Concurrent Move and Up/Cancelled | terminal 状态胜出，后续 Move 不覆盖终态 |
| Snapshot read during write | seqlock 重试，只接受前后相同的偶数 sequence |
| `MultiTouchEnabled=TRUE`，但 HWND 未 opt-in | 视为初始化契约不完整；Touch/多指可能完全没有 callback |
| `WM_TABLET_QUERYSYSTEMGESTURESTATUS` | 返回多点 opt-in 与禁用 press-and-hold/flick 的固定标志，不调用外部 COM |
| 第一份或突变的速度样本 | 从当前直径平滑追随，不回写已可见点，不允许单帧直接跳到目标直径 |
| Disabled / Error / tablet removal | 所有 producing contact 以 Cancelled 结束 |
| Resize succeeds | 保留 L2 交集并从 CPU runtime 重建全部活动 L1/L0 |
| waitable swapchain second resize | 保留原 swapchain 描述字段；不得用 `ResizeBuffers(..., UNKNOWN, 0)` 丢弃 flags |
| Clear with active contact | 延后到活动集合为空 |
| Multiple Up in one frame | 一次 L2 resolve、一次 composite、一次 present |
| Up arrives after a visible prediction | 默认稳定前缀连接模型 `kUp` 真实尾段且清除 prediction；开关启用时才原样烘干最后可见 L0 |
| Present failure | 保持整画布重呈现请求，下一帧恢复 |

### 5. Good / Base / Bad Cases

- Good：32 个同步生产者使用显式 token 并发 Down，slot/pointer 唯一；两支笔交错移动并同帧抬起，终态完整且批量提交。
- Base：单 contact 的 Down/Move/Up 使用相同 generation、seqlock、pointer ingress 和批量渲染路径；慢速到快速过渡时宽度连续。
- Bad：容量耗尽后调用隐式 `enqueue` 扩容，或只设置 `IRealTimeStylus3::MultiTouchEnabled` 却不处理 HWND opt-in。

### 6. Tests Required

- `Debug|ARM64`、`Release|ARM64` 全解决方案 Rebuild，且两个 shader、C++ Modules、资源嵌入与最终链接成功。
- `Release|x64`、解决方案 `Release|x86`（项目映射 Win32）Rebuild 并运行测试，验证 8/4 字节指针 payload 和 lock-free 静态断言。
- 自动并发覆盖 32 个生产者、32/64/多 block 容量、耗尽/复用、无分配 Down、Move/Up 竞争、stale generation、重复回收、Cancelled/shutdown 和 ControlWake/Down/终态唤醒。
- 静态检查窗口属性、`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 与 `IRealTimeStylus3` 三处多点 opt-in 同时存在。
- 静态验证 generation/state CAS、sticky terminal、seqlock、零自旋阻塞等待、timer begin/end 配对和 Release 无逐帧日志。
- 真机验证鼠标宽度连续、Pen/Touch 单 contact、双 Touch 交错、同时抬起、活动时 resize、活动时 clear、设备禁用/拔出、长时间 idle CPU 和最终点位置。普通 `SendInput` 不能替代 RTS 硬件验证。
- Release 自动基准至少连续三轮：即时工具 Down→Present p99 ≤ 8.33ms，活动帧间隔 p99 ≤ 9.5ms，>16.67ms 比例 <1%，连续空闲至少 4.9 秒且 frame/Present 零增长。
- 快速曲线末端抬笔时，默认的模型 `kUp` 收尾无 prediction 残留、回头或重复连接；开关启用时上一帧预测端点仍保留且不重复连接 `kUp` 尾段。

### 7. Wrong vs Correct

Wrong：`收到 Up 就立即把该 contact resolve 到 L2 并 Present；槽位只靠 contactId 复用。`

Correct：`route 用 generation+state 精确交接；同帧全部 terminal contact 先完成几何，再统一 resolve/composite/present，并重建剩余活动层。`

Wrong：`Up 后同时绘制新的真实尾部与上一帧留下的 predictedPoints，把两种收尾接在一起。`

Correct：`重建已提交稳定前缀；默认只连接模型 kUp 真实尾段，retainPredictionOnUp 启用时只烘干 previousL0DrawPoints。`

Wrong：`put_MultiTouchEnabled(TRUE) 成功，所以窗口已经能收到多指。`

Correct：`HWND 属性、WM_TABLET_QUERYSYSTEMGESTURESTATUS 返回值和 IRealTimeStylus3 三处同时 opt-in，再用实体 Touch/Pen 验证 callback。`

Wrong：`IngressCommand{ kind, record } 入队；token try_enqueue 失败后调用普通 enqueue。`

Correct：`非空 ContactRecord* 只表示 Down，nullptr 只表示 ControlWake；全部 producer 在初始化期建 token，热路径只调用 token try_enqueue。`

### Visual Prediction Is Not Persistent Ink

上面的抬笔规则只描述当前测试程序的瞬时视觉画布：

- prediction 是瞬时视觉结果。
- L2 是当前已落定的像素画布，不是正式墨迹持久化记录。
- 正式持久化原则上从已确认输入生成。
- 最终 prediction 必须被后续真实采样替换，或经过明确定义的提交动作转为已确认数据。
- 未确认预测点不得无条件写入永久笔迹、回放记录或跨版本格式。

> **待验证**：仓库尚无正式 `InkStrokeRecord`、预测来源标记或显式提交协议。未来持久化任务必须单独定义数据结构、替换/提交条件和兼容行为，不能直接复制当前 L2 的视觉提交语义。

## Scenario: Persisting Stroke Samples

### 1. Scope / Trigger

当任务新增 `InkStrokeRecord`、保存、导出、同步、回放或从 L2/活动 contact runtime 提取永久笔迹时，必须应用本契约。

### 2. Signatures

当前仓库没有正式持久化 API 或记录结构。未来任务必须先定义明确的写入签名和数据结构；禁止把活动 contact 的 prediction 或 L2 像素读取隐式当成持久化接口。

### 3. Contracts

永久样本只能来自以下状态之一：

- 已确认真实输入；
- 由后续真实输入替换后的 prediction；
- 经过明确定义的提交动作转为已确认的数据。

仅为当前帧展示而生成的 prediction 保持 transient，不进入永久记录。

### 4. Validation & Error Matrix

| Input state | Persistence behavior |
|---|---|
| Confirmed real sample | 可以写入 |
| Prediction later replaced by real sample | 只写替换后的确认结果 |
| Prediction explicitly committed by future protocol | 按该协议写入并保留可解释的确认语义 |
| Unconfirmed prediction | 拒绝或省略，不得静默写入 |
| L2 pixel result | 只能作为视觉结果，不能自动还原为永久 stroke record |

### 5. Good / Base / Bad Cases

- Good：持久化层只接收已确认样本，并测试 replacement/explicit commit。
- Base：在尚无持久化结构时保持 prediction 仅用于 L0/L2 视觉流程。
- Bad：抬笔时直接把 `predictedPoints` 全量序列化，因为它们当前可见。

### 6. Tests Required

- 未确认 prediction 不出现在保存记录中。
- 真实采样到达后替换对应 prediction，回放使用确认结果。
- 显式提交协议存在时，提交条件和记录解释可重复。
- 保存后回放不依赖当前 predictor 参数才能解释旧数据。

### 7. Wrong vs Correct

Wrong：`L2 已包含最终 prediction，所以永久笔迹直接保存 predictedPoints。`

Correct：`L2 只表示当前视觉结果；永久笔迹只接受真实确认或显式提交的数据。`

## Highlighter Geometry

- 画刷固定为画布 Y 轴方向的 `6.25×50px` 矩形，指针位于矩形中心；路径、笔倾角和设备方向都不旋转画刷。
- 连续点距离不超过 `0.25px` 时去重；累计位移超过阈值后再生成新 sweep。
- 单点生成一个居中矩形；每对相邻点生成该矩形沿中心线平移的凸扫掠区域。
- shader 以 X/Y 轴向边界和线段法线边界的半平面交集计算 sweep；零长度退化为矩形 SDF。
- 相邻 sweep 通过 Add/MAX、Retain/MIN coverage union 连接，不生成 round join、cap 或 short mark primitive。
- CPU bounds 与 GPU AABB 都使用 `halfSize=(1.25,25)`，并分别保留现有 `3px` dirty padding 与 `2px` shader quad padding。
- Down 起允许 L0 点击矩形和 prediction；`12px` 不再是可见性、完成态或几何分支。
- L1 只增量缓存稳定 sweep，Up 只重放缓存和最后一帧 L0；从未生成 L0 的同步点击仅补一次单点矩形。

上述尺寸和去重值仍属于实验参数。修改任一值时必须同时验证 CPU bounds、HLSL coverage、L0/L1 切片、缓存完成态和单点行为。

## Dirty Rect Contract

- 每个 rect 在资源操作前裁剪到当前画布。
- L0 更新的 dirty rect 是上一帧 L0 与当前 L0 的并集，确保旧预测被清除。
- 第一帧、窗口重新暴露或呈现失败后的恢复使用整画布。
- stroke dirty 同时包含稳定提交和最终 live 几何。
- resize 只保留新旧画布左上角交集，不缩放历史墨迹。

## Transparent Presentation Contract

- 绘图窗口用 `SW_HIDE` 预设创建；完成 presenter 初始化和首个透明画布提交后才显示。禁止在初始化期间先暴露 HiEasyX 白色窗口类背景，也不得为此直接修改第三方 HiEasyX 源码。
- DirectComposition swapchain、visual tree 和 `Commit` 全部成功，不代表驱动一定按 premultiplied alpha 合成；透明正确性必须通过真实桌面背景验证。
- 默认优先 DirectComposition；当前 QCOM ARM64 适配器优先使用 `UlwDirtyRect`，因为实体设备已观察到 DComp 透明像素显示为黑色。
- 适配器专用回退只改变尝试顺序，不移除后续模式；首选模式失败时仍按既有清理和回退协议继续。
- ULW 的 `1/255` alpha 只存在于 CPU 输出副本以维持窗口命中，不能写回 L0/L1/L2 或改变墨迹 alpha。
