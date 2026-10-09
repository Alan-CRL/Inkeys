# 本轮后续实施记录（最终自动验证通过）

## 需求与范围

上一轮由主会话提交 b8beea0b。用户本轮授权修复主页与全部实际竖直滚动宿主、侧栏重复间距、滚动条显隐与内容宽度动画，并优化拖动响应；该追加授权覆盖先前严格保持旧 helper 正文的限制。本代理仅改设置内部与现有无窗口测试，不修改 vendor、Win32 消息、依赖、配置格式、工程或提交 Git。父任务及提交由主会话处理。

## 已实施行为

- 侧栏使用原生 WindowPadding.x 的 4 DIP 间隔，删除此前额外 RowGap 的重复扣减。按钮真实绘制/命中宽度共用 native WorkRect 与轨道尺寸。
- 设置内共享 Begin 前滚动样式作用域：8 DIP 轨道宽度与 2 DIP padding 随既有 NavigationTween 进度变化，三态 thumb alpha 同步渐变；溢出判定仍由当前 ImGui 1.92.7 原生 Begin 完成。消失阶段在内容 items 提交前保留过渡宽度，并仅重绘缓存的 thumb 几何，无透明输入项。关闭或未提交后重新出现从隐藏状态开始。
- Card/Expander 的跨 Begin/End 样式栈保持原样，滚动作用域独立备份/恢复 ScrollbarSize、ScrollbarPadding、三个颜色字段，避免交错 pop；Details 在 content 回调前结束该独立作用域，nested child 不继承祖先透明度。
- 正文、自动高度页、主页 common/author 子窗口、Card/Details、中段导航、导航浮层及 Combo 使用共享作用域。Combo 因 vendor 将 Popup Begin/items/End 封装在同一函数，采用必要的设置内部提交包装；保留同版稳定 ID、原 alpha 加权颜色动画、按钮/Selectable/键盘、位置与 clipper 逻辑，不修改 vendor。
- Mouse/TouchScreen 沿现有 ImGui 输入路径工作。已捕获拖动在 NewFrame 后采样一次，在真正宿主本帧首次 Begin 前投递 target，使 native 同帧更新 Scroll/cursor/clip/items。未提交或已关闭 Popup 不消费 pending target；零位移快返；全路径不扫描全部窗口/顶点、不分配动态 child 名字符串、不搬运绘制顶点。
- 仅内容型点击项（主页 feature/common、导航与 Combo Selectable）登记稳定候选 ID，超过 ImGui 已有 MouseDragThreshold 才转移抓取，轻点保留业务动作；Slider/TextBox/普通按钮与 native scrollbar 保留输入所有权。弹层无有效范围不向正文传播拖动；维持单宿主、释放与导航屏障。
- 物理位移只应用一次，保留浮点累计，并与 native ImRound64/clamp 量化一致；先前或 Begin 前迟到的外部 scroll target 保留优先权，下次拖动重建基准。

## 自动验证过程与真实边界

完整 solution 与 --no-window 统一由主会话调用，无 GUI。

- build-first：22.87247 秒失败，纯布局 Design.h 引入 ImVec2/ImGuiWindowFlags 声明缺类型。仅将带 ImGui 类型声明移到已有 Controls.h，不污染纯几何头。
- build-second：87.8447836 秒成功；headless-first：114.8640786 秒失败 60 条，均为旧每帧满宽 thumb 断言，以及 Home 固定两帧显隐采样。实际新拖动/业务输入断言通过。改成跟踪真实 native target 过渡，保留严格中间值、终值和 DrawData 证据。
- build-third：43.5759277 秒产品编译成功，新增测试 NavigationState 空 aggregate 临时触发 MSVC C2512；仅测试改命名 fresh 对象，修正同处 shadow warning。
- build-test-fix：33.3575245 秒成功；headless-second：119.3953078 秒仅 1 条失败：1.875 倍短 Combo 最终零 range 断言。其他真实 Home、导航、Combo、外部 target、原控件与既有回归通过。后续 diagnostic 实测与未修改 vendor 在相同字体、内容、稳定 Popup ID 下完全一致：1.875 倍 bar=true/reserve=15px/ScrollMax=0.5px/content=127px/inner=134px/size=134px/padding=3.75px；原生 auto-size 截断造成真实小数溢出。产品不改变 native 规则；两条内容做严格 vendor 等价检查，另用真实零溢出空列表验证退出完全释放。
- build-diagnostic：28.6563174 秒成功；headless-diagnostic：117.6860417 秒仅原先 Combo 已定位预期失败；新增真实 Card/Details 回调内、nested child 返回后的 vendor padding/background/stack 与独立五个滚动字段断言全部通过。
- build-final：完整 InkeysRepo.sln Debug|ARM64，ARM64 原生 MSBuild 与同 invocation PATH workaround，65.7257406 秒成功、0 errors、9 条既有 vendor 警告；headless-final：完整 --no-window，125.8065025 秒 Exit0、stderr 0，包含 PASS animation correctness。
- 最终空 Combo 原生真实零溢出终值在 1/1.5/1.875 倍全部 bar=false/reserve=0/ScrollMax=0，退出过渡与无背景传播输入通过。两条短列表与未修改同版 vendor 在全部倍率的 bar/reserve/range 严格一致。
- Home 实际首帧 bar=false/reserve=0，次帧 native bar=true/reserve=0，随后发生宽度/alpha 过渡。1 倍浅色出现第三帧 reserve=2.24863px、内容宽 791.751px、alpha=29；完全出现 reserve=8px。退出首次 native bar=false 时 reserve=8px，下一帧 5.75137px、宽 788.249px、alpha=75。深色第三帧 4.76368px/alpha83；退出下一帧 3.23632px/alpha57。1.5/1.875 倍同模式按物理像素缩放。
- 真实 RenderHome 使用生产 root flags、真实页面/Card/字体/DrawData。Mouse 与 TouchScreen 起拖在 hero、实际 feature、author、common，跨 child 与内容卡验证当帧 Scroll 与实际 glyph 顶点移动，取消拖动后的业务点击。对照 b8beea0b 旧 helper 在同一真实页面 outer padding 起拖：位移输入帧 Scroll/glyph 不变，下一绘制帧变化；新实现运动帧直接变化。确证少一个绘制帧，不把约 60Hz 推断当作实机端到端时间。
- 小位移通过真实 AddMousePosEvent 每次 0.25px 序列验证。当前 ImGui 会 floor MousePos，IO.MouseDelta 因此呈 0/1 像素；测试不伪造 MouseDelta，不宣称改变该库采样量化。
- CPU 采样为完整真实 Home 提交（NewFrame 后至 Render，扫描测试顶点前），新/旧算法各预热后独立 100 idle 与 100 captured held 帧，当前共享动画包装/native UI 相同，非旧完整应用版本也非纯 helper 微基准。headless-second：1 倍 held 新 median487.3/p95691.2µs、旧516.6/685.6；1.5 倍新592.8/728.3、旧483.9/662.9；1.875 倍新594.3/1394.7、旧489.8/685.7。跨倍率未得一致 CPU 降低，不能宣称百分比性能提升。可靠改善是取消额外绘制帧与稳固内容项拖动归属。


## 最终完整 Home 提交 CPU 采样（µs，信息项）

保留全部组，避免选择单一倍率或尾部分位数得出提速结论。当前动画包装与 UI 在对照两组中一致；本机后台负载会影响结果。跨运行与跨倍率没有一致 CPU 提升，唯一明确的响应时间证据是输入至 Scroll/glyph 绘制少一帧。

| 倍率 | 算法 | 状态 | 采样帧 | median µs | p95 µs |
| --- | --- | --- | --- | --- | --- |
| 1 | 新实现 | idle | 100 | 502.6 | 881.3 |
| 1 | 新实现 | captured held | 100 | 497.6 | 710.5 |
| 1 | b8 helper | idle | 100 | 571.7 | 854 |
| 1 | b8 helper | captured held | 100 | 487.5 | 943 |
| 1.5 | 新实现 | idle | 100 | 614.3 | 1045.8 |
| 1.5 | 新实现 | captured held | 100 | 479.4 | 903.2 |
| 1.5 | b8 helper | idle | 100 | 488 | 863.7 |
| 1.5 | b8 helper | captured held | 100 | 616.7 | 944.7 |
| 1.875 | 新实现 | idle | 100 | 651.8 | 4966.1 |
| 1.875 | 新实现 | captured held | 100 | 635.4 | 1364.6 |
| 1.875 | b8 helper | idle | 100 | 620.1 | 2676.3 |
| 1.875 | b8 helper | captured held | 100 | 661 | 4498.9 |

## 文件与验证边界

产品：Setting.Controls.cpp/.h、Setting.Design.h、Setting.Pages.cpp、Setting.Shell.cpp、Setting.cpp。测试：InkeysHeadlessTests/setting_design_tests.cpp。源文件保持 UTF-8 无 BOM 与 CRLF，关键边界中文注释。

本代理产品与测试实施已完成，完整 solution 与 --no-window 最终通过；独立检查、国际化/格式审核与任务关闭/commit 由主会话统一收尾。Combo 小数原生规则无需产品修改；Card/Details 内容内与 nested 返回断言也已通过最终测试。真实桌面触摸/鼠标与最终观感按用户规定留人工验收。本轮未打开 GUI、Computer Use，未 commit/push。
