# C2 实施计划（方案 B，已审定）

> **主 Agent 审定（2026-11-20）**：按 design.md §8，删除白名单中 `UI3.Theme.cppm/.cpp`、`UI3.Lighting.cppm/.cpp` 两行，删除下表“步骤 1 · 隔离 UI3 帧输入”。预览使用全局已应用主题、背景/内容透明度与全局动画开关/速率；光照沿用现有入口。其余步骤编号不变。

本文件给下一轮源码实施使用。本轮交付仅本文件及同目录 design.md。前置为当前工作树的 C1 克隆、布局模型和 UserDivider 窄列实现；基线必须取同一 C1 工作树。父设计要求的整栏实例委托与本方案的差异见 design.md §1。

## 1. 下一轮源码白名单

| 路径（相对仓库根） | 唯一用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.ContentInstance.cppm`、`Bar.ContentInstance.cpp`（同目录，新建） | Bar 模块的 ContentInstance 分区与实现。导出实例、帧输入/结果、数值几何；模块内共用函数接收本帧所需的窄引用，放在同一分区内，不导出产品内部按钮给设置页。实例私有持有注册模板、按钮、场景、帧钟、renderer 与设备缓存 |
| `Inkeys/Inkeys/UI/Bar/Bar.cppm` | 增加 `export import :ContentInstance`；分区接口直接 import 所用的 UI3 及 Bar.LayoutModel，使用模块所有者的定义。实现单元 import :Button 及所需的 Metrics、MainLayoutPolicy、FramePacing、Material、Animation。根模块与分区不可循环 import |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 在原位置调用共享的本体/More/提示绘制和 More 布局计算。保留其余状态、目标时序、脏区、宿主生命周期和重放入口 |
| `Inkeys/Inkeys/UI/UI3/UI3.Theme.cppm`、`UI3.Theme.cpp`（同目录） | 帧局部只读主题快照及可嵌套作用域；预览路径无发布副作用，实栏默认读取语义不变 |
| `Inkeys/Inkeys/UI/UI3/UI3.Lighting.cppm`、`UI3.Lighting.cpp`（同目录） | 为现有状态机提供显式动画参数入口，默认入口沿用原采样时机和速率钳制 |
| `Inkeys/Inkeys/UI/Bar/Bar.Host.Test.cpp` | 在已有无窗口入口中增加独立实例的 fixture；复用已有 D2D 目标、像素读回与错误汇总，无需新命令行开关 |
| `Inkeys/Inkeys.vcxproj`、`Inkeys/Inkeys.vcxproj.filters` | 仅登记两个新编译单元。filters 镜像 Bar 目录，与 Bar.Scene 相邻（filters:2156）。沿用已有 C++20 module 项配置 |

C1 已有构造、布局与视觉函数直接 import。无需修改 Bar.Button、设置页、配置、语言资源、solution 或测试基线。DWrite/字体集合借 RenderPipeline 当代资源，renderer 的 resetDeviceCaches 只回收到本实例。新源码 UTF-8 BOM + CRLF，工程文件保持原编码和换行。非直观的帧所有权、源缝编号、字号/光源坐标用简短中文注释。

## 2. 实施顺序、验证与回滚点

每步先看 diff，保留 C1 和其他既有未提交修改。下表“回退”指撤销该步所引入的局部变更，不涉及 Git 写操作。

| 步骤 | 实施边界 | 本步验证 | 回退点 |
| --- | --- | --- | --- |
| 0 · 固定基线 | 记录工作树及编译配置；从当前 C1 源码生成真实主栏 golden、host-offscreen、attribute-dirty 的基准与退出码 | 确认每个 OK 场景有尺寸一致的完整 BGRA，记录已有 SKIP 及原因；基线失败先区分环境/既有失败 | 本步无源码变更 |
| 1 · 隔离 UI3 帧输入 | 主题查询在局部 scope 中使用副本，退出恢复，作用域外沿原 getter 流程。Lighting 在真实入口原有读取点（UI3.Lighting.cpp:202）取全局开关/速率，显式重载使用调用方的值；共用同一个状态转移函数 | 编译，嵌套深/浅/退出、无 listener 通知；灯光同输入轨迹与基线一致，rate 改变仍只乘一次。跑三项生产门禁 | 撤销两对 UI3 文件中的快照入口 |
| 2 · 共用画法与 More 几何 | 新分区、工程项与根导出一起登记。以 R:9757、9788、10479 为边界抽本体、More 和提示三个函数；分别保留 body、rigid、base 坐标作用域。显式传递布局可见性、fallback 解析后的 More 快照、shape/Panel、current 指针、字体资源/主题。抽 R:5505–5713 中与缩放、行翻转、定位有关的计算；主按钮外侧限位在真实调用点求，保持原值与算序。保留 More 目标设置、隐藏控件收敛及与真实推进的相位 | 比较抽取前后 golden 全部 OK 图像；复跑 host 的裁剪开/关与外部目标、属性 retained/full。重点对含 More、正文与浮窗的帧检查传入 renderer 和变换恢复 | 单独撤销调用点及共用函数，移回对应内联块；保留步骤 1 |
| 3 · 独立编辑投影 | 实现 design §3–4。对私有注册集合逐项克隆，More 单独取入口对象；两份交界、Redo、所有单例也重绑。外部语言快照更新时只刷新默认视觉数据。有效尺寸/选中/不可用取本帧投影；参考硬笔与白板图标/标签用实栏对应状态已有资源。初始化可见/几何/交互为本实例的端点。可见性与段/模型下标始终一起维护 | 默认与自定义模型、重复组件、连续用户线、More 为空、Setting 移动、A2 条件项、小尺寸 Redo 共移；核同 ID 各 occurrence 的地址、state 以及 clickFunc 为空。更换语言/操作模式不触发真实动作 | 移除实例投影，保留已通过的共用画法 |
| 4 · 自带推进与绘制 | 实现 Advance、Draw、ConfirmFrame、More 开合、局部指针光与 hover 截止；使用本实例字体服务、当前和最终倍率。用目标/源矩形与共用包络测量实现居中和净宽限制。先生成候选几何，再用同一批图元画 More、底板、按钮、提示；成功后发布。按设计处理 idle、切页、设备更换与失败 | 两实例交错推进、四组合和各 target 成像，核尺寸、颜色、光照锚及阴影；详见 §3。只固定时间推进，测试无 HWND | 撤销私有帧/绘制实现及对应测试增量，实栏保持步骤 2 |
| 5 · 命中与交付 | 完成 Lift、Place、CancelLift、Geometry、HitTest、InsertionGaps。保留源前模型、occurrence ↔ Position、临时编辑对象；不写配置。返回真实几何的 More 按钮/面板/关闭键；处理镜像、堆叠、换行、空段、窄线、隐藏项和无效版本 | 执行 §3 全部测试，以命中返回 gap 实际调用 C1 Move/Insert 核对结果；再次构建 solution 并跑三项生产门禁。检查新增源文件的工程登记、编码、换行及差异 | 先撤销几何/编辑增量。若无法证明生产等价，回退到步骤 0 的已测 C1 源码边界，保留失败报告 |

步骤 2 的公共 More 姿态输入须能表达真实 caller 的窗口尺寸、展开方向、可选限位、当前/目标进度、close press 与原 forcedOverflow。这避免用第二套手算去近似小尺寸网格。实栏原调用的 Bar 包装会读取 `barUISet.barStyle.darkStyle` 或 `barUISet.spec`（Bar.Button.cpp:1816–1845）；预览调用 UI3 重载并传本实例的 renderer 和 labelSize。若将目标设置也放入共用函数，实栏形参应保持旧包装使用的实例与参数。

## 3. 验收步骤

**构建与原门禁**。定位已安装 Visual Studio 的 ARM64 原生 MSBuild，在同一 PowerShell invocation 先规范化 PATH（按仓库规则移除 Env:PATH，令 MSBUILDDISABLENODEREUSE=1），使用已解析的可执行文件绝对路径构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，至少留 5 分钟。记录退出码与首个有意义错误，不调整工具链。用同机、同字体、语言、设备后端、固定时间轨迹和独立输出目录比较步骤 0 与候选版本。

| 门禁 | 通过条件 |
| --- | --- |
| `Inkeys.exe --ui3-golden-dump <独立目录>` | 对齐 results.log 的场景清单及 OK/SKIP/FAIL、manifest 的宽高，逐个比较每个 OK 场景的全部 .bgra 字节；要求零新增 SKIP、零 FAIL、原 OK 全保留。原有 SKIP 据实记录为未覆盖。单次程序只核两次新 fixture 自洽，不能替代跨版本比较（Bar.Golden.Test.cpp:600–646） |
| `Inkeys.exe --ui3-host-offscreen-test` | 保留已有 PlainBitmap、ExternalContext（含开/关外部 clip）与真实帧字节比较、失败/设备代次路径。新实例测试追加进同一返回码（Bar.Host.Test.cpp:166,222,344,526,803） |
| `Inkeys.exe --ui3-attribute-dirty-test` | 比较实际变化像素覆盖、原 retained/full 的逐字节结果、局部更新与字号登记；既有失败/漏刷/多余整幅检查全部保留（Bar.AttributeDirty.Test.cpp:101,421–471）。三条 CLI 均在 IdtMain.cpp:1914–1933 分发 |

**新离屏夹具**（加入 Bar.Host.Test.cpp）。下面是实施阶段需要新增的断言。当前文档轮没有新增或修改任何测试。

1. 大/小 × 深/浅，每格分别在 PlainBitmap、宿主已 BeginDraw 的 ExternalContext 上画同一固定轨迹；核非空读回、同格两目标 BGRA 全等、深浅表面/文字像素差异、两档高度同目标且按钮几何符合对应装箱。另在匹配投影、同倍率/语言/主题、关动态光并稳定后的实栏图中按条目对应比对图标/标签与底板几何，主按钮及锚点差异区域单列。无主按钮区域、背景外围 alpha=0，浅色阴影外圈有衰减且不贴裁剪边。用宽松目标裁切比对预算包络内外，验证 More/提示/过冲时阴影不截断。另以 GDI compatible 离屏目标过一次内容入口。
2. A 与 B 采用不同布局/倍率/主题/帧时间/指针，在同一 owner 交错渲染。以 B 单独运行的固定轨迹作对照，逐帧比较 B 的 BGRA、位置、按钮状态、字号修正和灯光，A 抓起/改变主题/销毁设备缓存后 B 仍完全相同。重复项轮流 hover、放入 More，确认另一份本地状态未改；同一 frame 重画不推进。真实主栏在夹具前后取同一输入作字节对照。
3. 抓独占列：原位从命中移除，动画中宽度介于端点，最终收窄；落入或取消时从当前值返回正确宽度，测试快速反向和停用动画当帧落位。双行共列仍占用时宽度正确保留。两个速率同一 dt 的到达时间与设置相符，idle 后首帧不跳。小尺寸 Recall 带 Redo，真实点击动作调用计数保持 0。
4. 两种尺寸展开 More，验证分组装箱、关闭栏、展开侧物理行、More 下层和主栏遮挡；折叠后面板项不再命中。连续用户线、两固定交界、空 B1/More、同 ID 两份、同段向右、跨段/换行、已有不可见项与 RTL 均按原模型下标回传。过期帧和失败 EndDraw 不可发布新几何；核对 CanPlace/Move 的实际结果。
5. 大/小同用小尺寸提示；499 ms 不显示、达到 500 ms 显示、移动跟随/换字、触摸/离开/编辑暂态关闭。指针移动只改本实例 cursorLight；第一灯随首个有效按钮，暗/亮及关闭边光均可测。窄目标按比例缩放并居中，桌面主栏缩放变化时固定宿主输入下像素不变。
6. 替换同设备新 context 和新 epoch，主体、More、角标和字体均可再次读回；失败帧保留上个成功几何或使其失效。确认宿主的变换/clip 和绘图生命周期成对，恢复后重复同一确定性轨迹。D3D11 纹理互操作、预乘合成与 Win7 实机观感由宿主阶段验收。

## 4. 现有符号索引

这里列出源码实施会用到的接口及源位置；design §6 中的 Preview 类型和方法均为提案。各组由其所属模块导入，避免跨模块前置声明。

| 现有符号 | 定义/合同位置 |
| --- | --- |
| BarLayoutModel、Segment、Position、CanPlace、Move、Insert | `Bar.LayoutModel.cppm:20,21,32,148,191,205` |
| BarButtonClass、BarButtonSetClass、PresetInitialization、RegisterBuiltInComponents、GetMoreButton、TryGetRegistration | `Bar.Button.cppm:46,144,169–175`；`Bar.Button.cpp:223,802` |
| ResolveBarButtonListLayout、DrawButtonVisual、ConfigureButtonVisualTargets、RetargetButtonInteractionVisual、AdvanceButtonVisual | `Bar.Button.cpp:1826–1878`；UI3 公共按钮实现 `UI3.Button.cpp:157,209,441,556,713`，本地状态/点击在 `UI3.Button.cppm:80–128` |
| ResolveBarA2Projection、ResolveRecallChrome、ResolveBarSizeMetrics、ResolveBarMainIconX | `Bar.A2.cppm:48`、`Bar.RecallChrome.cppm:139`、`Bar.Metrics.cppm:113`、`Bar.MainLayoutPolicy.cppm:30` |
| Panel::BindFrame/DrawFrame、UiShapeClass、UiWordClass、FormatCache、SurfaceShadowOutsetDip | `UI3.Panel.cppm:32`、`UI3.Panel.cpp:67`、`UI3.Primitive.cppm:259,457,483`、`UI3.Format.cppm:44`、`UI3.Material.cppm:147` |
| ThemeStyleState、GetThemeColor、常驻透明度 getter | `UI3.Theme.cppm:46,61–72`；实栏绑定及通知路径 `UI3.Theme.cpp:159–225` |
| UiFrameLightingInput/Snapshot、LightingStateMachine、SetFrameLightingSnapshot | `UI3.Lighting.cppm:19,36,57–75`、`UI3.Renderer.cpp:444` |
| FrameAnimationClock、UiValueClass、UiAdvanceAnimation、UiAnimationAdvanceContextClass、动画开关/速率 | `UI3.FramePacing.cppm:89–131`、`UI3.Animation.cppm:36–38,503,808–830` |
| UIRenderingServices、UIRendering、BeginFontFrame、ResolveWordFontSize、SetFontTargetScale | `UI3.Renderer.cppm:60–76,101`、`UI3.Renderer.cpp:178–221`；`SetFrameZoom` 的定义在 UI3.Renderer.cppm:145，调用见 R:2282 |
| FrameTarget、FrameTargetKind、FrameTransaction、FrameCoordinateMapping | `UI3.Host.cppm:22,33,148,196` |
| DeviceEpoch、SharedAssets、FrameContext、RenderSettingFrame | `RenderPipeline.cppm:41–65`、`Setting.cpp:5286` |
| DrawMainBarContent、SyncMainBarRendererFrameInputs、ReplayMainBarContent、ResolveBarHoverHintRect | R:8410、10514、10531、8161；提示延迟与几何函数定义在 `Bar.Metrics.cppm:254,270` |

本轮无新增 i18n 键或对外文案。现有标签来自注册原型和原有本地化资源；基准语言的生成流程见 `Scripts/i18n.ps1:13,496,732`，无需执行同步。
