# Research: 设置开屏页面缓存与无限速离屏基准

- Query: 书写完成后截取不含艺术字标/遮罩的真实页面，渐退与归位复用截图；确认最小插入点、生命周期失效、WARP/HARDWARE 离屏测量与现有限帧。
- Scope: mixed；只读设置生产入口、生产 DX11 backend、现有无窗口测试和上一任务记录；外部仅 Microsoft D3D11/DXGI API 文档。
- Date: 2026-10-14

## Files Found

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 常驻 ImGui coroutine、标题栏、正文/导航、后台过滤、DX11/Present 及资源恢复 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.h` | 纯 CPU HomeMotionState、Preparation 值键、统一 2.66s 时序与入口接口 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp` | Begin/Complete/Filter、真实 RenderHome、布局门禁及单份 foreground 字标 |
| `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm` | 共享设备 lease；Setting 独立 discard swap chain/RTV、resize/hide 清理 |
| `Inkeys/Inkeys/UI/Setting/Setting.Theme.h` | Solid/Mica/Acrylic 实际 backdrop 模式 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | 默认 WARP 共享 epoch 与唯一串行 60 FPS 调度器 |
| `Inkeys/additional/imgui/imgui_impl_dx11.cpp` | 真实标准 blend、clip 转换、动态 atlas 更新、VB/IB 上传及 HRESULT |
| `InkeysHeadlessTests/setting_home_warp_tests.cpp` | 现有真实首页/导航/字标 FL11.0 WARP 离屏 Frame、RGBA 回归、benchmark |
| `InkeysHeadlessTests/setting_design_tests.cpp` | 打包字体/真实三语言设置测试；scale=1 时先运行 WARP probe |
| `InkeysHeadlessTests/animation_tests.cpp` | `--no-window`、测试主入口；现有 `--benchmark` 仅额外算法基准 |
| `.trellis/tasks/10-10-setting-home-design/entry-performance-revision.md` | 上轮授权、范围、时序及有限准备方案 |
| `.trellis/tasks/10-10-setting-home-design/entry-performance-implementation.md` | 上轮实现及 Debug profiler 边界 |
| `.trellis/tasks/10-10-setting-home-design/research/entry-performance-research.md` | 上轮成本分析及 AutoResize/atlas 注意事项 |
| `.trellis/tasks/10-10-setting-home-design/research/entry-performance-validation.md` | 上轮 WARP 和 native 实测，不是本轮实测 |

## Findings

### 1. 当前流程和可以复用的接口（直接确认）

- `Setting.Pages.h:78-82` 统一 wait240ms/write1060ms/hold600ms/dock540ms/settle220ms，总逻辑时间2.66s。书写结束是 `WritingEnd=1.30s`，渐退开始 `DockStart=1.90s`。
- `Setting.Pages.cpp:286-311` Begin 在正文生成前运行，先比较当前值键；两个暖帧准备后，不透明且未到 DockStart 时返回跳过正文/导航。到 DockStart 恢复两帧 child 布局才进入半透明。
- `Setting.Pages.cpp:323-348` Complete 以本帧编号去重；提交/字体失败回滚本帧时钟、重走有界准备。两不同成功完整帧才 ready；六次预算后收束，避免永久遮罩。
- `Setting.cpp:2434-2447` 真实输入取消在 Begin 之前；allowFreeze 对弹层、navigation block/flyout 门控。`2449-2450` 起正文和 `4027` 导航生成同处跳过分支中。
- `Setting.Pages.cpp:127-135` 活动入场的 RenderHome 保留字标占位、origin/width/visible，但不提交横幅字标；`565-609` 正常主页测量仍实际发生。因此捕获活动入场的正常背景 DrawData，排除 foreground，即自然得到无字标页面，无需给 RenderHome 加平行布局或把 motion 置空。
- `Setting.Pages.cpp:397-410` foreground 包含整客户内容区遮罩及唯一字标；`Setting.cpp:4044` 生成后 `4056` Filter 原地删背景。捕获必须发生在 Filter 丢失完整背景之前。
- `FilterHomeEntryDrawData` 当前仅在覆盖 alpha==1 时保留 root/titlebar/foreground，root clip 到 `clientContent.y`（`351-367`）；可以扩展为缓存有效的半透明/落定帧同样只保留这些当前帧列表。
- `Setting.cpp:4057-4091` clear、backend、Present 是统一提交入口，适合封装生产/测试共享的缓存 helper。保持原后端 HRESULT、失败后禁止 Present 残缺帧的规则。

### 2. 最小缓存方案：优先 GPU copy，避免普通 SRV 再混合（设计建议）

现有 ImGui renderer 的 RGB blend 是 SRC_ALPHA/INV_SRC_ALPHA、alpha 是 ONE/INV_SRC_ALPHA（`imgui_impl_dx11.cpp:599-604`）。透明 RTV 从 0 clear 后所得 RGB 已包含透明度；把截图按普通 AddImage 再送这个 blend 会额外乘 alpha。在 Mica/部分 alpha 区域即 double-alpha。浮点 unpremultiply + UNORM 再量化也不能凭空承诺精确 RGBA。普通页截图应直接用 GPU copy 恢复；不需要新增 shader、SRV、离屏 RTV 或更改 vendor blend。

建议最小产品边界是一个 Setting 专用 GPU cache helper，加 Pages 的有界 capture/restore 状态：

1. 常驻缓存只持有一张独立同设备 `ID3D11Texture2D`（DEFAULT usage、同 target Width/Height/Format/SampleDesc，copy-only 可 BindFlags=0），及值键/ready。通过当前 RTV `GetResource` 临时取得 backbuffer；不要在类内长期保留交换链后缓冲引用。
2. 在 WritingEnd 之后的 Hold 段恢复真实页面/导航，通常仍需两个不同完整成功布局/字体帧（AutoResizeY 缺席后需要）。只在 full layout、实际字形已处理、backend 成功后准备捕获；逻辑书写不因准备补帧提前结束。
3. 捕获帧在当前线程构造只存在该帧的浅层 background ImDrawData，排除 foreground，保持 `Textures` 队列；清原 RTV，再用生产 backend 画它。活动 RenderHome 已经省去横幅字标。
4. backend 成功后 CopyResource 到独立 cache；然后在同一 backbuffer 上仅提交 foreground，不再 clear，继续原 Present。生产只在正常提交/字体 ready 后 publish 对应缓存状态。可以同一 immediate context 排队 copy，不需 CPU 等待截图、不需生产 Flush/Map。
5. 缓存复用帧：原 clear -> CopySubresourceRegion 缓存的 `clientContent` 正文+导航区域至当前 backbuffer -> 只提交 root 上部/titlebar/foreground。标题栏继续实时，正文下部边框/透明像素保留截图原结果；遮罩和字标按原时钟推进。
6. copy 源 BOX 和 target 坐标以实际物理像素计算，并严格在 target 内 clamp。backend clip 转换是 `(ClipRect-DisplayPos)*DisplayFramebufferScale` 后 LONG 截断（`imgui_impl_dx11.cpp:375-382`），copy/body 门限应使用同规则，避免倍率时留一行缝或两次覆盖。
7. 在切回正常帧之前重走必要完整 child 暖帧；这几帧输出仍可从有效缓存复合，先测量/字体成功后再放开正常 DrawData。结束直接返回原静态页，不在 screenshot 上再重复叠静态字标。恢复预算失败须有限收束并复用原 Retry/DeviceLost；不能无限等待。

临时背景/前景 DrawData 只能借用本帧的 DrawList；不可跨帧保存 naked list、ImFontBaked、旧 UV、旧设备 SRV。GPU copy 保存最终像素，字体 atlas 后续 repack 本身不会破坏已缓存像素，但不同字体/语言/布局还是必须让缓存失效。无需改变 HomeBrand 原始网格或书写采样精度。

内存预算：R8G8B8A8 单张 1000x1100 约4.20MiB，1920x1400约10.25MiB，不含 driver 内部开销。每个活动 Show 最多一张；释放/替换时无累计图片历史。捕获与结束准备耗时必须单列，不能只报稳定缓存热帧收益。

### 3. 失效及生命周期（直接确认 + 必须约束）

已有 `HomeEntryFrameKey`（`Pages.cpp:269-283`）覆盖 DisplaySize、FramebufferScale、内容bounds、gutter、Pixels(1)、atlas/fonts 身份、epoch、fontSerial、language、dark、atlas UniqueID/宽高。Begin 键不同清准备且重验布局，不重播同一次 Show。需额外补齐实际 backdrop/clear 状态；dark 不等于 backdrop，Mica -> Solid 可不换主题（`Theme.h:12-17`、`Setting.cpp:2224-2225`）。

| 事件 | 代码证据/要求 |
| --- | --- |
| 新 Show | `Pages.h:133-143` Preparation 重置；缓存必须以相同 ShowSerial 清除或重建，不能重开复用旧页 |
| Hide | `Setting.cpp:4175-4179` 可仅 release presentation 且 coroutine 不 Resume；缓存 Reset 必须能从渲染线程此处同步访问，不能只期待下一次 Begin 清理。下一次实际帧 `2239` 消费 HideReset |
| 输入/弹层/离开 Home/动画禁用 | `Pages.cpp:413-423`、`Pages.h:145-149`；立即退出缓存绘制并保留该帧阻断输入屏障。正文 child 恢复首帧不得闪空/裁切，应有有界恢复安排 |
| 主题/实际 backdrop/clear 变更 | 主题在 `Setting.cpp:4261-4268` 帧界应用；比对主题和 clear/backdrop 值键。不可让旧浅色/旧透明截图露出 |
| DPI/用户倍率/字体重建 | `Setting.cpp:4248-4258` 在 coroutine 生成之前 invalidate backend/font；清 cache 或下次 Begin 先比新 key，失败恢复前不复用旧状态 |
| Resize | `Base.cppm:120-126` 先清 RTV 再 ResizeBuffers；释放任何临时 backbuffer 引用，cache 独立 texture 也应 invalid。新的 size/scale 值键必须先检查再 copy |
| 设备 epoch 重建/移除 | `Setting.cpp:4220-4245` 显式 teardown/rebind；必须 Reset 旧 cache texture，不能让独立纹理拖住旧设备或 cross-device copy |
| backend/Present/捕获失败 | `Setting.cpp:4072-4093` 原 Retry/DeviceLost；Complete 回滚/有界次数。资源创建用 HRESULT；CopyResource/CopySubresourceRegion 返回 void，不能假造成功 HRESULT，尺寸/设备/格式需前置保证 |
| Occluded/minimized | 现有暂停/恢复时钟保持，不新 Show。`4284-4291` probe 与 `4149-4153` pause；未真正提交成功不能消耗动画/发布新 snapshot ready |
| Session stop/shutdown | `Setting.cpp:4129-4139` Drain 和 coroutine末端 `4108-4117`；缓存必须在 device lease 释放前 Reset |
| 静态终态/尺寸不安全跳过 | `Pages.cpp:461-467` 和 Begin Finish；释放/invalid 缓存、保持 Show 已消费，本次放大不能补播 |

把 GPU cache 置 coroutine 局部虽然易 RAII，但 Hide/设备 epoch teardown 并不保证 coroutine Resume；若选择局部 ownership，必须另有可同步调用的 Reset 通道。纯 CPU HomeMotionState 不应因新增资源携带可复制 COM 所有权，测试 reference=state 的值拷贝应仍有效。

### 4. 无窗口基准可以复用，需把读回与吞吐分开（直接确认）

- `setting_home_warp_tests.cpp:78-112` Offscreen 显式 FL11.0、WARP、无 HWND/swap chain，使用生产 DX11 backend/嵌入 CSO，target R8G8B8A8_UNORM。
- `125-148` Submit 无论 pixels 是否 null，每帧都 CopyResource -> Map 等完成。该路径适合 RGBA 正确性及历史 completed+copy 对照；不能将它称为纯GPU耗时。
- `153-210` Frame 调用生产 Begin/RenderHome/navigation/overlay/Filter；与 `Setting.cpp` 完整 coroutine 是不同入口。夹具只有 root 上部填充，无真实 caption glyph/buttons，也没有 HWND、正常 swap chain Present，HomeContent 图片为0；不能把数据当整个原生窗口和所有应用组件。
- `852-916` Benchmark 20帧预热+100帧采样，按真实时刻变化模拟 writing/dock/settle；有CPU和completed+copy median/p95、vtx/idx/batch、ImGui allocation、brand capacity。
- `RunSettingDesignTests` 在真实字体加载后且CPU模拟 TexID 之前运行 probe（`setting_design_tests.cpp:3693-3742`）；完整 `--no-window` 会包含此测试（`animation_tests.cpp:1642-1643,1696`）。无需新工程/构建链。
- `--benchmark` 现有语义只额外跑动画算法；当前 homepage benchmark 即使无该flag也执行。硬件补充诊断可在同现有 test 文件增加 driverType 参数/独立窄入口，但要在全新ImGui session/font生命周期里初始化第二 backend；现有 probe `920-924` 要求 framecount==0，不能在已 materialized texture 同一context里再次调用原入口。

### 5. 建议无限速测量分段

保持上述 readback 路径用于 correctness。新增无 staging readback 的完成测量路径：

- CPU frame: `t1-t0`，覆盖 NewFrame/真实页面或缓存快路径/overlay/Render，不含 DX11。
- backend call: clear/copy复合和 `ImGui_ImplDX11_RenderDrawData` 所有调用的 CPU墙钟区间；明确是否包含 clear/copy 命令 CPU 提交，不把它称纯GPU。
- completed submit: `tDone-t1`；调用完成 event，Flush 将命令发出，GetData 等到 S_OK（有data时BOOL=TRUE），无 CopyResource 到 staging、无 Map。单列 wait-after-backend=`tDone-tBackend`。
- total: 每一帧 `tDone-t0`，直接存数组再求 median/p95；不能以 CPUmedian+completedmedian 假造 totalmedian。FPS 为1e6 / total-us 对应中位延迟；p95延迟对应慢帧FPS单独标明。
- 可选 timestamp: 一个 TIMESTAMP_DISJOINT Begin/End 包住两个 TIMESTAMP End，data Frequency!=0 且 Disjoint==FALSE 才计算设备时间；不支持或disjoint输出 unavailable/计数，不回落成CPU时间却继续写gpu-us。WARP是CPU软件光栅，设备时间标签必须注明WARP；GPU timeline elapsed也可能含CPU供给空隙，不单凭此字段宣称纯shader忙碌时间。
- 每帧 completion 串行等待测的是“无显示节拍限制的单帧完整处理速率”，不是多帧在途最大pipeline throughput。正好适用于本用户每阶段瓶颈分析，但报告边界须明确。
- 避免Sleep或production scheduler，dummy io.DeltaTime=1/60只用于ImGui逻辑不构成墙钟限帧；固定phase的逻辑delta=0同样不限制帧率。GetData等待设置合理有界超时，并检查FAILED与GetDeviceRemovedReason，不能无限busy loop。
- 至少报告 wait/writing/hold/capture/dock/settle/restore/static；writing、dock使用同一固定进度序列，新旧同字体/分辨率/renderer/phase配对。capture/restore准备单列，不能拿热缓存帧充当冷捕获成本。

HARDWARE 诊断只在测试里创建独立 D3D11 device、FL11.0，同字体/CSO/像素面积/phase序列；不调用生产 PrepareBackend/CommitPreparedBackend，不修改共享生产设备。读取 IDXGIDevice->GetAdapter->GetDesc，输出driver type、adapter description/vendor/device id、FL、物理Width/Height、FramebufferScale/DIP倍率、theme/backdrop模拟模式。硬件创建失败明确 unavailable，不静默 WARP fallback 又冠名硬件。

推荐双后端最小矩阵：WARP full对current cached，HARDWARE full对current cached；1000x1100是现有可比基准，若额外测实机1920x1400需同area对照，不能不同尺寸横比。可选去字标/仅背景消融应复用同Frame选择本帧DrawData子集，以真实GPU completion判断归位剩余耗时是否由字标复杂三角网格还是大面积clear/copy/遮罩填充造成；至少不能只因vertices多就断言瓶颈。

### 6. 目前确实有两个正常显示节奏限制（直接确认）

- `RenderPipeline.cpp:52` FrameInterval=16,666,667ns；`736-739` 如未到deadline sleep_until，下一deadline按本轮实际开始+interval。因此所有共享UI客户端被最多约60FPS节拍调度；慢批次超过该budget后不再额外等待一个完整16.7ms，不是简单两个sleep相加。
- `Setting.cpp:4085` Present(1,0)。discard模型 SyncInterval=1要求至少一个显示垂直周期，可能阻塞共享线程；实际显示器/DWM/驱动决定等待，不能仅从源码推出实机固定60Hz。
- `RenderPipeline.cpp:1133` 初始默认 `Backend::Warp`；`182-219` 仅显式选择Hardware才创建硬件device。本次离屏硬件性能为补充吞吐诊断，无法代表当前产品真正使用硬件。
- Offscreen 无Present/无Scheduler/无sleep，适合解开上述节拍后诊断。正常产品限帧与实际处理不过budget的低帧率是不同事实，优化缓存无需更改这两处策略。

### 7. 必要回归（最小且有意义）

1. 无字标/无遮罩截图：用同Frame background子集读回与单独真实背景对照；检查brand包络区域不能残留前景笔画，捕获时活动 RenderHome仍保留layout。
2. 浅深、Solid、实际透明clear、部分alpha：同一时刻full versus cached body+live title+foreground精确RGBA；copy方案理论不需放宽UNORM误差。首半透明、dock中间、settle、标题/body边界、外框、终态逐像素比较。若full布局色值动态不稳定，先定位动画/fixture生命周期，不能放宽全图阈值掩盖double-alpha。
3. key字段改变和backdrop/clear改变都不copy旧缓存；state对键变化保持已消费Show和进度、有界重新准备。epoch更换真实两个device验证拒绝cross-device缓存。
4. Resize/Hide->Show无中间帧、退出资源、重复Complete、CreateTexture失败/后端失败/occlusion：ready只在真实成功后发布，失败不推进或无限遮罩。
5. 输入取消首帧屏障和child恢复：真实Tab/Enter/鼠标、滚动保持原合同，至少验证末帧/取消后前两帧没有AutoResize裁切且静态精确回到baseline。
6. 性能热帧cache资源alloc=0；捕获次数有限、单张内存预算、热帧真实vtx/idx/batch缩减；ImGui计数继续明确不覆盖std::string/vector/driver。已有byte cache维持不采样贝塞尔/不降AA。
7. 完整 Debug|ARM64 Solution（含PptCOM依赖）、现有--no-window、i18n check、encoding/BOM/CRLF与diff检查。此代理不执行构建/测试，主会话统一执行。

## External References

以下于2026-10-14检索Microsoft主文档；本方案只使用D3D11 FL11.0基础接口，不新增D3D11.1专属API或库版本。

- [CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource)：GPU整资源异步copy；要求兼容资源。
- [CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)：GPU区域copy，无拉伸/混合；BOX/目的边界无效可导致移除设备。文档更新2024-11-18。
- [D3D11_QUERY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query)：EVENT只End，完成S_OK且BOOL=TRUE；TIMESTAMP必须配DISJOINT。
- [GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)：S_FALSE尚未完成；只能immediate context获取query data。
- [Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)：发出命令但本身异步，不等于GPU完成。
- [TIMESTAMP_DISJOINT data](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)：Frequency换算，仅Disjoint=FALSE时timestamp可信。文档更新2024-02-22。
- [Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)：discard模型SyncInterval与垂直同步的语义，源码Present返回耗时不等于屏幕scanout。

## Related Specs

- `.trellis/workflow.md`：研究持久化与角色隔离；本代理不读implement/check.jsonl。
- `.trellis/spec/native-desktop/index.md`、`rendering-and-ui.md`：设置唯一DX11 backend、共享WARP/串行所有权、正常60FPS、Show/Hide/input/字体事务与限次预热。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：完整Solution/当前原生ARM64、至少5分钟、Win7实机与源码兼容分开；MSBuild PATH/Path同PowerShell修正见用户AGENTS。
- 当前规范的“禁止产品新增离屏纹理”是上一轮任务范围。本轮用户明确要求页面截屏GPU缓存，最新授权覆盖此限制；仍保持无新device/thread/shader/timer/调度请求。由主会话在完成后同步定向规范。

## Caveats / Not Found

- 本研究只写本任务research文件；未运行GUI/Computer Use、构建、基准、Git操作，未修改产品、spec、task元数据或其他任务。
- 源码行号来自开始研究时的当前工作树，implement代理同时修改后可能位移。以上性能候选不是本轮实测瓶颈结论。
- 上轮日志显示1000x1100的WARP dock完成+copy中位约39ms，明显高于CPU生成约3ms，提示后台软件光栅/填充值得优先优化；其map读回与不同实机面积局限不能当成本轮无限制FPS。
- CPU高开销的归属可能在WARP软件执行、驱动工作或UI生成；只看backend call很短不足以排除后续软件工作。需本轮event完成、同area配对和硬件补充证据。
- 硬件GPU和timestamp是否可用需本机实际尝试；任何unavailable要报告。没有Win7实机或真实DeviceRemoved恢复，不应外推通过。
- 工作树/stash恢复由主会话独占核查；此代理按职责没有重复查Git或判断是否有遗漏暂存。
