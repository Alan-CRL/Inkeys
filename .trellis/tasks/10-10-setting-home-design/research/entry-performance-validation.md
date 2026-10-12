# 第八轮：开屏时序、WARP与原生性能验收

2026-10-12，主会话完成本轮实现与验证。沿用fluent/当前Trellis子任务，HEAD基线f77bd6f3013b58a09ae887dfd92cb1d7ed713f4a。用户预批准实施及必要原生性能测试；所有窗口操作由限定PID/可执行路径的CLI Win32测试脚本完成，没有Computer Use，没有新的commit/push。父子任务保留开放，后续人工观感、Win7实机及真实设备失效恢复独立验收。

## 时序与最终实现

默认速率1，统一HomeEntryTiming：wait240ms、六筆同时write1060ms、完整字标hold600ms、背景同时渐退/原Bezier空间曲线dock540ms、落定220ms，共2.66s逻辑时间，恰为此前1.33s的两倍。1.90s前覆盖alpha=1；保留原采样精度、透明AA、定向活动圆头与k接合。

首次真实主页和导航动态字形在遮罩下完成两个不同成功backend+Present全帧；准备时间不消耗书写。值键稳定的不透明阶段暂停底层页/导航CPU生成，并仅向生产DX11后端提交root标题栏clip、titlebar与foreground；Textures仍正常处理。首半透明前先恢复两个成功完整child布局帧，避免缺席后的AutoResize裁切，随后正常实时绘制整页。

准备状态只保存值键、计数与已消费Show，不保存跨帧DrawList/SRV/ImFontBaked；键涵盖大小/FramebufferScale/内容bounds/gutter/DIP、字体与atlas身份、fontSerial、UniqueID/尺寸、语言、主题与device epoch。失败/遮挡回滚本帧时间、重新准备，最多六次预算后收束，保持原Retry/DeviceLost资源事务。实际Show播放，Hide回Home顶端；导航/主题/resize/滚动不制造重播；窗口不够直接静态。

局部Solid优化保留：仅活动主页入场、Solid且style/background alpha精确1、规范RGB，SetNextWindowBgAlpha(0)省同色clear之后的根背景重复填充。没有NoBackground，外框与原flags保留；根Begin和Clear共享本帧快照。Mica/Acrylic/部分透明/静态回退。归位/落定有显著WARP收益，书写小收益、hold接近噪声，未宣称所有阶段都改善。

## 构建与完整回归

- 动态定位ARM64原生MSBuild：C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe；完整InkeysRepo.sln Debug|ARM64，同PowerShell调用规范PATH/Path并禁用node reuse，允许1800s。最终Exit0，79.7518292s，56个既有HiMsg/hashlib等warning，0errors；没有为warning修改无关代码。
- 最终InkeysHeadlessTests.exe --no-window：Exit0，273.9630156s，stderr0，末行PASS animation correctness。
- 本轮最终i18n check384/384 PASS（entry-performance-20261011/i18n-check-final.log）；本轮没有可见文案变更，无须sync生成物。Trellis implement/check上下文各14项validate PASS，规范文件超32768字节的既有注入警告通过定向读取处理，未拆改全局规范架构。
- 最终Inkeys.exe SHA256：7203F814ACA9786318506B96D3AE76414CBE08E926027C54859CB735AD0DE395。
- 最终Headless SHA256：193BFC7CF822F4C182DA527078BDD412A7A9E6992B91EC4B944FE6FC400068A3。
- 真实三语言字体/文案、三窗口和额外窄宽、浅深、展开/收起及七倍率已有回归保留；21类失效键、速率/开关、失败/重复完成、暖帧与透明恢复有新增断言。真实Tab取消与后续导航恢复保留，不伪造导航ID；既有tap/Enter/Space/拖动取消、链接和滚动均由整套回归验证。
- WARP明确FL11.0，使用生产imgui_impl_dx11和CSO。浅深各8阶段full/suppressed全图精确RGBA；另各306112标题/外框像素精确比对，三个真实透明/部分alpha回退。首半透明/终态精确RGBA与准备快路径对照通过。k接合与活动圆头1/1.875/4倍无空缺；Hold完整静止、原曲线路径、单份字标和透明AA通过。

## 真实WARP同帧对照

Windows11 ARM64、1000×1100离屏面积、同Frame/同字标时刻/字体/renderer/FL11.0。每组20帧预热+100帧采样。CPU不含提交；完成提交含生产后端+CopyResource/Map等待，不能当成纯GPU时间或原生显示FPS。以下所有单位ms，数值为median / p95。

| 模式 | CPU | 完成提交+copy | vtx / idx / batch |
| --- | ---: | ---: | ---: |
| writing | 3.058 / 4.726 | 39.144 / 46.418 | 14236 / 10848 / 11 |
| writing-fast | 1.464 / 2.545 | 12.221 / 16.348 | 12204 / 5790 / 3 |
| hold | 2.848 / 3.400 | 39.525 / 43.002 | 14075 / 58890 / 11 |
| hold-fast | 1.299 / 1.794 | 14.738 / 18.510 | 12043 / 53832 / 3 |
| dock | 2.970 / 3.653 | 39.294 / 43.016 | 14075 / 58890 / 11 |
| dock-prepared | 2.741 / 3.841 | 39.256 / 45.458 | 14011 / 58614 / 11 |
| settle | 2.685 / 3.989 | 29.801 / 33.850 | 14071 / 58884 / 11 |
| settle-prepared | 2.556 / 3.411 | 30.223 / 33.846 | 14007 / 58608 / 11 |

writing全页→快路径CPU median下降52.14%、完成提交下降68.78%；hold分别54.38%和62.71%。不透明提交从11批降为3批；渐退必须恢复实际整页，所以没有用此数据声称透明阶段也获得同样收益。

| Solid根填充（每对只改suppressRoot） | CPU | 完成提交+copy | vtx / idx / batch |
| --- | ---: | ---: | ---: |
| solid-writing-full-root | 1.376 / 2.527 | 12.685 / 20.844 | 12204 / 5790 / 3 |
| solid-writing-suppressed-root | 1.368 / 2.344 | 11.717 / 21.049 | 12172 / 5652 / 3 |
| solid-hold-full-root | 1.208 / 1.637 | 13.290 / 16.461 | 12043 / 53832 / 3 |
| solid-hold-suppressed-root | 1.361 / 1.567 | 13.334 / 15.606 | 12011 / 53694 / 3 |
| solid-dock-full-root | 2.621 / 4.054 | 41.692 / 60.123 | 14011 / 58614 / 11 |
| solid-dock-suppressed-root | 2.607 / 3.494 | 31.800 / 41.750 | 13979 / 58476 / 11 |
| solid-settle-full-root | 2.550 / 3.145 | 30.543 / 35.983 | 14007 / 58608 / 11 |
| solid-settle-suppressed-root | 2.396 / 3.224 | 22.622 / 25.956 | 13975 / 58470 / 11 |

Solid完成提交median：dock减少23.73%、settle减少25.93%、writing减少7.63%、hold增加0.33%近噪声；hold CPU也较慢12.60%，writing完成提交p95略慢。依据原渐退瓶颈的dock/settle收益及精确等价保留局部候选，记录全部配对而非只选择有利项。

## 原生窗口必要测试

独立app和opt配置副本，SC、浅色、倍率1、DPI192，client1920×1400（960×700DIP）。一冷首开+三热重开采用工具栏业务入口真实Show/WM_CLOSE Hide；不以ShowWindow假造业务serial。截图只在完成后，不逐帧录屏。额外720×520DIP小窗真实Show直接静态，随后放大不补播，恢复尺寸后从正常关闭软件入口退出。

最终PID24140，日志idt1791763440731.log。四次finished、第五次skipped、dropped0、error/critical0；正常关闭末句且PID消失、窗口已清理。PowerShell重新获取的Process.ExitCode返回null，不能把它写成native Exit0；正常退出结论来自业务退出动作/关闭日志/进程消失。

| Show | 帧数 / fast | 总wall ms | callback gap median / p95 ms | Present-return gap median / p95 ms | writing CPU median / p95 ms | dock Present median / p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 (cold) | 147 / 106 | 2820.930 | 17.762 / 26.083 | 17.811 / 26.327 | 0.690 / 0.869 | 21.822 / 25.680 |
| 2 (warm) | 146 / 105 | 2747.780 | 17.789 / 24.431 | 17.830 / 24.660 | 0.632 / 0.870 | 20.313 / 23.416 |
| 3 (warm) | 146 / 105 | 2755.222 | 17.705 / 25.080 | 17.719 / 24.997 | 0.689 / 0.915 | 20.648 / 23.963 |
| 4 (warm) | 140 / 103 | 2768.310 | 18.085 / 28.785 | 18.535 / 28.799 | 0.752 / 0.968 | 24.492 / 27.653 |

当前系统native确实走Mica：所有Show的solid-root-suppressed计数0。因此最终native是Mica回退正常及不透明快路径的测量证据，不能把同机候选前后的噪声解释为Solid native提升。callback中位约17.7–18.1ms（节奏约55–56Hz），p95约24.4–28.8ms；Present-return和callback都不是屏幕scanout帧时间，未证明稳定60FPS。冷prepare CPU最大38.37ms在不透明遮罩下完成，未消耗书写；实际wall约2.75–2.82s包括有限准备与调度。

此前已验证旧native二进制SHA30E494E218A54FEDBE14E496DE01A6897A43764BA22CC78653AAACBFD9DD80A8已同配置一冷三热正常运行，但只含warning触发UI3Diag汇总，无可比native median/p95，不能编造旧→新FPS百分比。另旧headless产物Exit0耗时2280.64s且无HomeWARP输出，其源码一致性未证，不用作WARP基线。离屏收益均来自上述新版同Frame控制组。

OS拒绝了短ETW provider探测（AccessDenied -2147024891），未创建trace/提升权限/修改配置；采用显式Debug有界profiler。首次native采集的系统Python无PIL是测试环境问题，后续使用既有bundled Python完成，不安装依赖。

## 内存、热路径和兼容边界

新增Preparation实测160字节；Debug条件编译且环境=1才采样的512帧数组32768字节，Release不编译诊断。没有每帧计时/日志格式化（未启用时）、没有无限缓存、整套CJK预烘焙、专用Shader/离屏texture/新device/thread/timer或额外调度请求。原字标Mesh与采样未变。

所有writing/hold/dock-prepared/settle-prepared和Solid对照热采样ImGui alloc=0、brand cache growth=0、rebuild=0；该计数不包含既有std::string/vector及驱动分配。原静态/鼠标移动/回正对照同样记录；运动姿态重算是必要更新，不把其rebuild数当几何重采样或内存增长。

原生完成后整进程快照（含共享WARP/字体/其他应用组件，不能归因开屏，更不能证明长期无泄漏）：

| 阶段 | working set MiB | private MiB | handles | threads |
| --- | ---: | ---: | ---: | ---: |
| cold-complete | 673.94 | 653.75 | 1143 | 115 |
| warm-1-complete | 675.73 | 656.89 | 1142 | 115 |
| warm-2-complete | 676.24 | 656.01 | 1142 | 115 |
| warm-3-complete | 676.99 | 656.70 | 1142 | 115 |

这些短时快照没有递增的thread/handle趋势，但约650MiB private属于整个既有应用；没有完整旧版内存基线，本轮不宣称整体内存减少。新增局部状态与诊断预算及热路径无扩容是可证明的内存边界，未擅自优化共享架构。

兼容审查继续沿既有D3D11 WARP FL11.0/ImDrawList/生产CSO；没有FL11.1专属或现代flip/API依赖、没有改变Present模式。Microsoft对Win7平台更新的说明仍为基线：https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7 。本机Win11实际FL11.0离屏证据不能当Win7 SP1+KB2670838实机证据；Win7和真实DeviceRemoved恢复未运行。原纹理事务/真实E_INVALIDARG与模拟removed/reset回归保留；最初设备移除根因仍unknown。

原Build/ARM64/Debug/opt/deploy.json SHA256仍13810DFE66395A9E1F83C73DDB8145164AD72B0E7C5FDEB4AB588593DF0DCD4E；独立测试未改用户配置。最终HEAD/index、UTF8/BOM/CRLF、git diff --check由format-audit.json记录。

## 失败记录与审查

首次本轮build成功，首次headless Exit1（273.9984s）出现15个HomeWARP失败，未冒充通过。定位为真实Tab夹具在逐像素序列前污染导航视觉，以及连续ImFluent U32颜色插值基线与真实页面缺席后target不一致；另root/caption实际两命令+foreground一命令为3批，旧两批假设错误。仅修正夹具生命周期/隔离和明确命令所有权，没有放宽精确RGBA，没有改产品主题或字体精度。第二轮Exit0（234.0755533s）后加入Solid候选，最终上述第三轮全套Exit0。

trellis-implement与trellis-check已独立实施/审查，见entry-performance-implementation.md和research/entry-performance-check-review.md；没有发现未解决的本轮源码阻塞。规范已同步时序、准备接口、Solid精确门禁及真实计数。当前实施轮完成；任务开放的剩余项为人工主观观感、Win7实机及真实设备移除恢复。

原始证据：Inkeys/Cache/home-design-20261010/build-entry-performance-solid-*、headless-entry-performance-solid-*、entry-performance-20261011/warp-final-metrics.json及format-audit.json；Build/SettingHomeEntryPerformance20261012/current-solid-final-native-metrics.json、current-solid-final-memory.json、current-solid-final-exit.json、native-actions.jsonl和结束截图。

## 2026-10-12 后续提交授权

用户已明确授权为本轮改动提交commit，并要求保留任务，稍后继续性能优化和人工测试。此授权覆盖上文提交前的无新增commit限制；当前任务保持in_progress，不finish/archive，不push。本次提交使用已通过的完整构建、无窗口/WARP及必要原生验收结果，没有新增产品源码变更。
