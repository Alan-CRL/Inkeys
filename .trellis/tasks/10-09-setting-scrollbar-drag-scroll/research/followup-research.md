# Research: 主页拖动遗漏、侧栏间距与滚动条显隐动画

- Query: 定位真实设置主页拖动遗漏，枚举竖直滚动宿主，计算侧栏右间隙，研究不修改 vendor 的宽度及滚动条显隐动画边界。
- Scope: internal
- Date: 2026-10-09
- Task: `.trellis/tasks/10-09-setting-scrollbar-drag-scroll`
- Baseline: 主会话报告上一轮提交为 `b8beea0b`；本研究不执行任何 Git 操作。
- Authority: 本轮用户要求进一步修复并再次提交；随后追加允许优化原 helper 的性能/跟手性，覆盖上一轮原正文不可变约束。性能/低延迟接口由另一研究代理负责，本报告侧重宿主、交互覆盖及动画。

## Findings

### Files found

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:2289` | 所有设置页面共用外层 `##setting-content`，内部还有 `AutoResizeY` 的 `##setting-page-content`。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:3863` | 旧统一拖动部署在全部 UI 结束、Render 前调用一次。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:214` | 原旧 `ScrollWhenDraggingOnVoid` 与当前部署选择逻辑。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:735` | 统一 8 DIP scrollbar track、2 DIP thumb padding 和主题颜色。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp:201` | 导航内层横向 WindowPadding 为 4 DIP；中段 Begin 后又减 RowGap。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp:514` | 通用导航 flyout 的实际 popup 宿主，短客户区可滚动。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:146` | 真正 RenderHome，包含巨幅按钮和嵌套 auto-size child/card。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Design.h:76` | 现有 NavigationTween：归一化进度、当前值反向重定向、关闭动画立即归位。 |
| `Inkeys/additional/imgui/imgui.cpp` | 当前 1.92.7 Begin 几何、装饰提交、hover/MoveId 及 input ownership 实现，只读。 |
| `Inkeys/additional/imgui/imgui_widgets.cpp:1017` | 实际 ScrollbarEx，可本地复用 native thumb 计算、轨道页翻、抓取语义。 |
| `Inkeys/additional/imfluent/imfluent.cpp:4210` | ComboBox 内部 popup 与 clipper，只读。 |
| `InkeysHeadlessTests/setting_design_tests.cpp:2372` | 上一轮通用拖动 harness，尚未使用完整 RenderHome 跨卡拖动。 |

### 主页没有被显式排除，但旧 OnVoid 语义与实际主页不匹配

直接确认：正文路由 Home 与其他页面一样使用 `##setting-content → ##setting-page-content`；后者 AutoResizeY。RenderHome 内部实际结构如下：

- 页头/hero 矢量是 page child 的 Dummy/TextAt/AddRect 内容（Pages:146-183），不是独立 Card child。
- 两张 feature 卡本身是覆盖整卡的 Button（Pages:199）。用户认为是在“内容”上拖动，ImGui 认为这是正在按住按钮。
- 常用设置 `##home-common` 是 AutoResizeY child（Pages:223），内部条目亦是整行 Button（Pages:235）。
- `##home-author-section` 是另一个 AutoResizeY child（Pages:252），里面 `##author` 是 ImFluent AutoResizeY Card child（Pages:255；imfluent.cpp:3249）。
- outer content 应当承担整页滚动；auto-size children/card 不应无理由各自消费一次 delta。但应按真实 ScrollMax 处理任何确实受约束产生竖直条的 child。

当前 ApplyDragScrolling 由 HoveredWindow 沿 ParentWindow 找最近 eligible owner（Controls:251）。因此“主页遗漏是未把 Home 加入列表”不成立。真实问题在原 helper 的后续 held/capture：

1. `ButtonBehavior(window->Rect(), overlayID)` 经 ItemHoverable 处理；库在 imgui.cpp:4930 要求 `g.HoveredWindow == g.CurrentWindow`。Hover 在作者卡、common child 或 page-content 时，outer owner 的 overlay 不会获得自己的 ActiveId。
2. ChildWindows fallback 可以认祖先：imgui.cpp:8430-8439 使用 `IsWindowChildOf(ref_window, cur_window)`。**只有同时传 RootWindow 才先取 root**，不能声称 ChildWindows 会自动把 CurrentWindow 变为 RootWindow。
3. fallback 还要求无其他 ActiveId 或 ActiveId 恰等于 hovered 子窗的 MoveId（imgui.cpp:8445-8447）。Home 整卡/整行 Button 的 ActiveId 使 fallback 失败；旧版策略只支持 void，不满足用户本轮“按住内容”在主页交互卡片上的自然预期。
4. fallback 不为滚动 owner 建立 overlay 抓取，而仅当帧 held=true。StartMouseMovingWindow 可能于 EndFrame 抓取 nested child's MoveId（imgui.cpp:5340）；有 MovingWindow 时 FindHoveredWindowEx 又将其锁为 HoveredWindow（6182）。因此“换 child 必然 MoveId mismatch”只是部分配置风险，不能当成实机已复现的唯一根因。若 root 使用 NoMove、关闭移动或点击已禁用 item，hover/MoveId 路径会改变。
5. 整个 ImGui 区域外 Down 的 MouseDownOwned=false 边界仍须保留（imgui.cpp:5408-5432），不应以全局强制 hovered 修补。

建议：本轮新授权允许改 helper，应明确内容拖动拥有持续单一 capture，沿真实父链选 owner，然后在本帧绘制开始前消费位移。需要区分可点击内容卡片/Selectable 的点击与拖动，以及 Slider/TextBox 的独立操作；不能以只保留 void 且不给 Home feature drag 测试声称修复所有内容。具体触发阈值/所有权及延迟由性能代理提供设计。必须使用真实 RenderHome 复现 hero、feature、common、author 和跨子窗路径，不能再次只验证固定一个 Dummy child。

### 全部当前实际竖直宿主

| 宿主 | 当前入口 | 说明 |
| --- | --- | --- |
| `##setting-content` | Setting.cpp:2289 | Home、General、Debug、所有其余页统一宿主；blockContentInput 时 NoInputs。 |
| `##settings-nav-list` | Shell:235 | 中段树；展开/紧凑/覆盖模式均可溢出。 |
| 通用导航 modal flyout | Shell:514 | 按客户区 clamp 后短窗实际溢出；保留 fade-out/释放屏障。 |
| Combo 内部 popup | ImFluent ComboBox:4320 | capped popup + clipper，长列表可滚；真实条目全是 Selectable，旧 void 拖动仅窄 padding 可用。 |
| 所有受约束的普通 child/card/expander | Pages:223/252、Controls:555/563、Setting.cpp:2434/2748/3596 | 多数 AutoResizeY、无条；若真实 ScrollMax/ScrollbarY 有范围应接入，不应靠固定名字排除。EDID 完整高度卡合同仍是不内部滚动。 |

root、settings-pane、nav-top/nav-footer、general-children 动画裁剪区、titlebar、scrim 有明确 NoScrollbar/NoScrollWithMouse；tooltip/NoMouseInputs 排除。最近实际可滚 ancestor 的选择应保留 fixed child 向祖先转交的语义，Popup 的开放态须精确检查祖先 PopupId，关闭/隐藏宿主终止 capture。

### 侧栏间隙过大：4 DIP native padding 被再叠加 4 DIP

Shell:201 设置导航 child 的横向 WindowPadding=4 DIP。原生 Begin 在 ContentRegionRect.Max.x 扣轨道宽及两侧 padding（imgui.cpp:7991-7993）。无条时按钮右边缘为 window.right-4。有条时轨道左边缘为 window.right-8，而按钮右边缘在原生 Begin 后已是 window.right-8-4，天然存在 4 DIP。

Shell:243-244 再减 RowGap=4，把按钮→轨道 gap 变成 8 DIP，额外将整个右边沿收缩到 window.right-16，符合用户截图的过大空白。

最小稳态修正：移除额外 RowGap 减法，沿用 native WindowPadding.x=4 DIP。若引入动画重新构造 content bounds，仍保持 `content.right = visibleTrack.left - 4 DIP`（满显示终点），根项/父项箭头/内联 child 的 WorkRect 与 ContentRegionRect 一起更新。必须检查真实 ItemBounds，不能只调背景顶点。

### 动画提交时机与可行边界

- 现有 NavigationTween（Design.h:76-100）可以用于显隐进度；Target 从当前 value 开始反向，Advance 按 delta/duration，动画关闭 Reset target；速度为 state.animationRate，dt 由既有帧预算限幅。复用约 160 ms / rate 的现有节奏，不新添配置。
- native Scrollbar 在 Begin 的 RenderWindowDecorations 内已执行 ButtonBehavior 和绘制（imgui.cpp:7958-7960；widgets:995-1008）。Begin 之后才 PushStyleColor 无法影响该 thumb。
- child 装饰可能暂时写入 parent DrawList，之后恢复 child DrawList（imgui.cpp:7944-7963）。因此仅扫描或改 child DrawList 的全部顶点 alpha 会漏掉真实 thumb，且可能误淡化内容；不能拿样式值或 child-only draw 伪造成功。
- native ScrollbarY 变 false 当帧不画任何 thumb，不能通过 alpha 乘法复活消失的条。宽度也在 Begin 按 ScrollbarSizes 直接扣除，End 后只改 WorkRect 会留下初帧 snap、裁剪或嵌套 child width 不一致。
- ImGuiContextHook 只有 NewFrame/EndFrame/Render/Shutdown 阶段（imgui_internal.h:2173），**没有 BeginWindow hook**。不应臆造 per-Begin hook 或库私有 SetCurrentWindow API。

可行实现路线需要实际代码验证，而非照搬整份 Begin 几何：

1. 已知可滚宿主使用项目内部 Begin 包装，先从真实现存 window 内容尺寸判断目标，局部 tween scrollbar reserve/alpha，并在 Begin 前设置相关 style/flags。native 轨道随 reserve 改变时注意 padding 的截断与 hover width，不允许按钮 overlap 短暂暴露的轨道。消失尾帧可短暂保持强制 vertical 装饰并关闭其 input，归零才移除；不能强制永远常驻。
2. 或在项目 Design 层接管竖直装饰，保留 ImGui ScrollMax/ScrollTarget/wheel，原生 NoScrollbar 后用真实 `ScrollbarEx` 绘制/命中。该函数的尺寸、ID、rounding 及输入语义均已在当前版本真实存在（widgets:1017）。需要在内容绘制前完成滚动，以免 scrollbar 与内容错一帧；若完全隐藏后无range，不提交交互。渐显时轨道实际可见/命中宽必须与 reserve 一起变化，不能固定8 DIP track却只reserve部分宽而与按钮冲突。横向条仍留原native合同。

第二路线对 Combo hidden popup、AutoResize child/Card 的接入较难；第一路线同样无法仅在 ComboBox 返回后补入早已绘制的 popup Begin 动画。当前 ComboBox 是单个 vendor 调用包裹全 popup，项目 wrapper（Controls 内 ComboBox）需要确切获取稳定 popupID或本地复制必要 popup部署，禁止未验证 hash 私有命名和大范围 vendor重写。若本轮动画只施于 sidebar，应在需求/最终说明明确边界，不能默认为“所有条均淡入”。

### 验证建议

- 三倍率真实 RenderHome + 生产 root flags 连续帧：hero空白、整卡 feature Button、common整行、author非交互文字、跨 parent/sibling、离窗及 release；观察当帧 Scroll、子内容顶点位置、路由/点击次数不误触及 sustained ownership。
- 对竖直条宿主矩阵逐项断言实际 scroll 变化：正文、sidebar、modal flyout、Combo的 Selectable区/blank区以及受约束 child/card。无需给无条的AutoResize卡制造内部滚动。
- sidebar 稳态精确测量 4 DIP gap（容许像素取整），所有实际 parent/root/arrow/inline bounds 都不得进入 track，固定顶部/底部不变化。
- 显隐至少采样首帧/中间帧/终帧：实际 item width 连续变化、thumb RGB不变而alpha渐变，消失tail存在但非交互，快速反向不回跳、不同 rate 中途改变、动画关立即归位。实际decor可在 parent DrawList内，要观察总DrawData及相应track区域。
- resize、group展开、跨短/长页和DPI变化重新计算真实目标；避免 content宽影响换行高度后造成overflow反馈振荡。连续帧稳定性测试应覆盖边缘恰好可见窗口高度。
- 性能回归观察输入结束到本帧绘制之间的更新、每帧仅一个owner及没有全窗口/全顶点扫描；不能声称 headless 证实实机触屏渲染延迟。

### Related specs / references

- `.trellis/spec/native-desktop/index.md` 已读；`rendering-and-ui.md:1975-1976` 是上一轮 gap4和原helper严格不可变合同，本轮用户新授权要求更新，不能让旧规范覆盖用户最新要求。
- `.trellis/tasks/10-09-setting-scrollbar-drag-scroll/research/spec-boundaries.md` 已读。
- 当前原生 API 版本由本地 `imgui.h:31-32` 确認为 1.92.7 / 19270；本研究依本机源码，不依赖外部推荐或版本假设。未浏览外部网站。

## Caveats / Not Found

- 本研究只读产品/库，没有启动GUI、写产品/测试、运行构建或新输入序列；不声称已复现实机“主页完全不动”的唯一原因。上述 HoveredWindow/ActiveId 限制为直接源码事实，因果需新增真实 RenderHome 回归。
- 出于研究角色隔离未读取 implement.jsonl/check.jsonl；hook明确 Active task，已读取 workflow、prd/design/implement、相关规范与实际源码。
- 没有公开 per-window Begin hook，没有公开 SetCurrentWindow；不要依据不存在的API选方案。
- 本报告行号以研究时工作区为准；实现代理可同时修改产品，最终行号需按最终diff校准。
