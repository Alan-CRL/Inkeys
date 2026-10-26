# Research: 设置静止与 Hold 阶段按需提交

- Query: 设置窗口能否在静止/完整字标Hold阶段安全Idle；若全事件唤醒不足，如何最小改动跳过精确相同DX11/Present提交并保留全部动态内容。
- Scope: internal；只读现有ImFluent/Design/ImGui动画、Setting消息与异步更新、生产帧提交及本任务新增页面缓存。
- Date: 2026-10-14

## Files Found

| 文件 | 说明 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | WndProc、FIFO业务完成Request、CPU帧、DX11/Present、epoch/字体/显隐恢复 |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h` | 显隐/resize/font/business serial及SessionDecision |
| `Inkeys/Inkeys/UI/Setting/Setting.Design.h` | NavigationTween::Active和导航六组可见状态 |
| `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp/.h` | 描述展开和滚动条tween分散在ImGuiStorage、Fluent控件包装 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp/.h` | entry等待/书写/Hold/归位、button pulse、parallax、输入取消 |
| `Inkeys/Inkeys/UI/Setting/Setting.HomeEntryCache.h` | 本轮GPU缓存capture/replay，与DrawData之外的copy输出 |
| `Inkeys/additional/imfluent/imfluent.cpp/.h/imfluent_internal.h` | 私有颜色/float动画、hover/tooltip、有限公开API |
| `Inkeys/additional/imgui/imgui.cpp/imgui_widgets.cpp` | hover延迟、menu延迟、时间推进及文本光标闪烁 |
| `Inkeys/additional/imgui/imgui.h` | ImDrawData/List/Cmd/Vert/Texture字段，实际版本1.92.7 |
| `Inkeys/additional/imgui/imgui_impl_dx11.cpp` | 纹理事务、callbacks、backend资源错误与实际绘制 |
| `Inkeys/additional/imgui/imgui_impl_win32.cpp` | 输入事件只入IO队列，无产品调度器Request |
| `Inkeys/Inkeys/Net/Net.Update.cpp`、`Net.Update.Download.cpp` | 异步自动更新状态和下载量，未发现Settings Request |
| `InkeysHeadlessTests/setting_home_warp_tests.cpp` | 同Frame正确性/吞吐夹具，可新增有限静止提交判断回归 |

## Findings

### 1. 核心结论

**建议本轮保留现有CPU帧和FrameResult::Continue，先只跳过精确相同的最终GPU输出。** 当前不存在统一可查询“所有控件未来无动画/延迟变化”的接口，普通客户区输入与一些异步页面状态也没有完整事件唤醒。仅因为连续两帧DrawData相同就返回Idle，会停住tooltip/caret、延迟菜单、hover动画、更新进度甚至首次输入。

继续CPUFrame意味着原NewFrame/IO消费/命中/鼠标cursor更新、所有timer与渐变、页面数据轮询保持原行为。若本帧最终输出等于最后一次成功呈现且没有资源事务/副作用，跳过Clear、page copy、VB/IB上传、DX11 Draw以及Present。节省WARP与Present成本，仍保留最多60FPS CPU逻辑节拍；这不是全窗口零CPU或真正事件Idle，最终报告须明确边界。

### 2. Pending动画可查询范围与缺口

| 动态来源 | 已有状态/代码证据 | 能否可靠聚合 |
| --- | --- | --- |
| 导航展开、group/flyout/content reveal与indicator x/y | `Setting.Design.h:76-102` NavigationTween有Active，`190-213`依CPU帧推进六组tween | 这些六组可查询，但不足覆盖全部UI |
| 主页entry | `Setting.Pages.h` EntryActive、entryElapsed、delta；统一时序 | Hold仍有600ms未来结束deadline，输出相同也不能停CPU时钟 |
| 主页parallax | `Setting.Pages.cpp:137-153`鼠标target、160ms指数跟随、小于0.0001归target | 部分可直接读x/y；仅输出稳定不能推未来输入 |
| 开源按钮pulse | `Setting.Pages.cpp:172-180` ButtonMotion hovered/elapsed，hover上升沿elapsed=0、有限0.32s | 可读，但仍不代表Fluent底层动画 |
| row描述展开 | `Setting.Controls.cpp:525-550` NavigationTween值/target/start/progress按每行ID放window ImGuiStorage | 无统一pending总量/注册表，只有局部重建tween |
| 滚动条装饰 | `Setting.Controls.cpp:269-285` window storage重建tween、frame连续性 | 同样分散，无公开pending查询 |
| Fluent颜色/float hover、toggle、按钮等 | `imfluent.cpp:662-718` static AnimateColorU32/AnimateFloat；每窗storage、私有seed/channel | 公开h/internal.h未发现pending/动画活动查询。U32取整可连续几帧相同仍未达target；指数float也可能极慢收敛 |
| 项目复制Fluent样式控件 | `Setting.Controls.cpp:802-837`本地static颜色/float helper | 第二套分散storage；查询vendor也不覆盖此套 |
| tooltip | `imfluent.cpp:3221-3233`IsItemHovered(ForTooltip)；`imgui.cpp:1532-1533,4892-4901`stationary/delay timer | 延迟前多个帧输出完全相同，随后才出现；CPU必须继续 |
| 文本caret | `imgui_widgets.cpp:5609-5614` CursorAnim+=DeltaTime，1.20s周期内0.80s可见 | 任意几十帧相同仍有未来闪烁；ActiveId!=0/WantTextInput只能作保守禁止Idle条件 |
| 延迟submenu/drag-hold | `imgui_widgets.cpp:584,9350` hover timer阈值和mouse stationary条件 | mouse停止不是静止承诺；仍需future CPU帧 |
| 滚动/拖拽/键盘repeat/IME/gamepad | `Setting.Controls.cpp:261-267,381-424`、ImGui帧IO及input state | 有滚动目标跨帧消费，keyboard重复与backend polling不只依WM消息；全聚合需广泛审计 |

不建议为了本轮Idle往vendor增加全局pending布尔：所有Local/ImGui/timer/外部数据仍需接线，易遗漏；也不建议扫描ImGuiStorage猜私有seed和target，违反最小diff并绑定vendor内部。

### 3. Request唤醒覆盖：并不完整

**明确有Request：**

- `Setting.cpp:1247-1293`caption按压/释放/hover/leave；`1300-1302`NCACTIVATE；`1373-1378`退出move/size；`1403-1413`主题/设置/activation；`1415-1432`resize。
- `Setting.cpp:782-789`每个FIFO业务命令执行完发布BusinessCompletion serial后Request Settings；`SessionState.h:181-209`消费serial不等于它自己唤醒，实际Request由业务worker完成。
- 初始化、Show/Hide、用户倍率font rebuild/主题按钮也有明确Request（`Setting.cpp:2174-2176,2393-2400,4383-4457`）。

**缺少或无法承诺完整：**

- `Setting.cpp:1331-1340`仅在可见时把消息交给ImGui_ImplWin32_WndProcHandlerEx。普通WM_MOUSEMOVE/down/up/wheel/key/char没有统一追加产品Request。
- `imgui_impl_win32.cpp:641-754`普通mouse/key消息只io.Add...，return通常0；该vendor不知道RenderPipeline。因此仅靠WndProc当前结构，全Idle后很多输入不能叫醒Setting。此前持续Continue使这个缺口不影响正常输入。
- WM_DPICHANGED排font rebuild并SetWindowPos（`Setting.cpp:1380-1396`），实际可能依随后WM_SIZE唤醒；若要全Idle必须显式审核这种间接依赖。
- 自动更新过程 `Net.Update.cpp:617,633-635,703-714`直接写AutomaticUpdateState/downloadLine；`Net.Update.Download.cpp:194,207`直接store/fetch_add下载量。模块中未发现Settings Request。版本页 `Setting.cpp:2737,2757-2763`每CPU帧读取并格式化它们，BusinessQueue启动更新的完成Request不足以覆盖后续下载进度。
- 未发现设置WM_PAINT/曝光/显示器变化统一force-render机制，可见性/occlusion必须继续原probe和资源恢复。进入真正Idle需要额外事件及deadline设计，超出这次最小提交优化。

### 4. 未发现可直接复用的稳定DrawData比较helper

对`Inkeys/Inkeys/UI`与`InkeysHeadlessTests`检索DrawData Hash/Compare/Equal/Snapshot、DrawSnapshot、FrameSignature等，未找到生产全DrawData精确判断。现有setting_home_warp像素对照、提取真实文字/字标、buttonBounds memcmp是专项测试，不是可直接拿来控制生产Present的helper。

可复用基础是当前Filter及HomeEntryPageCache的本帧输出选择：在**最终Filter后的实际提交数据**比较，不比较整个ImGuiContext或未提交后台UI。snapshot应自己拥有一份有限值数据；不保留跨帧DrawList、ImFontBaked、ImTextureData裸指针或callback data指针。

### 5. 最小精确判断合同

推荐仅Setting专用“小型上一成功提交快照”，不改通用RenderPipeline/vendor。对每个list按提交顺序比较：

- ImDrawData的DisplayPos/DisplaySize/FramebufferScale、Valid以及list数/每list Vtx/Idx/Cmd有效长度。
- 所有有效ImDrawVert的pos/uv/col与有效ImDrawIdx。默认Vert20字节（`imgui.h:3201-3207`）；若memcmp应确认没有自定义layout/padding，或逐字段比较。忽略vector Capacity、内部write pointer/frame计数。
- 每ImDrawCmd：ClipRect、实际GetTexID、VtxOffset、IdxOffset、ElemCount。字段定义见`imgui.h:3180-3196`，不直接memcmp整个cmd（含指针/填充/未来不相关字段）。任何UserCallback包括ResetRenderState直接禁止跳过，因其可能影响外部资源或提交状态。
- 原Clear颜色RGBA、实际backdrop/target尺寸/格式、epoch、font/theme/Show/Hide/resize相关状态纳入key，或在相关路径统一Reset，确保不能因相同字节对新的RTV跳过第一帧。
- screenshot replay属于DrawData外部copy，必须纳入缓存内容generation/key或强制capture/restore/重建提交。本轮HomeEntryPageCache仅serial=Show，若同一次Show重建纹理内容不能只比较ShowSerial。最小安全门禁是只在fully-ready、不pending任何prepare/capture/restore且cache状态未变化时允许比较；所有过渡帧强制。
- 比较哈希可作为快速拒绝，但任何哈希一致仍需exact bytes/fields比对。单个64位hash不应成为画面正确性的唯一依据。
- snapshot只能在backend成功且Present==S_OK后commit；失败/遮挡/epoch释放清快照。不能与“上一尝试生成帧”比较，否则失败帧相同后永不真正呈现。
- 大小/有限内存预算：只持一帧；热相同帧不重新alloc/copy；发生实际变化且提交成功后copy更新。超过明确预算保守继续提交，不做全局无上限缓存。

同数据、同静态纹理、同clear、同copy内容且没有callbacks是“这次绘制结果相同”的充分保守条件。其逆命题不成立：字段略变也可能最终像素相同，此时继续提交是允许的保守成本，不能为收益用epsilon忽略细微parallax/AA变化。

### 6. 必须先于跳过检查的资源和生命周期条件

- `imgui_impl_dx11.cpp:228-237`对Textures中所有非OK状态调用UpdateTexture；不仅WantCreate/WantUpdates，WantDestroy也有资源生命周期副作用。跳过前任何非OK都force正常backend，不能只比较GetTexID，因为同TexID内容更新也可能改变输出。
- 先判断texture readiness再GetTexID；pending字体时backend原合同不允许GetTexID，快照计算不能提前触发断言。成功backend后再制作用于下次比较的实际纹理ID快照。
- ImGui_ImplDX11_NewFrame资源错误或ImGui_ImplDX11_GetLastResult失败时不允许走duplicate-success，应沿原ResolveSettingResourceFailure。可以保持每CPU帧NewFrame，因此pending shader/font retry不丢。
- 跳过路径必须在`Setting.cpp:4072-4073`的OMSetRenderTargets/Clear之前。离屏夹具也要在Clear之前判断，否则“跳过绘制”会把已保留画面清空。
- 同样跳过gpu page-cache Replay copy；底层DWM仍保持最后呈现帧，不依赖discard交换链backbuffer保留。下一次变化帧恢复完整clear/copy/draw/Present。
- 如果整个共享UI长期无其他提交，device removal可能直到下一次backend才被观察；保守跳过帧可检查GetDeviceRemovedReason或明确由现有共享epoch恢复通知覆盖，不把旧成功快照跨设备复用。
- 完全准备的Hold可以跳过GPU，但HomeMotionState.elapsed仍由Begin推进，到了Dock边界下一个生成输出改变必提交；准备/restore fullFrame不允许靠“重复呈现成功”增加真实backend/Present暖帧计数。
- CompleteHomeEntryFrame当前失败布尔会回滚时间；duplicate路径不能传submitted=false。建议duplicate稳定帧不调用提交完成计数，或者明确用“复用上一成功输出”接口而不计准备成功；必须证明该路径不会推进capture/restore/warm计数。
- 仍返回Continue，保持settingNavigationClockIdle=false。当前`Setting.cpp:4344`把非Continue重基准；若重复输出误返回Idle会改变导航/entry后续dt语义。
- Debug profiler需增加actual-submit/skipped计数；不调用Present的帧不应填真实Present gap，也不应把省略backend写成已完成GPU的高FPS。报告CPU callback rate与实际内容提交次数分开。

### 7. 有意义的最小回归

1. 真正生产Frame+字体预热后静态页：第一次必须提交；若干相同CPU帧判duplicate且保留RGBA不变；一次颜色/clip/idx/vtx/command TextureID变化马上force提交。
2. Hold重复GPU跳过仍按原600ms推进，再到Dock/Settle/终态恢复必须改变并提交；Prepare/capture/restore永远走真实提交，不虚增warm成功。
3. 无新增输入事件的tooltip延迟和文本caret跨至少1.2s：相同帧间跳过，出现/隐藏的确切CPU帧force提交；hover/pulse/parallax与导航tween继续推进，最终图像对照原路径。
4. pending WantCreate/WantUpdates/WantDestroy、callback、font/epoch/resize/theme/backdrop/Hide->Show都invalidate/force。后台更新文字在同窗口几何时也提交，不要求用户鼠标活动。
5. backend/Present失败后相同下一帧仍需真实重试；snapshot只认上次成功S_OK。
6. 计时分别CPU生成+比较、actual backend、completed event等待、total；静态/hold跳过帧仅CPU，不标成GPU极速完成。记录实际提交次数、snapshot有效字节/热分配，WARP与hardware对照保留。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：共享唯一串行线程、Continue/Idle/Retry及60FPS；设置常驻session/字体事务、输入屏障、Show/Hide与捕获准备合同。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：完整Debug|ARM64 Solution和无窗口验证；本代理不构建。
- 本任务`research/cache-and-benchmark.md`：GPU copy精确透明缓存、event完成基准、不能把离屏吞吐当屏幕FPS。

## External References / Versions

本题结论以仓库实际源码为依据，没有新增API、库或需要外部能力选择。vendored Dear ImGui版本1.92.7（`imgui.h:32-33`）；ImFluent兼容门禁同版本见`imfluent/UPSTREAM.md:6`。不要根据上游某新版本可能有pending接口推导本仓支持。

## Caveats / Not Found

- 本研究未运行GUI、构建、基准或Git操作；只写本任务research文件。实现代理/主会话同时在改代码，行号可能随后位移。
- 无法从当前代码证明全事件Idle安全；输入唤醒、timer/deadline与外部更新都存在缺口。若用户后续要求CPU也休眠，应另行审计接线与定时deadline，不以本轮output dedup成功当作已完成。
- DrawData精确snapshot是推荐最小设计，尚无本輪已实测收益；CPU比较/内存复制成本需主会话实测。即使相同图像，过于保守的细微float或callback变化会继续提交，是可接受回退。
- 全相同输出跳过仍不能自动处理外部纹理在同SRV上无revision变化的GPU写入。本设置现有资源是常驻图片/显式动态图集；未来外部动态texture需明确content generation或持续提交。
