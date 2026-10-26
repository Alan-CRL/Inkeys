# 实施与验证记录：设置滚动条与旧拖动复用

日期：2026-10-09。用户已批准规划并授权实施。当前 fluent 工作区最小修改；父任务及本子任务不自动归档，不 commit/push，不启动 GUI 或 Computer Use。

## 产品修改

- Setting.Shell.cpp：中段 ##settings-nav-list 解除 NoScrollbar，依赖真实内容测量自动显隐，未强制竖直条或启用横条。Begin 后仅在真实 ScrollbarY 下追加 4 DIP 内容间距；ImGui 本身已扣除 8 DIP 轨道，未重复扣宽。普通根项、通用父项/箭头、内联 child 的真实按钮背景和命中一起使用缩短后的右边界；固定顶部/底部不改。
- Setting.Controls.cpp：ScrollWhenDraggingOnVoid 函数正文来自历史 7218d7191c5edd52f7b37a864ad035ce4625c65d 的 Setting.Base.cppm，与 D:/Project/Inkeys/Repo/Inkeys-draw 本地旧源一致；保留原 ID、KeepAlive、直接 mouse_button 参数、held/ChildWindows fallback、SetScrollX/Y 和 (0,-MouseDelta.y) 调用。没有阈值、惯性、新取消点击、Win32/触摸转换或 vendor 改动。
- Design::ApplyDragScrolling 是新增调用部署，算法保持原样。仅运行设置 session 当前实际 Active/可输入/可滚窗口；排除固定 NoScrollWithMouse、NoMouseInputs、tooltip、隐藏/未提交和已关闭 Popup。用具体 PopupId 的开放状态及本帧导航宿主 ID 判断浮层淡出/正文输入/释放屏障。
- 先按原 overlay ActiveId 捕获保持越界，再沿实际 HoveredWindow 父链选最近的唯一 owner；不遍历期间调用 FocusWindow，不双重消费父子位移。
- 全 UI 提交后临时保存 CurrentWindow、目标 ClipRect，恢复 End 已弹出的 InnerClipRect 环境调用旧 helper，再精确还原。未伪造 Begin/End、字体/表格/DPI 状态或新增 DrawList 命令。SetScrollTarget 仍下一 Begin 消费/取整/夹边界，MouseDelta 不重复乘 DPI。
- Setting.Design.h：仅增加内部部署声明和本帧 navigationScrollWindowId/flyoutWindowId（不保存跨 session UI 指针）；Setting.cpp 在根 End 后、EndFrame/Render 前每帧调用一次。

## 真实无窗口覆盖与已确认边界

现有 setting_design_tests.cpp 直接编译生产 Controls/Shell，新增真实 ImGui 输入、控件和 DrawData 回归，不复制拖动或 thumb 计算公式。

- 生产侧栏三倍率 1/1.5/1.875，浅/深主题，展开/紧凑/覆盖导航：全部中段 item/arrow/child 实际 hit rectangle 与 scrollbar track/gap 分离；实际共享主题 thumb 绘制和 8 DIP track；动态展开/折叠/resize 自动显隐及宽度收敛；固定区、末项可达及现有所有导航/Debug/动画回归保留。
- 默认 ConfigScrollbarScrollByPage=true：轨道 Down/Up 产生真实分页 scroll；从实际 active-thumb DrawData 定位可点击中点重新 Down，再上移验证真实 Scroll.y 反向及 scrollbar ActiveId。条/间距操作不路由、不开合通用组、不执行业务。
- Mouse/TouchScreen 来源已进入 ImGui 后的上下/反向/释放、overlay 越界捕获；单宿主 outer、AutoResizeY body、父子双 range、固定 child fallback；真实按钮/slider/text input/40项 Combo 的 ActiveId 与操作保持。上/下限、底部快速反向回顶只走真实下一 Begin 夹边界。
- 实际 selected inline child padding 拖动只滚 nav-list，固定 child Scroll.y=0，蓝条相对真实 selected item 同步。
- 正常最小客户端 520 DIP 的四项导航 flyout 总高 172 DIP，小于客户区 488 DIP，实际 ScrollMax.y=0；未谎称普通最小窗会溢出。测试另向同一生产 renderer 提供更短 client.content.bottom=180、保留可见 pane anchor，由真实 flyout clamp 产生 ScrollbarY/range，验证 padding 拖动只滚 popup、CloseFlyout 淡出不提交 scroll target及最终 popup 关闭。
- Combo padding 测试使用生产内部 popup 的非条目位置，未改 Selectable。未提交本帧的旧 Popup、已提交却 ClosePopupToLevel 关闭的 Popup 均不能消费新位移。
- viewport 外 Down 属于 ImGui 未归属按下：MouseDownOwned=false，按住移入后 HoveredWindow=null，原 helper 也不会滚；保留该负向回归。不能从旧 ChildWindows fallback 推导任意外部未归属手势。倍率 1.875 的 reset=187.5 经 Begin 取整为187，断言使用真实 scroll 基线，避免直接 != 理论值产生假 PASS。
- 使用真实产品 root flags（没有测试独有 NoMove）；原 fallback 引发内部 MovingWindow 的帧仍保持 root 精确 {0,0}，拖动、释放及控件正常，无需改 root 或 Win32 caption hit test。这个内部状态不等于验证了真实 HWND 移动。
- 每帧核对 CurrentWindow、StackSizesInBeginForCurrentWindow、CurrentTable、Font/FontSize、DPI/字体密度、CurrentWindowStack 深度、LastItemData、End 后 ClipRect 和 DrawList 数量精确恢复。

## 验证结果

日志均在 Inkeys/Cache/scrollbar-drag-scroll-20261009，未覆盖既有任务日志。

| 验证 | 实际结果/日志 |
| --- | --- |
| 完整 Solution Debug ARM64 | build-accepted-result.json：ExitCode=0，25.9152186秒；动态 vswhere ARM64 native MSBuild、同 PowerShell PATH workaround、/m:1 /p:CL_MPCount=1、1800秒deadline。9条既有 HiMsg/hashlib++ warning，无新增本轮warning。 |
| 完整 --no-window | headless-accepted-result.json：ExitCode=0，122.8967172秒，stderr 0字节，结尾 PASS animation correctness；正常用户权限、隐藏非交互进程、1800秒deadline。新增生产 NavScrollbar/Drag DrawData 各倍率317帧与208帧，三个倍率均PASS；rootMovingFrames 各23帧，实际root位置/原fallback/单owner全通过。 |
| i18n | i18n-check.log：en-US/zh-TW 均 370/370 PASS。 |
| 编码/换行 | encoding-audit.json：5个修改源文件严格 UTF8 无BOM/CRLF，无 bare LF；实施前工作区计数：Design.h457、Controls.cpp748、Shell.cpp567、Setting.cpp4273、tests2602。Git core.autocrlf 下 HEAD blob 的LF不是工作区原格式，未拿blob换行作为原基线。 |
| git diff --check | 退出0。 |
| 原算法核对 | legacy-helper-audit.json 和独立 checker 原正文逐行核对（仅所属缩进）PASS；最终产品 helper 此后未修改。 |

## 过程中的失败及修正

- build-first：ExitCode=1，66.5293414秒。首个意义错误是测试 ImVec2 没有 == 运算符，改为各 x/y 精确比较；另消除新测试局部 beforeBusiness 重名 warning。只修测试，产品不变。
- headless-first：受限 runner ExitCode=1，118.9813096秒，5项 atomic writer 及2项真实 Config 模块临时文件写入失败；SettingDesign 新旧回归无失败。正常用户运行消除了这些环境失败，未改配置/测试写入模块绕过。
- headless-final：正常用户 ExitCode=1，120.8014153秒，18项错误轨道→thumb反向预期、2项错误外部鼠标归属预期。读真实库定位默认轨道为分页，修成 release/regrab 实际 thumb；后者不是旧 helper 允许的鼠标归属路径。
- headless-input-test-fix：正常用户 ExitCode=1，124.4167454秒，thumb/其他新增覆盖已PASS，只余2项外部未归属正向预期；诊断三倍率均 ActiveId=0、HoveredWindow=none、ScrollTarget=FLT_MAX。最终保留原序列为准确负向断言，并使用实际取整基线。
- 所有过程失败日志保留；不能把旧 failed 日志叫作最终通过。

## 人工边界

未运行 GUI、Computer Use、真实触屏/驱动、HWND/DX11 呈现或标题栏系统拖动。MouseSource 模拟只证明已有 ImGui 输入路径的行为；真实触摸和鼠标观感、实际屏幕呈现留待人工验收。无 commit/push；独立 trellis-check 最终报告由主会话协调。
