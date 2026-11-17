未提供 `-o` 路径，且会话仅可读，因此未落盘。基准 `refactoring/draw@7047db94`；未修改文件、运行子进程、编译或 GUI。

**事实（带 file:line）**

基底：`Inkeys/Inkeys/Drawing/Draw3/`；以下 `X:n` 表示 `Draw3.X.cpp:n`。简称 C=DrawingController、R=Renderer、P=TransparentPresentation、H=HiddenWindowTest、G=StrokeGeometry。

1. **数据流**

`Run`（C:15506）→帧循环（C:21347）。资源均为窗口 W×H、单采样 DEFAULT Texture2D：

| 层 | 格式/视图 | 写入者→读取者 |
|---|---|---|
| L2 | BGRA8、RTV | 完成笔画/历史恢复→最终合成 |
| L1 | Add=BGRA8、Retain=R16F，各有 RTV/SRV | 已确认前缀→落定/最终合成 |
| L0 | 同 L1 | 实时笔锋/预测/形状→落定/最终合成 |

资源：R:340–365、471–490。L1 写入函数为 `CommitStablePrefixToL1` / `CommitEraserRealPointsToL1`（C:22679）；L0 为 `DrawL0LiveComposite` / `DrawActiveShapePrimitives`（C:23100）。完成笔画时 `ApplyOperatorLayers` 读 L1/L0 写 L2（C:22965）；历史 `RestoreComposition→ApplyOperator` 也写 L2（InkHistoryGpu:1370、1479、697）。

`CompositeLayersToBackBuffer`：局部复制 L2→最终纹理，`ApplyOperatorLayers` 采样 L1/L0，以 `Add+Retain×Below` 混合，随后画调试板、背景、激光、游标、标记（C:15288、23280–23338；R:136–201）。最终 RTV 正是 `GetBuffer(0)` 创建的视图（R:448–469）。

`PresentFrame→P::Present`（C:15335）：

- DComp：`CreateSwapChainForComposition→visual.SetContent→PresentSwapChain→Present1(0,0,&parameters)`；局部一个 dirty rect，全帧 count=0，无 scroll（P:942、705、1147–1161）。
- ULW：`UlwDirtyRectPresenter::Present→CopySubresourceRegion→Map→CopyAndInspectUlwDirtyRows→Unmap→UpdateLayeredWindowIndirect`；不 Present 交换链（P:504–630、1409–1426）。

2. **脏区与 FLIP**

`frameDirty` 从空 RECT 合入恢复、稳定增量、旧新 L0/游标、激光和调试变化；`UnionRectInPlace` 合为单包围矩形（C:21930、22732、23131–23180；G:1960）。

`forceFullPresent` 触发点均在 C：输出/内容版本 21406；翻页/清屏 19269；主题 15869、20837；场景回执 20303、20602；恢复/Resize/层重建 21501、21595、21606；外部请求 21627；视口 21161、21697；辅助透明校验 21975；辅助读回重建 23179。

ULW copy 保持原坐标，范围受源/Staging/DIB 共同限制；首次或重建后全读回。局部 `prcDirty=&dirty`，全帧或诊断强制提交时为空（P:540–576、628）。

**当前没有累计未提交矩形。**失败后 `SetFailure`，控制器请求恢复/全帧，下一轮重建并全量呈现（P:1414–1422；C:15372、21408–21502）。旧备忘中的“累计区域”不能视为已实现。

DComp 普通帧完整重建当前 dirty，非每帧整屏；底图来自 L2，不从前一 backbuffer 复制（C:15293）。未调用 `GetCurrentBackBufferIndex`。D3D11 运行时会轮换逻辑 buffer0 的身份，Present 后无需重新 GetBuffer；代码重新绑定 RTV（R:179、234）。这符合 [DXGI 官方说明](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi)，不能套用 D3D12 的显式索引规则。

3. **直接依赖索引**

按函数/测试块归组，覆盖交换链、最终纹理/RTV 的声明、创建、读写和释放：

- R:448–650、848–989、1059–1245；`Draw3.Renderer.cppm`:214–221、396–405。
- P:687–705、804、871、921–1161、1404–1426、1553；`Draw3.TransparentPresentation.cppm`:133。
- C:6551–6669、12105–12120、15288–15482、19256、21474、23262–23302。
- RendererDebugOverlay:175–267、299–466。
- RendererLaser:349–476；RendererPattern:67–98、215–600。
- RendererPrimitives:353–481；RendererSnapshot:182–287。
- ShapeRendererTests:125–158。
- H:2915–2978、3169–3436、3868–3903、3987–3995、4380–4387。
- Diagnostics:961–971；`Draw3.Diagnostics.cppm`:127。

辅助 DrawpadPresentation 的入口在 P:1409–1413，同样读取 `renderer->backBufferTexture`。独立设置窗口另有交换链：`UI/Setting/Setting.Base.cppm`:58、112、142–149；`Setting.cpp`:1527、5463。

4. **调试、快照与探针**

CanvasDebug 上传 CPU 板子/标签；板子 under、标签/标记 over 写最终 RTV，无生产读回（C:16550–16651；RendererDebugOverlay:141–267）。ULW 像素统计复用 Staging，分别检查原始像素和最终 DIB（P:212–239）。

`PixelDiagnosticShaders.h`:3 是测试字节码；H:3517 使用，比较 HWND/普通纹理（H:3987），EVENT 完成后才 `DO_NOT_WAIT Map`（H:3258–3283），不是生产策略。

RendererSnapshot 是 GPU 漫游兜底：L2→快照→最终 RTV，非 CPU 截图（RendererSnapshot:147–199、271）。PerformanceProbe 是 CPU 几何基准，无直接目标依赖（PerformanceProbe:138–223）；未找到生产截图直接读最终纹理入口。

5. **ULW 与生命周期**

每个 ULW presenter 一张全源尺寸 BGRA8 Staging；生产 `Map(READ,flags=0)`，无 DO_NOT_WAIT；持久 top-down 32bit DIB 按客户区尺寸复用（P:256、410–477、576）。

`ComposePrimaryHitUnderlayAlpha` 在 CPU 行复制中、Unmap 前叠黑色 alpha=1 命中底层，只改 alpha；仅主窗启用（P:189–221、1047–1050）。ULW 同步运行于 Draw3 绘制线程（Host:2391、2429；C:23338）。

DrawpadPresentation 独立 ULW，首次才建读回；隐藏回执、版本及必要空白证明通过后释放，Resize 也释放；下次全量重建，主交换链保留（P:1068、1393、1492–1511；Host:2037–2071）。

Resize：解绑释放目标→`ResizeBuffers`→重取 buffer0，保留 L2/L1 交集→Resize presenter（R:497–509、578–619；C:15482–15499）。

**运行期也能 DComp→ULW。**先释放 presenter/renderer/链；丢设备则重建设备并从首选模式重试，普通故障从失败模式重试再降级；强制模式不降级（P:921–927、1305–1368）。

**推断**

- **A：独立纹理。**已有不传 swapchain 就建普通纹理的分支（R:456–466）；仍须调整生产初始化、Resize、恢复、信息展示及测试。保持最终目标与合成顺序，应可保留画布调试、快照和像素读回，但需验收。
- **B：保留交换链。**目标相关改动较少，工作集中读回/提交；异步化仍须处理像素基线、未提交脏区、版本/隐藏回执和 Present 成功回执/计时（C:15345–15371；P:1471–1484）。A 自身不会消除 Copy/Map/CPU/ULW 串行。

**待实测**

ULW 设置 `WS_EX_LAYERED`、清 `WS_EX_NOREDIRECTIONBITMAP` 后创建双缓冲 HWND FLIP 链，却不 Present（P:130–140、930–955、1015–1026）。代码无法证明 layered 冲突、额外 DWM 重定向表面或 Win7 异常，也无法证明没有。

双缓冲名义像素量为 `2×W×H×4`；实际内存、驱动分配、A/B 延迟吞吐、残影以及 Resize/恢复/调试/辅助窗口交接均待测。接受 Win7 SP1+KB2670838 创建可用的用户实测前提，不建议改 DISCARD。