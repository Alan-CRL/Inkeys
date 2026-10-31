# Research: 侧栏中段统一滚动条与导航命中间距

- Query: 当前侧栏中段的真实滚动容器、统一滚动条、按钮宽度/命中、子列表、蓝条、焦点与裁剪；最小启用策略及真实 headless 扩展点。
- Scope: internal
- Date: 2026-10-09

## Findings

### Files found

| 文件 | 作用 |
| --- | --- |
| Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp | 生产 RenderNavigationPane，固定顶部/底部、可滚中段、子列表、蓝条与紧凑浮层 |
| Inkeys/Inkeys/UI/Setting/Setting.Design.h | pane DIP 几何、导航状态/观测框、8 DIP 滚动条 token |
| Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp | ApplyScrollbars/ApplyPalette，统一主题滚动条 |
| Inkeys/additional/imfluent/imfluent.cpp | 真实 NavItemEx 的绘制、ItemAdd/ButtonBehavior 和焦点框 |
| Inkeys/additional/imgui/imgui.cpp | BeginChild 尺寸、ScrollbarY 判定、内容右边界、滚动上限 |
| Inkeys/additional/imgui/imgui_widgets.cpp | 真实滚动轨道、thumb、命中 ID 与交互 |
| InkeysHeadlessTests/setting_design_tests.cpp | 生产侧栏连续帧和 DrawData 回归，可直接扩展 |
| InkeysHeadlessTests/InkeysHeadlessTests.vcxproj | headless 直接编译生产 Controls/Shell |
| Inkeys/Inkeys/UI/Setting/Setting.cpp | 正文实际滚动 child 与页面 auto-height child，供拖动研究交叉对齐 |

### 当前真实布局与滚动范围

1. `RenderNavigationPane` 位于 Setting.Shell.cpp:88。外层 `##settings-pane`（:197）、顶部 `##settings-nav-top`（:203）、底部 `##settings-nav-footer`（:392）都使用 `NoScrollbar | NoScrollWithMouse`（:191）。
2. 唯一侧栏树形滚动宿主为 `##settings-nav-list`（:234-235）。当前仅传 `NoScrollbar`，未禁滚轮，因此已经能够竖直滚动，既有测试也已经验证其 `ScrollMax.y`、末项可达及蓝条跟随。需求 1 不需要新滚动容器、独立 thumb、全局输入或供应商改动。
3. 中段初始 WindowPadding 为左右 4 DIP、上下 0；child 同时保留 `AlwaysUseWindowPadding | NavFlattened`（:192、:199、:234）。固定区也使用这份 padding，但属于不同窗口，局部缩短中段右边界不会影响固定项。
4. `ResolveNavigationPaneGeometry`（Setting.Design.h:274-320）先分配固定区域：默认 36 DIP 行高、4 DIP gap 时顶部 144 DIP、底部 184 DIP，总固定高度 328 DIP。pane 位于 32 DIP 标题栏以下，默认 520/700/900 DIP 客户高度时中段约为 160/340/540 DIP。折叠六根内容几何为 236 DIP；通用完整展开额外加入约 160 DIP；最终真实滚动范围还包含已存在的尾部像素取整占位，应以生产 `ContentSize/ScrollMax` 为准。
5. 高度低于固定区总和时 `middle.Height()==0`，中段不 Begin（Shell:231）。`fits=false` 时固定区仍不互相覆盖，但超出 pane 部分裁剪；本轮不要假称新增滚动条能解决固定区域不足高度的既有边界。

### 统一样式与真实命中

- 统一 token：Setting.Design.h:21-22 的 `ScrollbarTrackSize=8`、`ScrollbarPadding=2` DIP；现有 `RowGap=4`（:14）可用作新增内容间距，不必新增全局样式常量。
- `ApplyScrollbars`（Controls:683-694）按 Pixels 重新设 8 DIP 轨道、2 DIP padding、3 DIP radius、透明轨道，以及当前 palette 的普通/悬停/按下灰色 thumb。`ApplyPalette` 最后调用它（:746）；无需给侧栏复制颜色或缩放。
- ImGui 1.92.7（imgui.h:32）在 Begin 中根据实际可用高度与内容计算 `ScrollbarY`（imgui.cpp:7838-7852），未指定 `HorizontalScrollbar` 就不会主动绘制横向条（:7852）。`ScrollbarSizes.x`（:7868）是垂直轨道的占宽，并加入 `DecoOuterSizeX2`（:7871）。
- Begin 计算 `ContentRegionRect.Max.x` 时已经扣除轨道宽与窗口 padding（imgui.cpp:7990-7993）。`GetContentRegionAvail` 对普通 child 读取这份右边界（:11619-11624）。不要再从可用宽度扣一次 8 DIP。
- `NavItemEx`（imfluent.cpp:3807-3846）用 `GetContentRegionAvail().x` 创建同一 `bb`，`ItemSize/ItemAdd/ButtonBehavior`、填充、文字 clip 和 focus ring 都读它（:3816-3847）。修改真实可用宽度能够同时缩短背景和点击框；仅改变背景绘制或 ClipRect 无法满足命中合同。
- 实际滚动条矩形/ID 在 imgui_widgets.cpp:964-983；ScrollBar 用这一整条轨道的 `ButtonBehavior`（:1066-1067），不是只用窄 thumb 命中。验证应以 `GetWindowScrollbarRect(list, ImGuiAxis_Y)` 为准，并确认 `ActiveId == GetWindowScrollbarID(list, ImGuiAxis_Y)` 时不发生导航/业务事件。

### 可直接用于设计的最小选择

1. 仅将中段 `NoScrollbar` 改为不含此禁用位的 flags，保留稳定 child ID、padding、NavFlattened、滚轮；不使用 `AlwaysVerticalScrollbar`，不启用 `HorizontalScrollbar`。
2. Begin 中段后读取该实际窗口的 `ScrollbarY`。有条时在 ImGui 已扣 8 DIP 的内容右边界基础上，再把 `WorkRect.Max.x` 和 `ContentRegionRect.Max.x` 同时局部向左缩 `Pixels(RowGap)`（4 DIP）；无条不增加缩短。这两者都缩的理由是现有代码已经同时维护它们，而普通控件的 `GetContentRegionAvail` 实际读后者。缩短只持续于中段窗口的本次绘制，不改全局 WindowPadding。
3. 在局部缩短后再取得现有 `right/contentRight`。通用父行现有的箭头预留逻辑（Shell:242-245）继续从该 `contentRight` 再预留 36 DIP；父项结束恢复到已缩短的边界；箭头仍在 `contentRight-36 DIP`（:294-299）。这样整行包含箭头的右边界与普通行一致，父项/箭头独立命中保留。
4. 内联 `##settings-general-children`（:328）维持 x 尺寸 0，即自动取父宿主缩短后的可用宽；子窗口 padding 0、子项 x=24 DIP（:334）保持，子项右端自动继承同一内容右边界。不要只缩六个根按钮却让展开子窗口覆盖滚动轨道。
5. 不直接改变蓝条逻辑。标记的 x 来自真实 item 左边界，统一缩右边界不会移动其 x；scroll delta 仍从真实 item/window 同步平移补间（Shell:433-467），indicator clip 来自 item 所属窗口（:161-166、:471-487）。滚动条交互在 Begin 的装饰阶段更新真实 Scroll.y（imgui_widgets.cpp:997-1011），之后导航提交会立即读到当前值。
6. 紧凑 pane 为 48 DIP；出现条及新增 4 DIP gap 后，行宽约 28 DIP（48-两侧8-轨道8-gap4），左边仍 x=4。真实图标槽 `bb.Min.x+8..+32`、16 DIP 图标轴 24 DIP 保持（imfluent.cpp:3830-3835）。图标的真实 glyph 边界和 focus ring 必须在多倍率下验证，不能为滚动条把图标轴右移。现有父项/子项状态及紧凑 tooltip ID 保持。
7. 展开/紧凑/覆盖导航都走同一中段宿主；覆盖时正文 x 不变，新增条只改变 pane 内按钮宽度。不调整 `ResolveShellGeometry` 或公开设置窗口 API。

### 连续帧与边界风险

- ImGui 自动 scrollbar 显隐基于上一帧内容（imgui.cpp:7842-7851）；真实 `ScrollMax` 使用当前内框而可能较显隐提前（:7923-7927）。使用真实 `ScrollbarY` 来决定 gap 可保证每一帧按钮都避开当帧实际条，不能仅据 `GetScrollMaxY()>0` 提前/延迟缩宽。动态展开、折叠与 resize 可能有正常的一帧显隐收敛，应测试连续帧而非把首次内容测量帧当稳定态。
- 不推荐为显隐无延迟另复制整树内容高度并 SetNextWindowContentSize；已有 tail pixel、child 动画高度、ItemSize rounding 会让复制公式容易偏移。若设计决定强制首帧已溢出就有条，需要单独写明测量方案并验证完整分数 DPI，不能把单纯移除 NoScrollbar 宣称为首次帧严格同步。
- 仅缩 `WorkRect` 无效：普通 NavItem 使用 `ContentRegionRect`。仅缩根条目也不够：内联 child 的真实窗口仍可能覆盖滚动条区域并成为 HoveredWindow。
- 有条时本来已有右侧 4 DIP padding，再追加 4 DIP 内容间距，实际按钮右端到轨道左端约 8 DIP。分数 DPI 下 track/padding 会 ImTrunc/round（imgui_widgets.cpp:1040），测试应核对实际边界并允许有根据的 <=1 物理像素量化。
- 完成展开时 `scrollSelectedGeneral` 的自动 scroll（Shell:361-376）和父项回焦（:260-266）必须继续正确；滚动条拖动不应重新展开、路由、关闭浮层或夺取正文业务输入。
- 拖动复用实现的调用阶段交由另一研究；若它在导航绘制后才调用 SetScrollY，其位移下一帧生效，测试必须照真实 ImGui 流程观察，不能后验改已生成蓝条顶点来制造同帧同步。

### 已有真实 headless 回归与建议扩展

1. `CheckHierarchicalNavigation`（setting_design_tests.cpp:1444）直接调用生产 `RenderNavigationPane`（:1532），以真实 child 名解析 window、Scroll/ScrollMax（:1541-1548），已有 parent/chevron/child/middle/footer bounds 观测来自生产控件（Design.h:131-138）。
   - 在 Frame 增加 list->ScrollbarY/ScrollbarX、实际轨道矩形、真实宽度和 thumb DrawData 观测。
   - 对所有中段根条目、父项加箭头、内联子项断言 right <= track.Min.x-gap；点击/按住轨道与间距，导航次数/业务次数/当前目的地/展开状态均不变，滚动条本身确实移动。
   - 无条时断言 ordinary/root/arrow/child 宽度恢复；固定顶部/底部横向宽度与位置不变。
   - 现有浅深、720×520/960×700/1440×900、expanded/compact/overlay 及末项实际 click 回归（:1867-1902）可直接扩展。
2. 同函数已有真实滚轮双向蓝条同步、切页过渡中滚动、反向切页与同帧切页+wheel（:2048-2089），裁剪外锚点恢复（:2093-2112），Debug往返（:2144 起）。加入实际 scrollbar seek/drag 和快速反向滚动，保留这些回归。
3. 动态展开/折叠最好选择 collapsed 内容可容纳、expanded 会溢出的高度（如客户端 600 DIP），观察每帧显隐与宽度；还需无条→有条 resize、有条→无条 resize、scroll end 后折叠 clamping、pane width 动画过程中紧凑模式恢复。极短 height 导致 middle=0 应验证未生成多余 child。
4. `CheckScrollbars`（:939）已检查主题 token 不累乘、普通/hover/active alpha、8 DIP 实际轨道及较窄 thumb；现有 helper允许装饰被合并到父 draw list（:966-969）。侧栏测试可复用这个观测，不能只检查 style 数值声称真实条已绘制。
5. `CheckNavigationScrolling`（:2323）是简化手工区域构造，现有 begin 自己仍给 `NoScrollbar`（:2356-2357）。它可保留固定分区基础回归，但新增需求验收应以生产 `CheckHierarchicalNavigation` 为主，避免改测试副本而遗漏生产中段或子列表。
6. 全局 CPU typography/scrollbar 阶段覆盖 scale 1/1.25/1.5/2/1.875/4（:2526）；生产层级当前仅 1/1.5/1.875（:2597-2598）。本轮至少保留这三种实际导航倍率，专门的 scrollbar-width 回归若成本可控可覆盖全部六倍率。
7. headless 工程直接编译生产 Controls.cpp、Shell.cpp（InkeysHeadlessTests.vcxproj:148-149），无需创建新构建系统/测试 UI。完整 solution Debug|ARM64 与 --no-window 可验证当前范围；真实桌面手指/鼠标观感仍是独立人工验收。

### Related specs / External references

- .trellis/spec/native-desktop/index.md：UI 所属范围及完整 Solution 验证边界。
- .trellis/spec/native-desktop/rendering-and-ui.md:1942-2008：Setting 全页合同，固定/中段、字体/动画、统一滚动条、蓝条锚点、真实 DrawData 验证。
- .trellis/spec/native-desktop/rendering-and-ui.md:2046-2047：主题 preset→accent→项目 token→scrollbar 顺序。
- 以仓库现有 Dear ImGui 1.92.7 的 vendored 主实现为主要技术来源；没有外部网页依赖，不需要依据不匹配版本的网上示例改变供应商库。

## Caveats / Not Found

- 仅研究并写本文件，未实现、未运行 build/test、未启动 GUI、未执行 git 操作。
- 角色隔离规定研究代理不可读取 implement.jsonl/check.jsonl；已读子 PRD、workflow、相关 spec 和生产/测试源代码，未读取该两份 manifest。
- settingui2 按住拖动代码由并行研究负责，本文件不复制或重新设计该算法。
- 新子任务处于 planning；本报告的推荐是可供 design 选择的最小策略，并不是未经设计批准即写代码。
