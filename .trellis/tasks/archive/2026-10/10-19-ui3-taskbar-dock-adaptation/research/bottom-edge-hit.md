# Research: 主栏最底 1px 输入穿透保障

- Query: 当底部任务栏自动隐藏时，5 屏幕 DIP 边框间隙是否足以让物理屏幕最底 1px 不被 Bar 光影、透明包络或动画截获；确定可规划的最小保障及其他 overlay 边界。
- Scope: mixed（产品源码/规范 + 微软 Win32 官方接口合同）
- Date: 2026-10-19

## Findings

### 1. 直接结论

1. **5 屏幕 DIP 的主体边框间隙不能作为最底 1px 永远穿透的证明。** Bar 光影随 UI 总倍率缩放，任务栏厚度基准后的 UI zoom 可以独立于屏幕 DPI；实际 Gaussian 支持范围、像素舍入、描边和底栏弹性映射均需考虑。透明 HWND padding 本身不是问题，有非零 alpha 的柔光像素才是。
2. Bar 本身为 `WS_EX_LAYERED` 的逐像素 alpha ULW 窗口；没有基于真实主体的 `WM_NCHITTEST` 排除。其 D2D target 直接通过 GDI interop GetDC 交给 ULW，**不是可随手扫描/修改的 CPU DIB**。不要为了这一条边新增整图 CPU readback。
3. 推荐在桌面、当前屏底部任务栏且 auto-hide 已确认的条件下，为 Bar 的最终窗口 viewport 设一条**物理像素上限**：成功呈现的屏幕 `RECT.bottom <= monitor.bounds.bottom - 1`。这让最底一行物理像素从 HWND 范围内消失，独立于 alpha、光影、debug overlay、倍率和动画。该规则必须覆盖底栏/居中底栏切换、弹性帧、direct `SetWindowPos` 直移与旧 HWND 到新裁剪窗口的过渡；不能只改初始 target 或名义主栏几何。
4. `HTTRANSPARENT` 的官方合同只保证继续查找**同线程**下层窗口；它不能单独证明跨进程 Explorer 任务栏收到输入。`WS_EX_TRANSPARENT` 对 layered window 可以全窗穿透，但会让正常主栏按钮失去交互，不适合局部 1px 保障。
5. 本任务应保证“主栏自己的像素/窗口不挡住最底 1px”。已有全屏 Draw3 主输入画布另有明确的捕获语义，不能将这项修复升级为所有模式、所有窗口下 Shell 必升起的承诺或顺手改造 Draw3 输入。

### 2. Files found

| 文件 | 描述 |
| --- | --- |
| `Inkeys/IdtMain.cpp` | 所有 overlay 创建样式、主画布与 presentation-only 窗口区分、Magnifier 尺寸 |
| `Inkeys/Inkeys/Window/Window.cpp` | HWND 创建、默认 layered 样式、点击穿透命令、owner/Z 序 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | Bar WndProc、直拖的 SetWindowPos 优化与呈现锁 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderingAttribute.cppm` | 描边/光影 conservative dirty outset |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | Gaussian mask 外扩、描边与光源实际绘制 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 本帧 viewport/source/size、dirty clear、ULW 成功事务 |
| `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h` | viewport 动画包络、capacity、坐标转换与提交工具 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | 底栏弹性幅度和 spring clamp |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | 其他浮层圆角主体 hit test 的现状 |
| `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp` | Preview 独立 layered HWND，HTCLIENT 与真实 ULW 透明命中 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | 主画布 ULW 的 1/255 alpha 命中底层，仅在 CPU 输出副本加入 |

### 3. Code patterns / evidence

- `IdtMain.cpp:3144` 的公共 overlay exStyle 为 `WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`；`IdtMain.cpp:3190` 起沿用这些样式创建 Bar。`Window.cpp:1153` 同样提供 layered/noactivate/toolwindow 默认。Bar 无 `WS_EX_TRANSPARENT`，不能把视觉半透明等同输入穿透。
- `Bar.Interaction.cpp:546` 起 Bar WndProc 处理 `WM_MOUSEACTIVATE`、Raw Input、触摸/鼠标等；全文未找到 `WM_NCHITTEST`。未专门处理的消息最终 `DefWindowProcW`（例如 `Bar.Interaction.cpp:1046`）。Bar 的 Shape/Button 业务命中在输入已经进窗口后，不能替代 USER32 对 Explorer 的命中分发。
- `Bar.RenderingAttribute.cppm:13-14` 定义固定 `dirtyAntialiasPadding=3` 像素、`pointLightDiffuseExtraWidth=6.0`；`:33-41` 使用 `ceil((ft + 6) * tarZoom)` 后再追加 3px。**这是保守 damage 支持，不等于实际所有范围都非零 alpha**。
- 实际圆角 diffuse mask 在 `Bar.Rendering.cpp:1514` 起：`:1520-1522` 的 Gaussian sigma 为 `6/6 * frameZoom`；`:1605-1606` 的缓存 padding 为 `ceil(3*sigma + strokeWidth/2 + 1)`，sigma 与 stroke 还经过四分之一像素量化。几何 mask 在 `:2126-2128` 与 `:2209-2210` 使用相同规则。
- `Bar.Rendering.cpp:2351-2358` 的光源边界也随 `frameZoom` 外扩；`:2391-2404` 先画基础边框，再叠加 Gaussian 柔光；`:2491` 起 Shape 绘制继续使用 frameZoom，`:2550-2554` 的实际 stroke width 随 UI zoom，而非只有屏幕 DPI。
- 因此例如 DPI=1、总 UI zoom=2 时，Gaussian 的有限支持 padding 约为 `6px + 半描边 + 1px`，已经可超过 5 屏幕 DIP=5px。不能由默认倍率下肉眼光影约 3px 推广到全部倍率；低 alpha 尾部到底是否量化成非零，需要真实像素测试。
- `Bar.BottomDock.h:19` 的 `BarBottomDockVisualLimitDip=24.0`；`Bar.RenderLoop.cpp:9508-9525` 为底栏弹性 current/预测包络预留该幅度及额外恢复范围。该包络是容量保证，不会自动将动画后的绘制裁到主栏5DIP间隙之上。
- `Bar.RenderLoop.cpp:9505-9507` 的 layoutBounds 当前为整张显示器布局；`:10049-10060` 解析最终 candidateViewport 和 candidateSource；`:10064` 起候选映射 tuple 保存 source/size/capacity/epoch。`Bar.WindowGeometry.h:398-439` 的 viewport controller 在动画期间只扩、idle 才收缩，并与 layoutBounds 求交。因此仅缩名义内容或只在 idle 裁剪不够。
- `Bar.RenderLoop.cpp:10252-10260` 在本帧 dirty clip 下 Clear 到 `(0,0,0,0)`。`:12930` 还可能绘制 HWND debug 边框；保护必须在所有业务/装饰绘制之外成立，不能只关闭光源。
- 最终 ULW `pptDst = monitorOrigin + candidateViewport + directTranslation` 在 `Bar.RenderLoop.cpp:13004` 附近形成；`:13028-13032` 调用 `UpdateLayeredWindowIndirect`，`:13116` 起仅完整成功后发布 viewport 和几何快照。
- `Bar.Interaction.cpp:7177-7205` 在 try_lock 成功、已有 committed RECT 时用 `SetWindowPos(...,SWP_NOSIZE)` 直接平移旧位图。**只在 RenderLoop 裁剪仍可被该直移路径绕过**；直移若需要不同裁剪尺寸，应请求现有 ULW fallback，不能等待慢呈现锁，也不要错误确认未实际直移的 committed translation。
- `Bar.Initialization.cpp:255-260` 首帧前仅提交 1x1 防闪 HWND；之后以首帧 ULW 计算真实范围，不应在初始化阶段另建全屏裁剪窗。

### 4. 最小保障方案比较（仅规划，无实现）

| 路线 | 正确性和成本 | 建议 |
| --- | --- | --- |
| 只用5DIP主栏位置/关掉光影 | 不能覆盖高 UI zoom、描边、Back/spring、debug 或子面板 | 不作为硬保证 |
| 对底行返回 HTTRANSPARENT | 便宜，但官方只保证同线程下层，Explorer 跨进程；已有 alpha 命中结果也不同 | 不作为唯一保证 |
| 整窗 WS_EX_TRANSPARENT | layered 场景可让输入穿过整窗 | 会破坏按钮交互，不采用 |
| 每帧 SetWindowRgn 排除底行 | 从窗口形状排除可行，但引入 HRGN 生命周期、owner 线程更新、直移同步和每帧 USER32 形状变更 | 无须为这一行新增 |
| 最后清底行 RGBA=0 | 可行，须在实际 screen→source 映射后、所有绘制后强制透明；dirty 必须含该行，旧/新行、映射变化需整窗替换；直移又会改变应清行 | Bar 不是 CPU DIB，优先避免 readback；D2D COPY/Clear 路线也有 extra dirty/转换与拖动一致性负担 |
| **最终 viewport 不覆盖 bottom-1 行** | 纯整数几何；现有 ULW source/size/crop 即可表达；影响通常只限最外一行光影；不依赖alpha命中细节 | **推荐**，需整合现有事务及直移保护 |

建议保护合同：在桌面且当前屏 bottom+auto-hide 已确认时，凡 Bar 的成功窗口几何、快速直移目标与动画帧，都不覆盖 `[monitor.bottom-1, monitor.bottom)` 物理行。可在形成 candidateSource/size/映射 tuple **之前**集中裁剪 candidateViewport，考虑同一帧 `directTranslation`，并让最终 ULW 持锁事务复核该映射未过期。对应局部布局上限为 `monitor.bottom - 1 - monitorOrigin.y - directTranslation.y`。不得仅在调用 ULW 时临时减小 psize，否则 viewport/source、client/layout命中、committedWindowScreenBounds 和 tuple 会分裂。保护状态/边界改变应请求 Bar，并依现有规则选择一次整窗替换以移除旧窗口底行；稳定后继续局部 dirty，不逐帧强制全脏。

直移目标若已有 RECT 在保护行之上，可继续原快速路径；目标会跨入保护行或改变裁剪需求时，保留 latest pending translation 并请求 ULW fallback。禁止把整栏硬上移来抵消光影，因为用户要求的5DIP按主栏外描边测量，透明窗口包络不参与间隙。具体恢复动画仍由dock几何调查统一设计；新任务barrier只裁最后一物理行。

### 5. 其他 overlay 的实际边界

- `IdtMain.cpp:3147-3148,3193-3194` 的公共 overlay 大小为主屏完整 width/height；主 `Drawpad` 在 `:3204-3209` 清除 `WS_EX_LAYERED | WS_EX_TRANSPARENT` 后按 presenter 能力选样式，没有全局少1px的几何合同。
- `Draw3.TransparentPresentation.cpp:931-933` 分别设 `primaryUlwPresenter.applyPrimaryHitUnderlay=true` 与 selection=false；`:186-189,215-218` 在 CPU 输出副本将原0 alpha 合成为1，确保主画布可命中。DComp主画布也不应套用layered逐像素命中假设。**真实绘制态全屏画布可能另行影响任务栏触发，静态证据不能宣称该模式下Explorer必升起。** 不在本任务顺手清除画布输入、裁掉笔画或重写RTS。
- `DrawpadPresentation` 在 `IdtMain.cpp:3197-3202` 出生固定 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，只呈现不输入；Freeze 创建传入 `WS_EX_TRANSPARENT`（`:3235-3236`）。
- MagnifierHost/Child 是已有 `SM_CYSCREEN-1` 尺寸（`IdtMain.cpp:3162,3178`），这也不证明其他 overlay 自动继承同样边界。
- PageControl 的 `WM_NCHITTEST` 在 `PageControl.cpp:2622-2637` 使用当前 Scene 主体/按钮 hit test 返回 HTCLIENT/HTTRANSPARENT。它已有PPT/白板几何与输入合同，本次不改PPT进入/退出/分页。
- StartupPreview 是单独 layered HWND（`StartupPreview.cpp:218`），`:116-117` 返回 HTCLIENT，而透明圆角外依ULW原生alpha命中；本任务如更新其桌面初始位置，应沿统一几何策略检查实际bounds，不凭正式Bar的裁剪宣称Preview也受保护。

### 6. 测试建议

无窗口自动验证应调用生产纯几何/呈现策略，不复制公式：

- DPI 96/120/144/192/240；UI倍率最小、默认和最大；任务栏厚度基准开关两档；主屏、副屏及负坐标/上下排列屏幕。
- 浮动→底栏→居中底栏、折叠/展开、隐藏→升起→隐藏、反向中途retarget、动画关/开、Back峰值与24DIP spring极值；所有受保护候选和成功RECT.bottom均 <= monitor.bottom-1。
- 裁剪边界/开关改变、source/size变更、旧映射tuple、部分dirty、viewport动画只扩不缩、device epoch重建和ULW失败重试：映射变化选整窗替换，失败不推进成功几何，恢复后稳定局部dirty。
- 直移完全安全时仍允许SetWindowPos优化；跨保护行时走fallback、pending不丢；再次上移或松手正确吸收，client/layout/surface往返与光源区域一致。
- 保护条件不成立时保持原几何，PPT/Whiteboard未套桌面规则；5DIP按真实DPI和主栏外描边计算，独立于HWND padding和UI倍率。
- 若实施选择alpha清行而非viewport，需要真实D2D离屏RGBA读回断言最底物理行**所有4通道均0**，覆盖所有描边/光源/debug绘制；不能仅测试名义几何。

实施后按仓库规则执行完整 `InkeysRepo.sln Debug|ARM64` 和 `InkeysHeadlessTests --no-window`。真实任务栏自动升起、跨进程命中、capture期间拖动、Win7/不同Explorer/SuperTop/混合DPI需用户授权GUI后人工验收；本次只调查，没有运行GUI或编译。

### 7. External references（2026-10-19读取，Win32官方文档）

- [Window Features / Layered Windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)：逐像素alpha=0或color-key区域让鼠标穿过；layered窗口加WS_EX_TRANSPARENT则忽略形状并让整窗鼠标穿过。以上为微软合同，低但非零alpha不等于透明穿透。
- [WM_NCHITTEST](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)：HTTRANSPARENT仅描述同线程下层继续查询；多显示器坐标使用GET_X/Y_LPARAM以保留负坐标。
- [Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)：WS_EX_TRANSPARENT一般描述绘制顺序；不能脱离layered窗口专门合同将它当作任意窗口局部输入穿透能力。

### 8. Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md` 的“UI3动态HWND与dirty”合同（约592-773）：source/size/epoch变化全窗替换、dirty/成功tuple、直移呈现锁；绘图与装饰必须消费同一candidate几何。
- 同规范约1690-1870的window owner/Z序与主画布surface选择合同：禁止以Bar局部需求修改Drawpad样式、owner或独立置顶。
- `.trellis/spec/native-desktop/draw3-integration.md:102-110`：主Drawpad与presentation-only的固定输入/样式边界。
- `.trellis/spec/native-desktop/startup-preview.md:95-104,242`：Preview透明命中、独立owner和实际可见轮廓间隙。
- `.trellis/spec/native-desktop/build-and-compatibility.md` 与根AGENTS：完整solution+ARM64、无GUI默认规则；兼容配置存在不是实测通过。

## Caveats / Not Found

- 没有执行真实Explorer、WindowFromPoint或D2D/ULW像素测试，因此只确认源码及API合同，不能证明默认机器已存在输入阻塞，也不能证明新几何对Shell所有模式必然有效。
- Gaussian padding是保守支持矩形，dirty6×zoom+3也不是非零像素下界。默认倍率下可能恰好安全，极端倍率下不能无测试宣称安全。
- 本文给出viewport裁剪的可行方向和必须接线点，未实现。最终应由实施者将clip、owner direct-move和成功tuple放在同一真实事务中审查，避免仅修改一处psize的表面修复。
- 保护状态采样、底部任务栏显隐判定及退化策略由主会话调查；PPT/Bar dock几何、5DIP描边含义与动画由另一研究主题负责。

## 补充研究：若用户批准扩大到 Draw3 主画布最底 1px

此部分是待确认范围的候选方案，**没有实施授权、没有改动产品、没有实测Shell阻断**。保留前文仅保障Bar的范围A；范围B在用户明确要求后才采用。

### A. 画布直接减1px高度的影响

- Window Service 已有同步主/辅助画布几何入口：`Window.cpp:2250-2267` 的 `ApplyDrawpadBounds` 以一次DeferWindowPos同时设置 `Drawpad` 与 `DrawpadPresentation`；`SetBounds(Drawpad)` 在 `:1772-1780` 唯一转入该函数。不能只缩一个角色。
- 真正改 HWND client height 会在 `Draw3.WindowControl.cpp:1770-1783` 发布 pending resize + control wake。`Draw3.DrawingController.cpp:8227-8263` 依次 resize renderer、resize presenter，两者成功才 CommitSize 并通知 observer；失败走graphics恢复。`Draw3.TransparentPresentation.cpp:1234-1256` 连主/selection两个ULW目标都按新尺寸重建。对一行任务栏热区频繁走此链比单纯窗口region昂贵得多。
- `Draw3.Host.cpp:1319-1334` 的client rect、ClientToScreen、显示器像素尺寸参与输入标尺上下文；直接改高度会改变client覆盖边界。虽然缩1px不会自动改modeler算法，仍须验证RTS坐标、文档/raster交集保存、Resize活动层重建及PresentationTarget映射。
- 不能用Presenter里的psize单独缩短主ULW窗口：后续Window Service SetBounds、主/selection切换及DComp模式会使真实HWND、window_.Size、backbuffer与映射分裂；不能只改1/255命中底层（对DComp无效）。
- 若坚持缩HWND，应按**策略变化时一次**设置裁剪高度并在不适用时恢复完整几何，不随任务栏升起/降下重复resize。进入PPT/Whiteboard前恢复既有窗口合同，退出桌面后再按最新策略处理；这些跨工作区事务超出了纯Bar位置改动。

### B. 更小候选：owner-thread SetWindowRgn 排除物理底行

对基本静止、client尺寸必须保留的全屏画布，**region比改client height更小**，与主栏每帧动态几何不采用region的取舍不同：

1. 在Window Service owner线程针对实际 `Drawpad` HWND构建window-local region，整张窗口矩形减去 `screenY=monitor.bottom-1` 上与该显示器相交的1px水平段；简单主屏完整画布通常等价于 `CreateRectRgn(0,0,width,height-1)`。保留物理像素，不乘DPI。跨屏窗口/窗口不贴该屏底部时，必须用实际屏幕→window坐标，不能无条件从窗口height减1。
2. region只限制系统窗口可见/输入形状，不改变窗口outer/client RECT与client原点，因此理论上保留RTS/Surface/Presenter/文档viewport坐标合同；全屏backbuffer仍可绘制/存储最后一行，该行仅在当前桌面窗口中不可见且不可新命中，不对持久化笔画做删改。
3. 样式、owner、Z序、窗口大小、primary/presentation切换保持现有所有权；不碰Modeler/RTS注册、不发清屏、不重启Host。`DrawpadPresentation` 与Freeze本已WS_EX_TRANSPARENT，输入上无需为它们排除；若本任务也要求所有可见像素都让出底行，可对它们应用同一region但不要修改其固定穿透样式。
4. 更新只发生在底部auto-hide策略变化、workspace切换、真实bounds/显示器变化、HWND代际创建/重建或恢复，值相同则完全no-op。任务栏本次升/降动画不改变要让出的物理底行，因此不用每帧调用SetWindowRgn。
5. region成功后HRGN所有权归系统，不能DeleteObject；失败时才由调用方DeleteObject并保存原策略待重试。恢复时SetWindowRgn(hwnd,nullptr,TRUE)。策略的成功revision以真实调用成功为准；新代HWND不能继承旧代已应用标志。若未来窗口已有非矩形region，必须与原region求交、恢复原值，不可盲目覆盖。
6. SetWindowRgn会发送WM_WINDOWPOSCHANGING/CHANGED；当前Draw3.WindowControl.cpp:1762-1767在GPU模式下可能请求一次full present。并不改变client尺寸；即使默认过程产生重复相同WM_SIZE，`DrawingController.cpp:8232-8234`同尺寸提前返回，不能靠假设省略检查。应验证无resizeCount增长、无Host重启、无连续present或policy反馈循环。
7. 该region必须在Window Service集中处理，不能由渲染线程直接改HWND。PPT/Whiteboard不适用时恢复，避免影响PresentationTarget、白板fullscreen标记和现有区域。恢复失败不能报告已恢复；保留诊断与有界重试。
8. 已有鼠标/RTS capture可继续收到区域外消息；region只承诺未捕获状态下新输入不被该区域命中，不能声称主动拖笔/鼠标按住时任务栏一定升起。不要为Shell热区擅自取消当前stroke/capture。

### C. 官方合同及兼容证据等级

- [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)：window region裁掉窗口不可见部分，坐标相对window左上角，函数发送window-position消息，成功接管HRGN；接口最低Windows2000。该文档支持shape合同，不等于本项目各presenter已实测。
- [DirectComposition Basic Concepts / Composition target window](https://learn.microsoft.com/en-us/windows/win32/directcomp/basic-concepts#composition-target-window)：窗口各composition层都裁到target窗口visible region。这支持region在DComp下的可行性推断；应真实验证DComp与ULW，而不是因alpha=0的layered合同套用到DComp。
- [CreateRectRgn](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createrectrgn)：创建矩形region，生命周期与SetWindowRgn接管规则一起使用；不引入新版本API静态导入。
- 当前源码未找到已有SetWindowRgn/GetWindowRgn实现；新增集中policy需单独列Window Service owner-thread、dedup和失败恢复验收。Windows7 ULW、Win11 DComp、SuperTop/UIAccess、混合DPI与Explorer触发都仍需真实环境证据。

### D. 范围B的增量验证要求

- 生产纯policy测试：workspace/Desktop、当前屏底部auto-hide、负坐标/多屏窗口、monitor bottom exclusive、region window-local坐标、策略恢复、HWND代际/失败重试、相同输入不重复调用。
- 若实现region，可无GUI测试HRGN纯几何PtInRegion：底行排除、倒数第二行和其他全部正常区域保留；窗口origin/client/size完全不变。不要把此测试称为跨进程真实命中验证。
- 所有改动仍做完整solution/无窗口测试。获用户授权的真实窗口验收应覆盖DComp与ULW、Drawing主画布/Selection前置表面切换、Down/Up capture、底部stroke经过/靠近热区但内部文档不丢、策略开关后重现底行、进入退出PPT/Whiteboard及Host重建。区分任务栏是否升起与region接口是否成功。
- 对比resizeCount、present计数、idle CPU：策略不变无region调用、升降只动Bar、画布最多策略改变时请求一次重呈现。若做height方案，增加RTS/resize/selection双窗口尺寸与source一致性回归；不得将简单1px改动描述为没有资源重建。
