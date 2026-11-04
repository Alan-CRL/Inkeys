# SettingUI 样式与动画研究（规划阶段）

研究基线：`feature/i18n`，HEAD `dbabd500`。本轮仅静态阅读及颜色取整算式探针；没有运行产品 GUI、改动产品源码或证明实机根因。

## 已有架构和必须复用的合同

- `.trellis/spec/native-desktop/rendering-and-ui.md:2118`：设置动画沿用现有开关、速率、帧时钟，不新增持久化开关或调度线程。
- 同文件 `:2145`/`:2146`：真实字体测量、响应式行高和 action 一帧只执行一次；不能为了预排布重复执行业务回调。
- 同文件 `:2152`：各实际滚动宿主共享本地 ScrollbarStyleScope；8 DIP 轨道、2 DIP padding，正文/导航/卡片/详情/浮层/Combo 不可混淆所有权。新需求将改变正文的条宽动画合同，需精确界定而非无条件修改所有宿主。
- 同文件 `:2318`/`:2320`/`:2323`：主页开屏按真实 ShowSerial 播放，取消/重建/恢复有严格门禁；主页已具备终态滚动布局准备，尺寸/字体/语言/主题改变撤销快照但不重播。
- 同文件 `:2324`/`:2325`：CPU 布局/输入仍逐帧进行；成功 Present 后才确认 FrameSubmissionCache。不可把“同帧像素相同”视为整个设置客户端 Idle。
- `.trellis/spec/native-desktop/rendering-and-ui.md:2487`：Win7 系统框架、系统菜单及八方向缩放语义须保留。无窗口回归不能替代 Win7 Aero/焦点/Snap 验收。
- 上轮 20 语及 FreeType 热切换已完成；不修改字体文件、排印标尺或语言索引。

## R1 开屏标题栏控件

**直接证据**：`Setting.cpp:2782` 版本入口，`:2815` 主题入口，`:2838` 反馈和 `:2841` 调测入口在标题栏中正常登记和绘制；`:2867` 才配置本帧主页开屏状态。内容/导航分别在 `:2896`/`:4763` 应用输入屏障，但上述入口不在这些 scope 内。

`Setting.Pages.cpp:410` 的开屏 DrawData 过滤保留整份标题栏；因此不能指望正文覆盖层或过滤阶段自动隐藏这四项。窗口图标、标题和最小化/最大化/关闭为独立系统 chrome，用户未要求隐藏。

**候选方案**：在标题栏提交前获得权威开屏可见性状态；同时管理真实绘制、ImGui item 注册与 Win32 标题栏命中区域，不仅把图标 alpha 设为零。按用户文字，遮盖层渐隐完成后再出现。输入取消与关闭动画也需直接恢复一致终态。

## R2 正文宽度迟变与 R6 页面分批入场

**直接证据**：`Setting.Controls.cpp:302-345` 使用同一个 `phase` 乘 ScrollbarSize、padding 和 thumb alpha；`:325-329` 在未连续提交的页面重新从零开始；`:349-359` 依据 Begin 后的真实 ScrollbarY 更新 target。故出现轨道时其布局宽度会逐帧增加。

`Setting.cpp:2904-2907` 仅在主页开屏/准备阶段使用 settleScrollbarLayout；普通新页面没有相同稳定策略。`:2924` 内层 AutoResizeY + 外层正文 owner 也必须一并考虑，不能只修改一个常数。

`Setting.Pages.h:50-60` 已在实际 destination 改变时触发 `contentReveal`；`Setting.Design.h:213` 为现有整页 120ms 淡入。`Setting.Pages.h:217` 绘制子页也有相同 reveal。当前没有用户要求的逐块上浮入场，不能再叠一套互不协调的全页 alpha。

**候选方案**：分离“轨道布局占位”和“thumb 视觉 alpha”；新页面第一次可见之前完成真实宽度/高度布局，按最终宽度入场，末尾只渐显滚动条。测量与动画必须使用稳定的语义页面/子页/块 ID，不能用翻译文案当标识；不为长页面按全部离屏条目累计无上限 stagger 延迟。

**技术待验证**：优先真实现有布局准备/缓存尺寸失效策略；若需要测量 pass，必须切断 action、Shell、配置写入、焦点和 active-id 副作用。不能通过永久 AlwaysVerticalScrollbar 给全部短页加空白来掩盖首帧问题。

## R3 导航移出后残留叠色

**直接证据**：`Setting.Shell.cpp:187` 调用真实 `ImFluent::NavItemEx`，`imfluent.cpp:5320` 使用 `AnimateColorU32`；`imfluent.cpp:987-996` 未选中且不悬停/按下时目标为透明。

`imfluent.cpp:823-840` 每帧将当前 U32 色值指数趋近 target 并回写 U32；`:686-700` 使用 alpha 加权插值，再 `alpha + 0.5` 取整。最后若干单位 alpha 会因每帧反复取整而停住，缺少有限终止机制。`Setting.Controls.cpp:1623-1625` 半透明模式默认 hover 为黑9/白15。

**纯算式探针（非产品回放）**：目标alpha=0、seconds=.083、初始alpha=9/15，连续3秒，按源码每步指数插值并取整：30Hz残留1/1；60Hz残留2/2；120Hz残留5/5；240Hz残留9/10。与高刷新率下透明底上残留叠色吻合，是有证据的根因候选。

**候选方案**：优先在颜色动画真正保存状态的边界解决终止/量化，不以侧栏强制盖纯色绕过。涉及 vendor 的局部补丁必须有最小范围和中文 INKEYS PATCH 注释，验证其他共享调用控件、透明端点和浅深主题。选中项本来就应保留 subtle fill，不能误改为透明。

## R4 连续 resize 卡顿

**直接证据**：`Setting.cpp:1733-1745` 记录交互类型，`:1790-1803` 窗口线程只排队最新 resize；`:5146-5149` 只有 Move 暂停，Size 必须持续真实渲染；`:5171-5184` 消费 resize 后重建呈现目标。`Setting.Base.cppm:118-124` 每次先销毁 RTV、ResizeBuffers 再创建 RTV。

现有 SessionState 已有 latest-size mailbox/serial；不能再造另一个互斥队列。Setting 借用共享 WARP/device/immediate context，修改耗时和持锁会影响 Bar/PageControl，须同源测量。尚无本次 profiler 证据，不能宣称 ResizeBuffers 是唯一瓶颈。

**规划验证**：区分 Win32 message/border 同步、CPU layout/测高、资源重建、backend 与 Present 阶段；尺寸风暴先合并并跳过无变化尺寸，不重建字体/图片。保留交互中持续内容更新与松手最终尺寸提交；若测量显示需要暂停部分效果或改变刷新策略，先作为产品取舍确认，不能自行改成拖动期间冻结整窗。

## R5 最大化/还原的透明度和模糊度跳变

**已有决策**：`.trellis/tasks/archive/2026-11/11-01-setting-translucent-seams/prd.md:20` 已将静止、移动和尺寸交互统一为 BlurBehind，单层 AppBase 负责 tint；它覆盖更早任务的交互时 Acrylic 切换方案。

**直接证据**：`Setting.cpp:266-288` 现已不按 Move/Size 切换 accent。`:314-332` ApplySettingBackdrop 每次会先 Disable 后启用；`:402-423` 通过 HWND 主题应用路径调用。`:303-311` frame margin 区分 IsZoomed；`:1818-1821` 位置/大小/maximize 都同步 border；`:2704-2707` 最大化也改变圆角几何。

**待定位**：目前没有代码证据表明最大化按设计应该改变 tint；需跟踪最大化/还原期间 native 消息、ApplySettingBackdrop 调用、RTV/清屏背景和 frame margin 变化，区分不必要 disable/re-enable、首次失配帧与系统 DWM 原生过渡行为。不承诺未经真实桌面验证的“仅保持同常数即完全解决”。不引入新的 Mica/Acrylic 切换或禁用 DWM 动画来隐藏问题。

## R7 条件条目显隐

已有 `Setting.Controls.cpp:592-619` 的 NavigationTween 是响应式说明布局，不是整条显隐；`Setting.Shell.cpp:354-392` 是导航子项高度揭示，不能等同于正文动态条目。

**候选方案**：为实际条件块保留稳定身份、显隐进度、当前测量高度，退出期间保留必要视觉而立即撤销不应再生效的业务输入；在真实布局中占用插值高度，让后项移动。快速反向从当前值续接。滚动 owner 使用同一动态总高度，维护合理锚点及边界，不对滚动条几何做事后跳补。不绘制两套可交互副本，不将首次进入的所有既有条目误判为条件新增。

## 已确认用户决策与范围依据

- 已确认：每次实际页面导航都播放，包含子页和返回；当前页重复点击不重播，并保留原有滚动位置。用户在会话问题中选择了该方案，需求已写入 R6。
- 进一步源码盘点：`Details`/`BeginExpander` 只有 `Setting.Controls.cpp:1401-1424` 的 wrapper 和声明，没有实际产品页面调用。无需询问一个不存在的当前交互，不扩展未用 wrapper。
- R7 现有条件内容：`Setting.Pages.cpp:608`/`:832` 物理尺寸提示，`:857-876` 笔速橡皮高级项，`:1053` 动画速率，`:1074` 边缘光效选项；`Setting.cpp:3155` 新版本操作、`:4507` 调测帧率等。首次页面已有状态不算条件变更；全局动画关闭后应立即归位，关闭动画开关自身不能留下退出残帧。
- 已确认：页面入场中正文滚动/点击立即使条目到位，并正常执行本次操作；新的页面导航始终立即切换。实现应在真实 items 提交前消费取消，使绘制/命中同源，不清空真实输入、不额外重放鼠标事件；触发入场的导航点击不能误被当作新页的正文取消操作。用户已在第二项会话问题中选择此方案。
- 已确认：播放首页字标开屏时保留原有效果，不同步叠加或结束后追加逐块入场；未播放开屏、从其他页面返回首页时正常逐块入场。用户在第三项会话问题中选择了该方案。

时长/位移采用短促的初始设计参数，复用动画速率并由验收调整；不把每个数值都转为不必要的产品问答。

## 验证路线

规划阶段不构建、不运行 GUI。实施获批后：针对现有 Setting ImGui 无窗口回放增加可见/命中/首帧宽度/颜色终态/中途反向/scroll anchor 回归；SessionState resize serial 及无字体重建回归；共享管线和呈现计数/失败恢复；完整 `InkeysRepo.sln Debug|ARM64` 原生 MSBuild + 适用 Headless --no-window。MSBuild 同 invocation 规范 PATH；完整构建至少允许5分钟。真 Win7 resize 和真 Win10/11 blur/maximize 留为明确的人工验收，用户未授权不打开 GUI。
