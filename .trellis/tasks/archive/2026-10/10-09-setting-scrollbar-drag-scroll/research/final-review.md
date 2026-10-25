# 独立审查：设置侧栏滚动条与旧拖动复用

日期：2026-10-09。角色：已派发的 trellis-check。活动任务：10-09-setting-scrollbar-drag-scroll，父任务 setting-winui3-design-rebuild。

已读取完整 hook 保存内容、check.jsonl 引用规范与研究、prd/design/implement，并逐项核对当前产品/测试实际差异。实施代理负责产品与测试文件，审查期间保持只读并协调其修复；本代理只写本报告。未派发其他代理、未启动 GUI、未 commit/push。

## Findings (fixed)

- File: InkeysHeadlessTests/setting_design_tests.cpp
  - Issue: 首轮新增测试使用 ImVec2 的 ==（MSVC C2678），另有 beforeBusiness 局部重名 warning。
  - Fix: 实施代理改为 x/y 精确比较、局部变量改用明确名称。完整最终编译通过；没有修改产品或库以绕过测试错误。
- File: InkeysHeadlessTests/setting_design_tests.cpp
  - Issue: 首版尚缺真实滚动条位移、固定 inline child 拖动后的蓝条同步、上下限、实际溢出导航浮层及关闭屏障覆盖；AutoResize 测试额外用了产品没有的 AlwaysAutoResize。
  - Fix: 与主会话协调后，由实施代理补齐真实 Scroll/DrawData/ActiveId 断言及实际 renderer 浮层夹紧分支，移除测试专有 AlwaysAutoResize。最终保留实际 AutoResizeY、父子双 range、fixed child 唯一 owner、离窗捕获、释放、已关闭/未提交 Popup、长 Combo、按钮/slider/text input 与 context/clip 恢复覆盖。
- File: InkeysHeadlessTests/setting_design_tests.cpp
  - Issue: 新增测试将轨道按下后直接上移当作滑块反向拖动。库默认 ConfigScrollbarScrollByPage=true，ScrollbarEx 初次点击轨道设分页模式，18 个反向预期失败。
  - Fix: 独立依据 imgui.cpp:1644 与 imgui_widgets.cpp 的 ScrollbarEx 查明；实施代理保留默认 IO，分开验证轨道 Down/Up 的实际分页，再从真实 active-thumb DrawData 定位滑块并重新 Down/上移，验证 Scroll.y 反向且 scrollbar 保持 ActiveId。不复制 thumb 公式，不改产品/库/旧 helper。
- File: InkeysHeadlessTests/setting_design_tests.cpp；research/settingui2-drag-scroll.md；.trellis/spec/native-desktop/rendering-and-ui.md
  - Issue: 新测试从整个 ImGui 区域外 Down 后移入，错误期待旧 fallback 必然滚动；库 imgui.cpp:5405-5432 会令 MouseDownOwned=false 并抑制 HoveredWindow。1.875 倍率又因理论 reset=187.5、实际 Begin 取整为187，使 != 理论值产生假 PASS，另外两倍率才暴露失败。
  - Fix: 保留原输入序列，准确断言未归属按下、hover 抑制及实际 Scroll 基线不变；正向 nested child fallback 继续测试。主会话同步研究/规范，说明旧 fallback 依赖已有输入所有权、hover 和 ActiveId 前提。最终三倍率诊断均 owned=0、active=0、hovered=none、ScrollTarget=FLT_MAX。这是测试与描述修正，不是产品手势修改。

## Findings (not fixed)

没有未解决的本轮产品代码或测试问题。实际桌面触屏/驱动、鼠标观感、HWND/DX11 呈现及系统标题栏拖动属于本轮明确未授权的 GUI 验收边界，保留人工验收；不将 MouseSource 模拟或 CPU DrawData 通过表述为实机验收。

## 实际代码核对

- 独立用 git show 读取历史 7218d7191c5edd52f7b37a864ad035ce4625c65d 的 Setting.Base.cppm，并与最终 Controls.cpp 的 ScrollWhenDraggingOnVoid 函数逐行比对（仅所属缩进），完全一致。稳定 overlay ID、KeepAliveID、mouse_button 原参数、ButtonBehavior、held/ChildWindows fallback、SetScrollX/Y 均保留。
- ApplyDragScrolling 只在当前设置 context 全 UI 提交后执行一次；先确定唯一 owner，再调用可触发 FocusWindow 的旧函数。原 overlay ActiveId 捕获优先，移出后续接；否则沿真实 HoveredWindow 父链选最近活动可滚宿主，父子不重复处理 MouseDelta。
- eligibility 检查当帧 Active、Hidden/SkipItems、实际 ScrollMax.y，以及固定 NoScrollWithMouse/NoMouseInputs/tooltip；精确验证祖先 PopupId 开放状态，保留导航淡出、正文阻塞与 mouseReleaseBarrier。没有利用 WasActive 接纳上帧已隐藏 Popup。
- 仅局部保存/切换 CurrentWindow 与 End 后窗口 ClipRect，使用 InnerClipRect 恢复旧 helper 原调用裁剪，随后原样还原。真实回归确认字体/表格/DPI/Begin 栈/LastItemData 和 DrawData 数量保持；没有调用不存在的私有 SetCurrentWindow，也没有伪造 Begin/End。
- 继续使用 (0,-MouseDelta.y) 物理像素与下一 Begin 应用/取整/夹边界的 ScrollTarget。没有改 MouseDelta、控件 ActiveId、root flags、Win32 输入消息、配置格式或 vendor。生产 root flags 回归中原 MoveId 状态出现23帧/倍率，root每帧实际位置保持{0,0}；这不等于观察真实 HWND 移动。
- 中段仅解除 NoScrollbar，使用既有统一8 DIP轨道/2 DIP padding/透明轨道/灰色 thumb；真实 ScrollbarY 为 true 时，在 ImGui 已扣轨道后的 WorkRect/ContentRegionRect 再预留4 DIP。根项、父项/箭头及 inline child 背景/实际命中共用缩短边界，固定顶部/底部不改。显隐仍按库真实测量收敛，没有第二份内容高度公式。
- 规范与 spec-boundaries 注入摘录已同步内部签名、显隐/宽度、唯一宿主/Popup/输入所有权和真实回归要求，与最终实现一致；没有新增源码工程项、依赖、公开窗口 API 或外部配置。

## Verification

- Lint: PASS。独立执行 pwsh -NoProfile -File Scripts/i18n.ps1 check，en-US/zh-TW 各370/370；git -c diff.ignoreSubmodules=all diff --check 退出0；task.py validate 两份上下文均6条 PASS。本范围没有独立通用 C++ lint 命令，使用现有静态检查。
- TypeCheck / Build: PASS。独立读取 build-accepted-result.json、stdout及脚本，完整 InkeysRepo.sln Debug|ARM64 退出0，25.9152186秒。vswhere 动态定位 ARM64 原生 MSBuild，同 PowerShell PATH workaround，MSBUILDDISABLENODEREUSE=1，1800秒deadline。最终9条既有 HiMsg/hashlib++ warning，没有本轮新增 warning。
- Tests: PASS。独立读取 headless-accepted-result.json，完整 InkeysHeadlessTests.exe --no-window 正常用户权限退出0，122.8967172秒；stderr为0字节，stdout完整结束于 PASS animation correctness。没有源码修正后遗漏重编/重跑，也没有使用过程中的失败日志冒充最终通过。
- 新增真实侧栏滚动条/拖动回归317帧 × 3倍率；正文/nested/长Combo/input/context拖动回归208帧 × 3倍率。原层级导航684帧、Debug541帧、汉堡546帧各 × 3倍率，EDID40帧 × 6倍率继续通过。侧栏覆盖浅深主题、展开/紧凑/覆盖模式、动态显隐、实际轨道与滑块位移、按钮/箭头/子项间距、compact glyph/focus与固定区、Mouse/TouchScreen来源、蓝条同步。
- 正常最小520 DIP客户高度中四项导航 flyout实际 ScrollMax为0；另向同一生产 renderer提供受限 client bounds，实际夹紧后产生ScrollbarY/range，验证该分支的popup拖动/淡出/关闭。没有把该受限geometry测试描述成真实最小窗口溢出。
- Format: PASS。独立对5个最终源码/测试严格UTF-8解码，并用 git cat-file --filters HEAD 的实际 checkout 基线核对无BOM、纯CRLF；与实施前工作区字节测量一致。core.autocrlf=true 下 HEAD blob 的LF不能作为原工作区换行。主会话最终文档/元数据格式审计另记。
- 过程证据保留：build-first 的测试 C2678；headless-first 的受限 runner 5项 atomic writer + 2项 Config 临时文件写入失败（正常权限消除，没有修改对应业务模块）；headless-final 的20项测试预期失败；headless-input-test-fix 的2项外部输入预期失败。上述问题均在最终 accepted 构建/全量测试之前解决，最终验证使用修正后的代码和测试。

最终结论：独立审查 PASS，无未解决本轮代码问题。子任务和父任务状态、人工验收与提交由主会话按已授权范围收尾；本代理未提交或归档任务。
