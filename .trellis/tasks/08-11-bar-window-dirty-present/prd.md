# Bar 非全屏 HWND 与脏区呈现优化

> 状态：已批准实施。首阶段实现动态非全屏 HWND，并保留现有 GDI-compatible D2D target、GetDC(COPY) 与 ULW 呈现路线。

## Goal

将 UI3 Bar 从“覆盖主显示器的全屏分层窗口和全屏 D2D 目标”迁移为“只覆盖 Bar 当前可见范围的 HWND”，并建立与动态窗口大小相匹配的脏区绘制和呈现策略，减少 4K 场景中的 D2D 到 GDI 同步、ULW 提交以及 DWM 合成负担。

启动、DPI 或影响布局的配置变化时，按 Bar 锚点计算一次所有合法状态的最大相对容量并据此创建持久渲染资源；正常动画帧只更新内容与脏区，不因可见内容范围变化而重建 D2D 资源。HWND 尺寸按动画批次的完整扫掠包络变化，而不是跟随每个动画帧逐像素变化。

## Confirmed Facts

- 当前 HWND 在 `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:114` 至 `:123` 被设置为主显示器宽度和高度减一，属于接近全屏的窗口。
- 当前 D2D 目标在 `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:52` 至 `:84` 创建，尺寸来自窗口，且使用 `D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE`。
- 当前呈现路径在 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:9525` 至 `:9563` 执行 `GetDC(D2D1_DC_INITIALIZE_MODE_COPY)`、`UpdateLayeredWindowIndirect` 和 `ReleaseDC`。`GetDC` 没有脏区参数，因此当前主要风险是整个 D2D 目标的同步成本。
- 当前 ULW 参数已包含 `pptDst`、`psize`、`pptSrc` 和 `prcDirty`，初始化见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:9684` 至 `:9694`，每次提交在 `:9542` 至 `:9560` 覆盖目标位置、源 DC 和脏区，可以作为同一次更新位置、大小和内容的基础。
- 当前渲染循环保留了 `visibleContentBounds` 收集入口，见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:6490` 至 `:7074`；同时已有按元素跟踪和提交成功后才确认的脏区事务。
- `visibleContentBounds` 目前由 `collectVisibleContentBoundsForFutureWindowSizing = false` 停用，保留的收集块已覆盖主栏、更多面板、绘图/几何属性、颜色选择器和多个越界浮层，可作为窗口包络生产者清单的起点。
- `BarWindowPosClass::x/y/w/h` 当前同时被窗口尺寸、D2D target、显示器布局限制和拖动命中使用；动态 HWND 前必须拆开“显示器布局域”“持久资源容量”和“已提交窗口视口”，否则缩窗会反向改变布局。
- 动画值已经保存 `startV`、可选 `middleV`、`tar`、曲线和统一时间线；Back 曲线常量固定，因此可以在动画批次开始时保守计算扫掠极值，无需逐帧试算窗口尺寸。
- 当前输入路径大量使用 HWND 客户区坐标直接命中控件；动态源起点后必须统一映射到稳定布局坐标，触摸、鼠标、Raw Input 派生悬停和拖动都不能遗漏。
- 当前 `ReleaseDC(nullptr)` 按[微软合同](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1gdiinteroprendertarget-releasedc)表示整个 render target 都被 GDI 修改；本路径的 HDC 只供 ULW 读取，没有 GDI 绘制，应单独验证空更新矩形能否避免不必要的整面回写。该实验不改变 `GetDC(COPY)` 可能同步整张 target 的事实；`GetDC` 本身还会 flush render target。
- `WindowService::SetBounds` 会在线程命令中调用 `SetWindowPos`。动态模式下必须只保留一个 Bar 几何提交者，不能同时让 `SetWindowPos` 与渲染线程的 ULW resize 竞争。

## Requirements

### R1. 锚点相对的最大资源容量

- 启动、DPI 变化、显示器变化或影响布局范围的配置变化时，计算 Bar 相对锚点的最大尺寸和效果外扩；不得把主按钮在显示器上的全部可拖动位置合并为绝对包围盒，否则容量会再次退化成全屏。
- 最大包围盒必须覆盖主 Bar、更多面板、绘图属性面板、几何属性面板、提示层、阴影、模糊、光照、抗锯齿边缘以及动画可能产生的外扩范围。
- 最大容量是持久 backing surface 的尺寸；Bar 锚点或容量原点可以移动，但不因此重建同尺寸资源。
- 最大容量未被突破时，不得因动画或普通状态切换重建 D2D target、GDI 互操作对象或设备相关缓存。
- DPI、组件集合或效果上限导致容量真正改变时，才创建新 target；新资源完整可用后再替换旧资源。

### R2. 动画批次包络驱动的非全屏 HWND

- HWND 只覆盖当前 Bar 可见内容所需的范围及预留 padding，不再长期覆盖整个显示器。
- 使用同一次 `UpdateLayeredWindowIndirect` 调用更新 `pptDst`、`psize`、`pptSrc` 和内容。
- 动画批次开始时一次计算 `当前已呈现范围 ∪ 本批次扫掠范围`：覆盖起点、中间关键帧、目标值、Back 曲线超调、效果 padding 和已知交互域。
- 扩张在首个需要该范围的内容帧中随 ULW 一次完成；批次进行期间保持已保留窗口范围，不逐帧 resize。
- 收起期间保留旧范围；所有关联时间线结束、输入捕获释放且最终帧提交成功后，只缩小一次。
- 动画中途重定向时复用当前保留范围；仅当新批次扫掠范围突破它时再扩张一次，最终静止后再缩小一次。
- Slider/颜色拖动等目标不可预知的连续交互使用控件预先定义的完整交互域；主 Bar 拖动允许移动 HWND，但不因屏幕位置变化重建或逐帧改变资源容量。
- 窗口大小、源起点或坐标映射改变时，默认将新的当前窗口范围视为全脏；后续只有在 Win7 SP1 实测证明安全后才进一步缩小该帧脏区。

### R3. 稳定内容坐标、输入映射和脏区

- 明确区分显示器布局坐标、容量 surface 坐标、当前 HWND 客户区坐标和屏幕坐标。
- 内容布局始终留在稳定的显示器布局坐标；绘制时通过 `surface = layout - capacityOrigin` 映射到持久 target，不因 HWND 左上角变化而改写控件坐标。
- 提交满足 `pptSrc = viewport.left/top`、`pptDst = monitorOrigin + capacityOrigin + viewport.left/top`。因此同一个布局点最终屏幕位置与 viewport 变化无关，窗口缩放不会让页面内容跳变。
- 输入执行逆映射 `layout = client + viewport.left/top + capacityOrigin`；所有消息来源必须共用一个转换入口。连续捕获/拖动期间锁住对应批次的 viewport 映射，或直接使用屏幕 delta，避免原点变化制造假位移。
- 现有元素脏区继续在稳定布局坐标中累计，提交前依次映射到容量 surface 和 HWND 客户区，最后裁剪为 `prcDirty`。
- 普通帧同时用该脏区限制 D2D 绘制和 ULW 提交。
- 首帧、资源重建、窗口范围变化、无法分类的内容变化或上次提交失败重试时，使用保守的全脏策略。
- 只有 D2D、GDI 互操作、ULW 和帧结束全部成功后才能提交脏区状态；失败时保留全脏重试语义。
- 动态窗口位置/尺寸只由渲染提交事务负责；WindowService 继续负责创建、显隐、样式和 Z 序，但动态模式激活后不再另行 `SetBounds` 覆盖 Bar。

### R4. 分阶段 D2D 到 GDI 策略

- 第一阶段保留 GDI-compatible D2D target 和 `GetDC(COPY)`，但将目标从 4K 全屏缩小为 Bar 最大容量；先测量缩小后的同步成本。
- 在引入 staging 前先验证 `ReleaseDC` 的未修改区域参数：HDC 只读时尝试空更新矩形，并在 Win7/Win11 上确认返回值、画面和下一帧 D2D 内容；若不可靠则保留现状。
- 如果缩小目标后 `GetDC(COPY)` 仍是明确瓶颈，再研究持久 CPU staging 和持久 DIB Section/HDC：D2D 只复制脏区到 staging，CPU 只复制脏行到 DIB，ULW 使用同一脏区。
- staging、DIB 和 HDC 必须复用，不得每帧创建；staging 容量可采用分档或只增不减策略。
- 不将“普通 D2D API 可以把一个脏区直接复制到 DIB/HDC”作为前提；现有接口没有这样的直接路径，单次复制方案只能作为独立研究项。
- 不在缺少性能数据时预先引入两次脏区复制的复杂实现。

### R5. 兼容性和性能验证

- 保持 Windows 7 SP1 + KB2670838 的 D2D 1.1 兼容边界。
- 验证 ULW 在同次更新窗口位置、大小、源起点和内容时的行为，特别覆盖扩张、收缩、移动和提交失败重试。
- 分别测量 D2D 绘制、`GetDC(COPY)`、可选 staging/Map/memcpy、ULW 和整帧耗时。
- 记录不同 DPI、分辨率、Bar 状态和动画负载下的脏区面积、窗口面积、CPU/GPU 占用和帧时间。
- 通过 PresentMon 或等价工具验证缩小顶层 layered HWND 后，对下方 flip-model 窗口的合成模式是否有实际改善；不把改善 Independent Flip/MPO 视为保证结果。
- 本任务只交付 Bar 的合成前置条件和测量数据；draw3 flip-model 的创建、swap chain、Present 与绘制合成由后续相关任务负责。

### R6. 脏区调试覆盖层

- 仅当 `Experimental.Inkeys3.UI3.Debug.Enable` 开启时绘制调试边框；关闭时不得留下覆盖层像素。
- 活动帧继续用红框表示业务脏区；进入 idle 前唯一的最后提交帧改用绿框表示该帧脏区，不再在帧率文字末尾显示“休眠”。
- 同一调试模式下用蓝框标出本次实际提交后的 HWND 客户区边界，边框必须位于窗口内部，不能因裁剪丢失。
- 红/绿脏区框、蓝色窗口框及其上一帧边界都必须参与调试覆盖层自己的 damage 清理；完整呈现事务成功后才推进其快照。

## Acceptance Criteria

- [ ] 启动或 DPI/布局配置变化后可以得到覆盖所有 Bar 合法状态及效果外扩的最大容量矩形。
- [ ] 正常动画和状态切换不会重建 D2D target、GDI 互操作对象或设备相关缓存。
- [ ] HWND 不再是显示器全屏尺寸，并能在一次 ULW 中同步更新位置、大小和内容。
- [ ] 一次普通展开/收起批次不会逐帧 resize：展开前最多扩张一次，动画结束后最多收缩一次；重定向只在突破已保留包络时扩张。
- [ ] Back 回弹、中间关键帧、Slider/颜色拖动域、弹出层和光影外扩均不会突破预估包络。
- [ ] 改变 `pptDst/psize/pptSrc` 时，同一布局点的屏幕位置保持不变，鼠标、触摸、Raw Input 悬停和连续拖动命中不跳变。
- [ ] 当前窗口范围不变时，D2D 绘制和 ULW 均使用映射正确且裁剪后的脏区。
- [ ] 当前窗口范围或源映射变化时使用安全的全窗口脏区，扩大区域不会出现透明残影或未初始化像素。
- [ ] 提交失败后不会错误丢弃脏区，后续能够全脏恢复。
- [ ] Windows 7 SP1 + KB2670838 兼容性验证通过。
- [ ] 已取得缩小 HWND 前后的阶段化性能数据，并据此决定是否实施持久 staging/DIB 路线。
- [ ] 如实施 staging，资源不会每帧创建，复制量与脏区面积近似相关，且最终画面与现有路径一致。
- [ ] 已记录 layered HWND 覆盖范围变化对下方 flip-model 呈现模式的实测结果。
- [ ] 已验证 `ReleaseDC` 空更新矩形实验；无论保留或放弃，都有 Win7/Win11 的正确性和耗时依据。
- [ ] 脏区调试开启时，活动帧显示红色脏区框，idle 前最后一帧显示绿色脏区框，并以蓝框准确显示实际 HWND 边界；帧率文字不再出现“休眠”。
- [ ] 关闭脏区调试后，红/绿/蓝框及旧调试文字均能被一次性清除，静止状态不会因此持续刷新。

## Out of Scope

- 本阶段不实现可选 staging/DIB 路线；只有动态非全屏 HWND 的测量结果仍证明 `GetDC(COPY)` 是主要瓶颈时才另行进入该阶段。
- 不将主 D2D 画布整体替换为 IWICBitmap 或通用 CPU 光栅画布。
- 不按每帧精确可见内容包围盒重建渲染资源。
- 不让 HWND 边界跟随每个动画采样值逐像素变化。
- 不承诺仅靠缩小 HWND 一定进入 Independent Flip 或 MPO。
- 不在没有基准数据时重写现有 D2D 1.1 效果和缓存架构。
- 不在本任务修改 draw3 或接入 flip-model swap chain。

## Deferred Research

- 将停用的 `visibleContentBounds` 清单收敛为少量顶层 Surface 与已知越界视觉的包络生产者，并补齐调试文字、光影及后续新增控件的登记门禁。
- 确定包络估算采用标量区间传播还是顶层组件专用 `PredictEnvelope`；默认推荐后者，以较小维护面换取可审计的保守范围。
- Win7 SP1 上 `prcDirty` 与同次 `psize`/`pptSrc` 变化组合的实际行为；默认方案不依赖局部更新正确性，尺寸变化帧直接全脏。
- 最大容量 target 缩小后 `GetDC(COPY)` 是否仍值得替换，以及 staging 的合适容量分档。
- `ReleaseDC` 传入空矩形在目标系统上的实际行为和收益。
- PresentMon 在目标系统上可用的指标和 flip-model 对照测试方法。
