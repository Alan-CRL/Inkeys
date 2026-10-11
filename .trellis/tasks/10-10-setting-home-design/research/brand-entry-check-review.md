# 第五轮独立审查：主页并行书写与显示归位

2026-10-11，trellis-check。审查依据为 check.jsonl 的各项合同、PRD/design/implement、最新 brand-entry-revision.md 与研究记录。已读取完整 hook 输出文件并定向补读超长 rendering-and-ui.md 的 Setting 生命周期、布局及首页末章；未派生代理。源码/测试冻结供主会话统一编译运行，任务保持 in_progress。

## Findings (fixed)

- Setting.Pages.cpp / setting_design_tests.cpp：入场取消原来仅检查新的 IsKeyPressed(false) 边沿。Show 前已按住 Tab/方向键时边沿已过去，ImGui 的 repeat 仍能在覆盖下导航。改成检查真实 IsKeyDown；实际 Tab/DownArrow/Space 保持到下一个 Show，再释放的回归断言不重播、不额外路由。
- Setting.Pages.cpp：无弹层的普通 Hide 复位无条件调用 ClosePopupToLevel(0,true)，而本仓 imgui.cpp 要求 remaining < OpenPopupStack.Size，会在 Debug 栈为空时断言。现在只在非空时关栈，不恢复旧弹层焦点；清除旧 inline child 的 focus bookkeeping 和拖动/移动活动状态。
- Setting.Pages.cpp / setting_design_tests.cpp：单纯 FocusWindow(nullptr) 虽能清旧焦点，但无缺帧 Hide→Show 的根窗不 Appearing，NavWindow 为空会令普通 Tab 直接返回。复位先从首层弹窗 RestoreNavWindow/ParentWindow 找到设置根窗，关栈后清根旧 NavLastIds/NavLastChildNavWindow，FocusWindow(root)+NavInitWindow(root,true) 建立新入口，避免指向已关 Popup。首次尚无根窗仍由正常 Appearing 路径初始化。回归覆盖实际无弹层及 focused Popup，稳定入口后用真实首个 Tab 聚焦内容 child、原生 Enter 进入，再以真实 Tab 找到 Home Star ID，不注入 NavActivateId。
- setting_home_warp_tests.cpp：原新增 WARP 虽保存书写截图，只在完整终态测 k 接缝，未直接验六笔当前前缀/活动端帽。追加从真实前景 DrawData 提取的检查：六筆都提交未完成前缀，无未来截面/提前终端帽；原索引段与有界七个活动半圆对应；去掉纯色覆盖后在透明清屏下用 1/1.875/4 倍读取六个活动 cap/shaft 内部。最终 verified 全套无窗口重跑给出浅深两主题各 331/1387/6854 内部样本、gaps=0；此前中断轮只有部分证据，最终退出结果见 Verification。

## Findings (not fixed)

- 未发现需产品决策的设计/API/模块边界问题。空间轨迹、尺寸 gate 与统一动画状态沿用最新已授权设计。
- Win7 SP1+KB2670838 实机体验、实际 HWND 完整生命周期/原生窗口最终观感仍须人工验证。本轮遵守不启动 GUI/Computer Use；FL11.0 WARP 离屏执行与旧 API 静态基线不能代表 Win7 实机。
- WARP 的完成提交计时含 CopyResource/Map 等待，ImGui hook 不覆盖已有 std::string/vector 或驱动分配，不作为 60 FPS 或整页零分配的证据。

## 静态核对

- 六组笔画局部 time/截面/索引 metadata 只在 Build 一次计算。原完整顶点/索引/彩虹及 k 闭合端点修复计算未改；完成 DrawWriting 直接调用原 Draw。
- .12s 等待+.53s 书写后，.65–.95s 使用完整网格、中心固定且 alpha=1；之后 fade 与三次 Bezier 空间路径同时推进，1.33s 精确静态。仅一份前景字标，正文/按钮命中不随动；未新增 Shader/纹理/设备/线程/timer。
- ShowSerial 单独触发，不因返回 Home、resize/主题/倍率重播；HideSerial 在线程同步状态中发布，在渲染线程 NewFrame 后、内容 Begin 前消费，外层真实主页滚动 child 在 Begin/旧惯性读取前归零。
- 尺寸 gate 使用实际外层 clip、完整 hero/brand/font/DPI，并用 17-float 布局键缓存安全包络。首次不足永久 Skipped，途中缩小取消，扩大不补播。首帧 AutoResize 尚不可见时不依赖其零 clip 判尺寸。
- 动画关闭/离页/输入取消收束；所有 skip-render 路径继续沿用原时钟重基准；无输入帧不全程 Disabled，渐退后维持正常 Fluent 配色。
- 实际三语言布局/链接/键盘/拖动/主页滚动旧回归保持；无新文案，i18n 源与生成头未改；无工程项/资源/平台配置新增需要模板同步。
- 主会话已同步 rendering-and-ui.md 首页末章；旧反光/内部返回播放合同已被本轮替代，未直接编辑主会话规划/spec 文件。

## Verification

- Lint/static：git diff --check PASS；独立 Scripts/i18n.ps1 check PASS（en-US/zh-TW 均 384/384）；10 个本轮 C++ 变更文件严格 UTF-8、BOM 与原文件一致、CRLF PASS。
- TypeCheck/native compile：独立只读核对最终 build-brand-entry-verified-result.json 与 .log：原生 ARM64 MSBuild 完整 InkeysRepo.sln Debug|ARM64 退出 0，45.8737871s，18 个未修改 HiMsg/hashlib 路径警告、0 error。首轮普通用户构建同样通过（122.184s），其 72 warnings 还包括未修改 IdtMain.cpp:2562，不能全部称 vendor；最初 restricted runner 的 C1902 为环境错误，未改工程/SDK/工具链绕过。
- Tests：独立只读核对 headless-brand-entry-verified-result.json / .stdout / .stderr：InkeysHeadlessTests.exe --no-window 退出 0，246.168837s，stderr 0 字节，末尾 PASS animation correctness。七倍率 HomeBrandDesign 均完成，每倍率 32 次实际播放、28 次尺寸跳过；1/1.875 两倍率 HomeMotion 包含实际键保持与无弹层/弹层快速重开首 Tab→Enter→Tab 到 Star 回归。WARP 完整终态 k 接缝及未完成六笔活动端帽浅深两主题三个 zoom 均 gaps=0，透明字标与下伸、四角视差、全客户区覆盖/标题栏排除等原像素断言通过。三语布局、原链接轻点/键盘/拖动取消、主页滚动与全套既有回归通过。上一轮为修复焦点人工中断的 ExitCode=-1 不算整套通过；这里以最终重跑结果为准。审查代理按协调未执行 MSBuild/EXE，未与共享输出并行构建。
- 只读审图：native entry-light-800/1080/1270 与 entry-dark-380 PNG 显示居中完整 Hold、渐退中的单份字标、归位与真实未完成六笔；静态帧不代替最终动态观感验证。
- 未 commit/push、未运行外链或远程变更、未 GUI/Computer Use；文档与任务仍开放。

## 最终 WARP 性能证据

同机、同设置、每组 100 个真实主页帧，CPU 与完成提交时间单位均为 μs。完成提交包含 CopyResource/Map 的 WARP readback 等待，不能解释为实际 Present 的 60 FPS 指标。

| 状态 | CPU median / p95 | 完成提交 median / p95 | 顶点 / 索引 / 批次 |
|---|---:|---:|---:|
| static 对照 | 2641.7 / 4143.7 | 30625.9 / 59744.1 | 14007 / 58608 / 10 |
| idle | 2487.3 / 3237.8 | 31180.6 / 43213.4 | 14007 / 58608 / 10 |
| moving | 3350.2 / 4497.2 | 31824.4 / 61232.1 | 14007 / 58608 / 10 |
| returning | 3240.8 / 4388.7 | 30781.5 / 43175.9 | 14007 / 58608 / 10 |
| writing | 2877.9 / 3846.0 | 37136.2 / 43496.2 | 14172 / 10572 / 11 |
| hold | 2665.0 / 3075.3 | 38559.3 / 44083.9 | 14011 / 58614 / 11 |
| dock | 2495.2 / 2923.5 | 37210.3 / 40531.1 | 14011 / 58614 / 11 |
| settle | 2519.9 / 2959.4 | 29486.5 / 32018.5 | 14007 / 58608 / 11 |

所有组热帧 ImGui allocator 新增为 0，brand cache-growth 为 0；static/idle/entry phase 的 cache-rebuilds 为 0，moving/returning 按姿态变化重建投影缓存。writing 的 165 顶点差额来自有界端帽，三角形提交反而只含当前六笔前缀；额外批次为前景覆盖/单份字标，不是 Shader/离屏 pass。std::string/vector 与驱动分配不属于 ImGui hook 统计，保留其证据边界。

日志：Inkeys/Cache/home-design-20261010/build-brand-entry-verified-result.json / .log，以及 headless-brand-entry-verified-result.json / .stdout / .stderr。最终检查源文件没有再修改；审查文档独立保留 UTF-8、无 BOM、CRLF。
