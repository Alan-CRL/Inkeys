# 第八轮开屏性能独立审查

2026-10-12，trellis-check 对当前实施作只读审查。Active task: `.trellis/tasks/10-10-setting-home-design`。本代理未启动 GUI、Computer Use、构建或无窗口测试；主会话正在统一构建验证，审查期间没有修改产品源码。

## Findings (fixed)

没有发现需要机械修补的源码问题。本轮没有源码修复。

## Findings (not fixed)

- 规范同步已由主会话完成：独立复读确认主页合同使用 240/1060/600/540/220ms（2.66s），且补入有限暖帧、值键失效、完全遮挡快路径及有界诊断。原待同步问题已解决。
- 真实性能/流畅性结论待主会话测量：源码可确认省略完全不透明阶段的页/导航 CPU 生成和 GPU 提交，但不能据此保证原生 60FPS。历史 baseline headless、当前同机 force-full/fast 控制组及实际 HWND/Present 的证据需要分别记录。Win7 实机和实际 DeviceRemoved 恢复仍未验收，不能从 Win11 ARM64 编译推出。

## Review evidence

- `HomeEntryTiming` 统一五阶段边界：WritingEnd=1.30s、DockStart=1.90s、SettleStart=2.44s、Total=2.66s。`ResolveHomeEntryPose` 写好后保持完整原网格、同一中心和 alpha=1 至1.90s，随后共用原三次 Bezier 空间曲线/透明淡退，最后精确归位。
- 产品 `Setting.cpp` 在 `CancelHomeEntryOnInput` 后、正文 Begin 前使用同一 `BeginHomeEntryFrame`。首开真实页及动态 atlas 先生成并提交；两次不同成功全帧且纹理已 OK 后才准备完成。managed 路径只有 Begin 推进时间，RenderHome 布局 helper 不二次推进。后端 HRESULT 失败或 Present 非 S_OK 的 Complete 回滚本帧时间并重新准备；失败尝试最多六次，继续沿既有 Retry/DeviceLost 路由。
- 快路径仅在准备完成且严格不透明阶段暂停正文及导航，根窗/标题栏/ImGui NewFrame/前景仍正常生成。当前帧 DrawData 过滤只保留实际 root/titlebar/foreground，root clip 限于标题栏，纹理请求队列不变；不缓存跨帧 DrawList、ImFontBaked、SRV 或 GPU texture。两次完整恢复暖帧保持 DockStart 不透明边界，随后第一半透明帧走正常主页及导航。
- 值键覆盖客户区、framebuffer、content bounds、gutter、DIP倍率、atlas/font 身份、FontRebuildSerial、设备 epoch、语言、主题与 atlas UniqueID/尺寸。核对 imgui_draw.cpp：实际 grow/repack 创建新的 ImTextureData/UniqueID（即使尺寸相同），可触发键失效；普通字号字形添加不缓存 UV，后端完整处理当前 Textures。键失效保留 ShowSerial/进度，强制完整布局重验。真实 Show 丢弃所有准备状态，Hide 继续在渲染线程消耗独立 serial 先归主页/顶部，尺寸不安全立即静态，不补播。
- 真鼠标/按键/滚轮等输入仍先取消，取消帧正文和导航屏障保留；新快路径不改变控件、外链/Tab/滚动动作或全局配置。独立代码审查没有找到帧循环、线程、timer、设备、现代专属 API 或 Present 模式变更；兼容链仍为原 WARP FL11.0/Win7 基线。
- `setting_home_warp_tests.cpp` optimized Frame 共用生产 Begin/Complete/Filter 和 RenderHome/Navigation/Overlay，不是自造状态替身；对照非 optimized Frame 只去掉准备/过滤，在相同字标时刻生成原完整主页。新增实际 WARP 像素断言包括首开、完全遮挡、第一次半透明、最终静态、Tab 取消/导航恢复；既有透明 AA/k 接合和 hold 完整 RGBA 比对保留。
- Force-full 与 fast benchmark 共用同一 renderer、100次测量、字体/主题/面积及采样进度；完成提交含 CopyResource/Map 等待，代码输出明确不冒充纯 GPU 时间。ImGui allocator 排除 std::string/vector/驱动计数，准备状态仅有有界值数据；没有宣称整个程序零分配。
- 新原生 profiler 仅 Setting.cpp 的 Debug 项定义宏、且运行时环境值精确为1才计时/记录。Release 无此结构和 chrono 调用；Debug 未启用时有固定数组成本但不记录。512样本满后丢弃计数，不扩容、不逐帧格式化或日志，不发额外渲染请求；结束/异常只输出一次阶段聚合。frameGap 是相邻回调开始，presentGap 是相邻 Present 返回，不冒充 display scanout 或 GPU timestamp。实际 Present 的等待未改。

## Verification

- Lint：`git -c diff.ignoreSubmodules=all diff --check` PASS。仓库本层未见独立 C++ lint/type-check 命令，实际类型/链接检查由完整 MSBuild 承担。
- TypeCheck / build：主会话第二轮完整 `InkeysRepo.sln Debug|ARM64` PASS（Exit0，24.2557948s）；本代理只读复核结果JSON。首次完整构建也成功（292.6694582s）。后续性能候选修改需另行完整验证，本结果不预先覆盖候选。
- Tests：主会话第二轮 `InkeysHeadlessTests.exe --no-window` PASS（Exit0，234.0755533s，stderr0，末行PASS animation correctness）；本代理复核结果JSON、末行和stderr大小。必要原生性能对照及进一步候选结果待主会话记录，本代理不重复执行。
- 格式：源码由实施者维护原 UTF8/BOM/CRLF，主会话最终审计；本记录使用 UTF8 无BOM/CRLF。

## 第二轮回归修正核对

第一轮15失败由主会话和实施者定位，不能删去这次红色历史。独立复读第二轮修正：

- `CheckPreparedKeyboard` 从浅/深逐像素序列中拆出，放在像素验收之后独立执行。仍先走16个真实快路径帧、再真实Tab down/up取消并断言不穿透动作，再真实Tab找回非零NavId；没有赋值NavId/NavActivateId或强制FocusWindow。结束用真实鼠标位移退出导航视觉。这样没有放宽输入断言，修正的是夹具对后续WARP截图的焦点环污染。
- `StableBaseline` 增加两个真实非主页 ImGui+DX11提交帧。独立核对 `imfluent.cpp` 的 `AnimFrameContiguous`/`AnimateColorU32`/`LerpColorU32`：不连续调用直接返回准确主题目标色；连续调用逐帧存储已量化U32，接近目标时仍可停留在不同于目标的整数色。快路径的真实child缺席会触发既有非连续语义，所以按正常页面重现生命周期建立静态基线合理。这里修正的是两次不同生命周期产生的主题基线，不是容忍差异：完全遮挡、第一半透明与最终静态的 `fast==full`/`fast==baseline` 精确RGBA断言仍保留。
- 原两批次上限不符合当前生产根窗边框/内caption独立clip。新断言严格检查root两批、foreground一批且总计等于该三批；实际list限定为root/foreground、texture有效、root止于标题栏、foreground始于客户内容区。没有以任意高批次数量或跳过验证掩盖提交增长。
- 第一輪回归失败不能作为产品像素已通过的证据；第二轮通过的范围为修正后的夹具。此修正没有改生产控件主题或全局样式。本代理未发现其掩盖新产品缺陷的依据。

正在候选验证的“Solid根窗背景等同Clear时省去重复填充”尚未纳入此时通过结果；须冻结后审查精确门禁、透明/Mica负例、真实清屏RGBA和同机bench。

## Solid同色根背景候选（冻结后只读审查）

- 未找到阻塞实现问题。`CanOmitHomeEntryRootBackground` 只接受Home且Playing、Solid backdrop、style alpha精确1、背景alpha精确1和有限0..1 RGB；透明/Mica、部分alpha、静态/取消完成均保留原根窗绘制。
- `Setting.cpp` 在NewFrame后取得backdrop/WindowBg的一份值快照，门禁和真实Clear都用同份数据；核对 `PushFluentStyle` 只PushFont，不换WindowBg。caption主题变更只在下一帧ApplySettingTheme，因而本帧不混合两套底色。
- 仅 `SetNextWindowBgAlpha(0)` 抑制重复根填充，原Begin flags/边框/标题栏和输入逻辑不变，没有使用会删除边框的NoBackground。普通cancel/disable的当帧若门禁沿前帧Playing已省填充，同色不透明Clear仍为正确底色；下一帧静态恢复原根绘制。
- 离屏 `Submit(... solidClear=true)` 使用与生产相同WindowBg乘alpha清屏；`CheckSolidRootBackground` 共用生产helper，八阶段逐RGBA比较suppressed/full，包括等待/书写/hold/dock/settle及终点，另外检查title/border像素。三种真实回退（透明backdrop、部分style alpha、部分WindowBg alpha）及static负例保留。
- 两组Solid benchmark都使用同一optimized Frame/renderer、相同时刻/面积/clear，仅toggle suppressRoot；完整暖帧后各100测，记录CPU、完成提交copy/Map、vertices/indices/batches。该对照能验证候选成本，但仍须实际输出证明收益，不提前认定保持候选。
- 候选没有增加缓存分配、线程、timer、shader、GPU纹理或Win7新接口；Debug profiler新增布尔样本与总数，不新增逐帧格式化。
- 本阶段本代理不改源码、不运行构建/测试/GUI。最终构建、像素结果及性能收益由主会话补充；第二轮PASS不自动覆盖本候选。

## 最终无窗口验证复核

主会话最终候选保留版 `build-entry-performance-solid-result.json` Exit0、79.7518292s；完整 `headless-entry-performance-solid-result.json` Exit0、273.9630156s。本代理只读原始结果JSON、stdout末行 `PASS animation correctness` 和stderr长度0，确认最终build/tests PASS，第二轮与首轮历史保留。

原始 `headless-entry-performance-solid.stdout` 的Solid对照：浅/深各八阶段、title/border各306112像素、三回退通过。解析 `entry-performance-20261011/warp-final-metrics.json` 与stdout一致，1000×1100 WARP FL11.0、预热后各100帧测量：

| 同机同Frame对照 | CPU median（µs） | 提交完成+copy/Map median（µs） | 解读 |
| --- | --- | --- | --- |
| writing full → fast | 3058 → 1463.5 | 39144.1 → 12220.6 | CPU减52.14%，完成成本减68.78% |
| hold full → fast | 2847.6 → 1299 | 39524.5 → 14737.7 | CPU减54.38%，完成成本减62.71% |
| Solid dock full-root → suppressed | 2621.3 → 2607.5 | 41692 → 31799.8 | 完成成本减23.73%；p95 60122.9→41749.9µs |
| Solid settle full-root → suppressed | 2549.9 → 2395.5 | 30542.7 → 22622.4 | 完成成本减25.93%；p95 35983.2→25955.7µs |

Solid局部候选writing仅小幅改善，hold完成median约慢0.33%近噪声且CPU更高，不能说所有阶段都加速。保留候选依据是已证明RGBA不变且dock/settle明显下降。准备值状态160字节，热测ImGui分配、字标缓存增长/重建均0；此统计不覆盖全程序std/driver分配。以上不是纯GPU时间、也不是实际窗口或Win7 FPS。

必要原生cold/三个warm及小窗skip仍由主会话执行；本代理未启动窗口。本记录将等待原生完成后补最终证据，不因离屏PASS宣称实机表现完成。

## 最终原生必要测试复核（主会话运行，本代理只读）

独立复读 `Build/SettingHomeEntryPerformance20261012/current-solid-final-native-metrics.json`、memory/exit JSON及原始 `app/log/idt1791763440731.log`，实际隔离PID24140、同目录app/Inkeys.exe，SHA256为7203F814ACA9786318506B96D3AE76414CBE08E926027C54859CB735AD0DE395。

- DPI192、客户1920×1400（960×700 DIP）、简中浅色；Show1冷与Show2/3/4热共四次finished，fast106/105/105/103，dropped全部0。实际wall2.821/2.748/2.755/2.768s，2.66s为逻辑动画，有限真实准备/边界暖帧及共享Present另计。
- 原始回调间隔median17.7048–18.0849ms、p9524.4313–28.7855ms，书写CPU median0.6319–0.7519ms，冷prepare最大CPU38.3693ms。冷准备峰值被保留且不计入书写时间，不把backend调用时间当GPU完成，不把Present-return/callback的倒数当实际显示FPS。
- Show5客户1440×1040（720×520 DIP）skipped；扩大恢复仍静态，没有补播。日志零error及正常“已结束所有线程并关闭程序”，exit JSON HasExited=true。其ExitCode=null，所以只确认正常关闭记录与进程退出，不声称native exitcode0。
- 所有样本solid-root-suppressed=0，实际是Mica/透明回退路径；这证明回退照常呈现，不证明Solid候选在这个原生窗口提速。Solid的保留证据只来自前述真实清屏WARP对照，不混淆两类结果。
- 整进程PrivateBytes冷685506560、三热688799744/687874048/688603136、Threads115，Handles1143→1142。短轮次无单调private增长但不能据此证明长期无泄漏，整体数值还包括其他客户端/字体/画布/库，不能归因于入场状态。局部新增state160字节、Debug采样32768字节及有界热测才是新增资源预算证据。
- 测试说明原配置校验未变、截图仅在动画结束后，未使用Computer Use；本代理没有操作窗口。ETW因OS权限被拒，未改系统权限/Tracing；旧二进制warning聚合不具同类原生median/p95，不能作精确 native before/after百分比。

## 审查完成状态

当前实际产品diff没有发现待自修源码缺陷；首轮红色夹具历史、第二轮与最终通过分别保留。最终完整ARM64 Build、完整headless、真实WARP精确像素与有界性能对照PASS；原生必要四次入场/小窗跳过/恢复回退PASS（结果范围如上）。最终规范同步与格式/i18n审计由主会话统一收束；Win7实机、原生人工观感、实际DeviceRemoved恢复和长时间性能/资源测试仍分开待验，不把它们写成完成。
