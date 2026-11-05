# Research: UI、字体、图片与托管互操作内存结构

- Query: 当前生产 UI/字体/图片/托管互操作由谁分配和持有内存，隐藏后哪些资源仍保留，优先测量哪些优化方向？
- Scope: internal；只读源码调查，唯一写入为本报告。
- Date: 2026-11-04
- Evidence: 下文“确认”只指当前源码；没有启动程序、构建或采集真实进程内存。

## Findings

### 1. 生产路径和文件

| 文件 | 职责与证据 |
| --- | --- |
| `Inkeys/Inkeys.vcxproj:907`、`:915`、`:919`、`:988`、`:1023`、`:1034` | 共享 RenderPipeline、分页/白板、字体、Bar 和 Setting 均为 ClCompile。 |
| `Inkeys/Inkeys.vcxproj:938`、`:953`、`:961` | 编译带本地补丁的 ImGui FreeType、legacy FreezeFrame 与 Draw3 Facade。 |
| `Inkeys/Inkeys.vcxproj:951`、`:1187` | `IdtDrawpad.cpp`、`IdtFloating.cpp` 为 None，不能把其中旧整页墨迹图片缓存视为生产分配。 |
| `Inkeys/IdtMain.cpp:3576`、`:3613`、`:4022`、`:4112`、`:4140` | 启动共享 UI device、DWrite 字体 collection、Setting resident session、Freeze 线程、PPT 联动。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:312` | Bar/PageControl/Whiteboard 使用的 D2D target 与缓存实现。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm:23` | Setting 图片 SRV 与保留的 CPU 解码像素。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:1932`、`:5603` | 内嵌字体、resident session、显隐和退出。 |
| `Inkeys/IdtFreezeFrame.cpp:47` | 生产可达的独立 CPU 全屏 DIB。 |
| `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp:400` | 启动预览临时 target 与效果资源。 |
| `Inkeys/IdtPlug-in.cpp:366`、`PptCOM/PptCOM.cs:1456` | native 创建 managed COM server，并进入长期运行的服务。 |

### 2. 共享 UI device 只是一层共享，窗口 target 与缓存仍各自持有

**确认：** `RenderPipeline.cpp:1131` 创建 shared DWrite/D2D factory；`:1151` 明确创建 WARP epoch；`:1174` 在 scheduler 停止后清空 current/prepared/pending epochs 和 shared assets。WARP 是 CPU 软件栅格化后端，资源应纳入进程/系统内存观察，不能笼统称为独立 GPU 显存。

Bar、四个 PageControl surface 和 Whiteboard background 各自拥有 `BarUIRendering` 的 device context、target bitmap 和 GDI interop。`Bar.Rendering.cpp:332` 创建 context，`:336`—`:342` 创建 BGRA8 premultiplied、TARGET|GDI_COMPATIBLE bitmap；逻辑像素成本为 `width × height × 4`，实际分配另有驱动、行对齐、图形运行时和 ULW/DWM 成本。

`Bar.Rendering.cpp:350` 先创建候选资源再替换旧资源，保证失败可恢复，但重建瞬间可同时存在旧/新 target。这是合理事务峰值，应与稳态分开。`:361`—`:396` 的 discard 会释放 target/context/阴影等缓存。

**Bar 高水位：** `Bar.RenderLoop.cpp:9654` 已明确避免为从未打开的隐藏面板预约巨幅位图；`:9670`—`:9683` 仍保留历史 capacity 的宽高最大值并以此创建 target。因此“首次展开后收起”可能保留较大 backing，但不能声称冷启动已预分配全部面板。

**分页高水位：** `PageControl.cpp:283` 记录 backingCapacity，`:2538`—`:2575` 解析稳定 backing，`:2586`—`:2602` 隐藏完成后停止呈现而非释放 scene；正常资源释放集中在故障路径 `:1627`/`:1640` 和最后引用 `Release()`（`:5977`—`:6041`）。`Bar.Scene.cpp:965` 的 `ReleaseDeviceResources()` 已能释放 renderer 和 icon caches，后续若做 idle 收缩应复用现有所有权/脏区合同。

**缓存已有边界：** `Bar.Rendering.cpp:1881` 和 `:2480` 分别限制 diffuse/geometry mask parent 条目至 24；`:2176`—`:2208` 限制每 parent 精确遮罩 6 项、全 renderer 48 项/逻辑 4 MiB，单项上限见 `:2081`（512 KiB）。文字格式每帧按使用清理，见 `Bar.Format.cppm:96`—`:115`；label fit cache 达 64 清理，见 `Bar.Rendering.cpp:3407`。这些不应被报告为已证明的无界泄漏；预算也不等于已分配量。

### 3. Setting 在首次打开前已持有 resident 字体与图片

**确认：** `IdtMain.cpp:4022` 在启动阶段调用 `Setting::Initialize()`。`Setting.cpp:5603`—`:5648` 即使窗口不可见，也注册 renderer、请求一帧并等待 resident 初始化完成。`Setting.SessionState.h:347` 使用 `!hasResident` 决定初始化，与 visible 无关。

初始化 `RunSettingSession` 的前段先取得 shared epoch（`Setting.cpp:2157`），加载图片（`:2167`），创建 ImGui context/backend 和字体（`:2455`—`:2468`）。`Hide()`（`:5687`）更新 visible 并请求 renderer；`RenderSettingFrame` 的隐藏处理（`:5343`—`:5351`）只释放 presentation，保留 context、字体、SRV 和同 epoch 橡皮预览缓存。

- Presentation：`Setting.Base.cppm:85`—`:99` 的独占 swapchain 是 RGBA8、BufferCount=2；`:111`—`:124` 可释放 swapchain/RTV/device lease。隐藏时调用 CleanupPresentation，不能把这两份可见窗口 backbuffer 当作隐藏稳态常驻。
- 图片：`Setting.Base.cppm:23`—`:24` 是 14 槽 SRV 和 CPU vector。`LoadTextureFromMemory` 在 `:165` 将每张解码 BGRA 再复制至全局 `settingTexturePixels`，`:170`—`:195` 创建 immutable BGRA8 texture/SRV；CPU 副本用于设备重建（`:204`—`:215`）。原始临时 DibSurface 与转换 vector/new[] 会释放，持久成本是 CPU cache 加纹理及运行时开销。
- Shutdown：`Setting.cpp:5265`—`:5274` 才释放 backend、SRV、字体、context 和 device；`CleanupSettingTextureCache` 在 `Setting.Base.cppm:218` 只调用 vector.clear，保留 vector.capacity，不能认为其将 CPU 缓冲存储立即归还；常规 Hide 连 clear 都不会调用。

#### 可直接计算的图片像素负载（估算，并非进程实测）

`Setting.cpp:2383`—`:2400` 在 resident 初始化时解码两张固定 `2304 × 432` 橡皮演示 atlas，分别保留 CPU BGRA cache 和 WARP texture：

- 仅 CPU 两张像素：`2 × 2304 × 432 × 4 = 7,962,624 B = 7.59375 MiB`。
- 其他 CPU 图片在 `:2173`、`:2207`、`:2241`、`:2275`、`:2310`、`:2344`、`:2371`、`:2403`：四个 30² 插件标记、45² 头像、100² 二维码、64² GitHub 标记和 650×460 赞助图，尺寸随 `settingGlobalScale=s` 变化；窗口图标固定64²，见 `:685`。
- 忽略整数取整，全部这些 CPU 图片 payload 约为 `7,979,008 + 1,274,884 × s²` 字节：s=1 时 **8.8252 MiB**，s=2 时 **12.4727 MiB**。不含 vector 余量、字体和后台 D3D/WARP 分配。不是宣称能无风险节省相同 Private Bytes。
- 对应纹理的逻辑像素也存在，但应单独记资源账，不与 Private Bytes/Working Set 等重叠统计直接相加。

**已有按需机制：** 橡皮播放目标在真正显示预览时才创建；`Setting.EraserDemoGpu.cpp:305` 的 invisible 分支只暂停并保留，`:314` 将单场景宽度限制至256—768并32像素分桶；`:235` 创建双场景 `2w × floor(w×432/768)` RGBA8 target，最大1536×432，即2.53125 MiB逻辑像素。设备换代/Shutdown reset，不应误报为冷启动必有此目标。

首页入场缓存是额外 presentation 尺寸纹理（`Setting.HomeEntryCache.h:88`—`:93`）；入场完成/显隐和重建会 Reset（`:51` 及 `Setting.cpp:5339`），属于瞬时峰值调查项。

### 4. 字体映射、解析结构与动态 atlas 是三类成本

`IdtMain.cpp:3600`—`:3615` 将 SC/TC/JP/KR 共8个 Regular/Bold 字库注册到 DWrite collection。资源在 `Inkeys/Inkeys.rc:128`—`:150`，当前文件总大小 **48,993,308 B / 46.7237 MiB**（本地文件 stat）。

这只是8份字库的嵌入文件 payload。`Text.Font.cpp:40`—`:49` 的 stream 从 `LoadResource/LockResource` 读取映像资源，`Text.Font.cppm:84` 的 ReadFileFragment 返回同一资源区的片段；Setting 的 `AddSettingFontResource`（`Setting.cpp:1935`—`:1943`）也直接使用 resource 指针，`Setting.Typography.h:28`/`:38`/`:95` 设置 FontDataOwnedByAtlas=false。本地 ImGui 的 `imgui_draw.cpp:3215`—`:3226` 将此指针交给AddFont，`:3059`复制配置而非整份字库。因此不能把48.99 MB直接当成48.99 MB复制到private heap，更不能因为4个逻辑字体就乘4；映像页是否驻留应看 Image WS。

Setting 构造 Legacy/Design × Regular/Bold 四个逻辑文本字体，每个带SC/TC/JP/KR fallback，并另有图标脸（`Setting.cpp:1987`—`:2038`）。以下优化已经存在：

- `imgui_freetype.cpp:400`—`:415` 在同一 atlas 中共享同字体数据的 FT_Face，FT_Size仍按角色/字号独立；不能再次把“共享 face”作为新优化。
- `Setting.Typography.h:70`—`:85` 已是FreeType轻hinting、Alpha8动态atlas，源density=1；字图不是预烘焙全部CJK字符。
- `imgui_impl_dx11.cpp:784`—`:804` 已支持Alpha8→R8纹理，设备不支持时才转RGBA。不能直接建议从RGBA改R8并声称固定4倍节省。

剩余待测：resident初始FreeType解析结构、实际语言/各字号glyph缓存、切语言/跨DPI后的高水位、candidate atlas事务（`Setting.cpp:2060`）同时保留新旧atlas的峰值。所有不应靠删fallback/粗体/语言功能处理。

### 5. Freeze 有一个启动即触碰的整屏 CPU DIB，白板另有独立背景 target

**确认：** `IdtMain.cpp:4112` 无条件启动Freeze线程（正常启动路径）。`IdtFreezeFrame.cpp:51`—`:74` 在循环前创建主屏 `pixelWidth × (pixelHeight-1)` 的DibSurface并clear，还解码sign4；局部变量直到线程退出才析构，非首次使用时才分配。

`Surface.cpp:157`—`:161` 为32-bit CreateDIBSection，`:199` reset 删除HBITMAP/DC，`:236` clear触碰像素。仅该DIB逻辑像素成本：1920×1079×4=**7.9028 MiB**；3840×2159×4=**31.6260 MiB**。这是CPU图像存储，尚未计ULW系统副本。

白板使用共享Freeze HWND，但在 `Whiteboard.cpp:299`—`:310` 另以 `backgroundScene` 创建其D2D/WARP背景target。普通退出走 `CommitBackgroundInactive`（`:151`—`:155`）并最终Idle（`:218`），这里不释放backgroundScene；释放主要在错误（`:416`/`:423`）及Shutdown（`:591`—`:597`）。因此白板首次使用后，即使回到桌面，可能同时保留legacy Freeze DIB与白板背景target；应实测退出白板后的持有成本。共享一个HWND不代表共享同一幅像素存储。

### 6. Startup Preview 与 PPT / 线程的边界

StartupPreview 的 RenderResources 包含D2D target与效果，见 `StartupPreview.cpp:400`；自动停止 `:579`—`:588` 会Reset资源，显式Stop `:940`—`:954` 在render线程释放并停止owner。可作为启动峰值来源，当前源码不足以指控其常驻泄漏。

PptCOM在主启动阶段加载DLL（`IdtMain.cpp:3487`—`:3517`），PPT worker又创建COM-visible托管server并运行服务（`IdtPlug-in.cpp:375`、`:514`—`:536`，`:1529`启动GetPptState）。项目目标为.NET Framework v4.0/AnyCPU（`PptCOM.csproj:11`、`:20`），manifest runtime v4.0.30319（`PptCOM.manifest:7`—`:12`）。因此“未打开PPT时CLR仍可能已启动”是有代码依据的重点测量假设；实际CLR模块、JIT/GC堆大小需看运行进程，不能从DLL体积推算。

`PptImg.Image`定义仍在（`IdtPlug-in.h:26`—`:30`），但生产 `IdtPlug-in.cpp:1005`—`:1007` 只清理；搜到写入来自已为None的 `IdtDrawpad.cpp`。不可把旧整页PPT图片map当作当前主要常驻来源。

常驻线程包括Main中的TopWindow、Bar初始化/协调、Freeze、StateMonitoring、Magnifier、PPT以及RenderPipeline和Setting业务owner；参见 `IdtMain.cpp:4103`—`:4140`、`RenderPipeline.cpp:1162`、`Setting.cpp:5607`。必须按实际线程数和Stack Commit测量；默认保留栈虚拟地址不等于已提交内存，不应直接按“线程数×1MB”计RAM。

### 7. 建议测量/优化优先级

| 优先级 | 方向 | 证据与可观测验证 | 主要约束 |
| --- | --- | --- | --- |
| P1 | 将未使用的Freeze整屏DIB改为按需创建/闲置释放，或缩小纯提示所需表面 | 冷启动未用Freeze时分配已确认；按分辨率建立DIB账，比较相同场景Private/WS与GDI资源。4K逻辑像素约31.63MiB。 | 必须保留Recall/PPT失败提示、共享Freeze所有权与白板交接；本轮不实施。 |
| P1 | Setting resident改为按需准备，至少将橡皮演示/赞助等按页加载 | 初次打开前即加载的CPU图片约8.83MiB（s=1），另有纹理和字体解析；比较冷启动→首次打开→切页→关闭的阶梯。 | 将增加首次打开/页切换成本，须维持遮罩首帧和初始化失败合同；是后续产品决策。 |
| P1/P2 | 降低Setting CPU BGRA恢复副本与常驻纹理重复 | 同时持有CPU副本与immutable texture已确认；按槽统计capacity及纹理尺寸，评估使用资源ID/解码参数在device reset重建，或冷页资源卸载。 | 设备丢失恢复不能依赖失效旧device；重解码时间与峰值需量化，不能只删除cache。 |
| P2 | 退出白板后回收背景target，长期隐藏时收缩Bar/PageControl高水位 | 白板全屏target可能再留一幅4K BGRA；Bar已避免冷启动预分配，但使用后仍保留peak。记录每client target capacity与idle后的差值。 | 必须等成功隐藏/事务退休与输入屏障；重建须整幅重绘，防残影、卡顿或旧页输入准入。 |
| P2/待测 | 用现有字体基准判断未打开设置的解析成本、字号高水位和语言切换峰值 | 资源映射、FreeType结构、CPU atlas、WARP纹理分别记账；Alpha8和FT_Face共享已存在。 | 不删除语言/真实粗体/跨语言fallback；只有采样证明大头后再调整策略。 |
| P2/待测 | 测PPT功能未使用时托管运行时基线，评估延迟COM服务激活 | 冷启动模块表确认CLR，做支持PPT但尚未启动Office的场景对照，避免将Office外进程内存算入Inkeys。 | 延迟会影响PPT自动检测/首次接管，需单独规划兼容与可用性；GC/FinalRelease不是合规替代。 |

### 8. 可复用的无窗口验证能力

- `--setting-font-lifecycle-test` 入口在 `IdtMain.cpp:1879`—`:1882`，实现 `Setting.cpp:5749`—`:5875`：离屏WARP target、真实字体替换/失败恢复与atlas退休，可验证字体生命周期，不代表全产品常驻内存。
- `InkeysHeadlessTests/setting_font_benchmark_tests.cpp:206`—`:213` 计算atlas纹理逻辑bytes；`:248`—`:251` 收集ImGui/FreeType分配与CPU atlas；`:269` 从磁盘载入源字库，`:275`输出CSV，`:280`—`:283`支持locale/scale过滤。CPU atlas是ImGui live子集，不能相加；gpu_atlas在WARP下也是逻辑资源bytes，非独立显存。测试源字体缓冲不等同产品的映像资源映射。
- 本轮未运行这些入口；它们需要已有匹配二进制及确认当前harness选项，并只能作为局部证据。

## Related Specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/rendering-and-ui.md`
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`
- `.trellis/spec/ppt-interop/index.md`

## External References / Versions

- 本报告以本地源码为依据；未引用外部文章推断当前产品行为。
- 产品在 `Setting.cpp:7` 固定 Dear ImGui 1.92.7；FreeType与DX11 backend有本地补丁，不能仅按上游默认行为判断。

## Caveats / Not Found

- 没有进程实际Private Bytes、Working Set、heap、WARP资源commit、CLR/JIT或模块驻留数据，没有可承诺的总节省百分比。
- 所有像素估算均为逻辑payload；驱动/对齐/ULW/DWM和堆余量另测，不能与进程Private/WS重复累计。
- 静态所有者调查不能证明泄漏；高水位缓存、事务峰值、映像映射、保留栈与已提交内存必须分开。
- Draw3独立device、历史tile、PPT/白板实际墨迹文档由另一研究主题覆盖，本报告不重复推断。
- 未改产品代码、工程、依赖或系统设置，未build/test、未启动GUI，未执行任何git操作。
