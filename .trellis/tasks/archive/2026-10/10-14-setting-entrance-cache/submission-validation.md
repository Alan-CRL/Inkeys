# 第二轮重复提交优化与组件离屏验收（2026-10-14）

## 最终结果与范围

用户直接授权后延续当前Trellis任务。产品继续所有UI共享WARP D3D11设备、Draw独立hardware的架构；没有新增device/thread/shader/timer或修改全局帧率策略。保留首轮无字标页面截图、渐退/归位复用与终态恢复。

本轮在Clear之前精确比较最终DrawData与最近一次backend成功且真实Present==S_OK的快照；相同且无事务时跳过cache copy/backend/Present，CPU Frame和Continue继续推进输入、延迟tooltip/menu、caret、轮询文本及动画时钟。准备/捕获/恢复永远是真实提交，不虚增暖帧成功。callback、纹理create/update/destroy、输入、资源/尺寸/font/主题/epoch/Hide、失败/遮挡都强制或撤销；上一帧Map失败允许下一次真正Render重试，避免GetLastResult旧错误永久阻塞。快照为单帧值字节，不靠hash、不留裸DrawList或ImFontBaked；固定1MiB CPU上限，超预算/分配失败回退提交。测试hold有效348692字节、静态397824字节，热相同帧不复制/扩容，无新增GPU内存。

缓存只在蒙版opacity与style alpha精确1、客户四边映射为整数像素时省CopySubresourceRegion；截图仍保留到渐退。部分alpha/非整边界走原copy。每个实际提交仍正常ClearRenderTargetView。

省Clear候选已撤回产品，仅保留测试局部实验作为否决证据。此前完整候选suite exit0/238.3326s，其三阶段median收益+0.69%/-0.80%/+0.81%，两阶段p95变差；最终配对结果见下。不同阶段及重复执行无稳定收益，不增加生产覆盖门禁/额外背景批次。Setting.Pages.h/.cpp与本轮开始baseline逐字节一致。

## 测量方法与边界

Windows 11 ARM64 / Snapdragon X1E80100 / Qualcomm Adreno X1-85，同第一轮机器；Debug|ARM64，1000×1100，scale=1，浅色，FL11.0。固定组20预热+100样本。性能帧没有Sleep/Present/共享调度器/读回，通过有界D3D11_QUERY_EVENT+Flush/GetData逐帧确认完成。CPU/backend/wait/total为墙钟，total逐帧测量，不能把分项median相加或把EVENT完成等待解释成纯GPU busy。像素回归另用staging Copy/Map。实际强制绘制组FPS为1e6/total-us median；skip仅CPU回调成本，没有显示/GPU FPS。

新21组全部生产WARP；原39组保留WARP与独立hardware诊断。组件消融固定同一时刻/字体/面积/缓存/真实网格，full/no-ink/no-cover/copy-title均正常Clear与强制copy保证目标完整初始化；CPU仍生成后裁剪，不能据此称生产删除字标会增加CPU耗时。只含真实Home/Navigation/Overlay和简化标题，未包括完整Setting协程业务图片、原生caption、其他共享客户端或真实swapchain/Present。

## 新21组：完整数据

| 组 / driver | CPU ms | 比较 ms | backend ms | 完成等待 ms | total median/p95 ms | 无限制完成 FPS | 实际提交/跳过/复制 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| hold-fixed-full / warp | 0.8845 | 0.0002 | 0.1501 | 14.6613 | 15.7349/16.3397 | 63.553 | 100/0/100 |
| hold-fixed-no-ink / warp | 1.7146 | 0.0003 | 0.1539 | 13.8056 | 15.6202/17.3844 | 64.0197 | 100/0/100 |
| hold-fixed-no-cover / warp | 2.1018 | 0.0003 | 0.2073 | 13.2903 | 15.4960/17.4938 | 64.5328 | 100/0/100 |
| hold-fixed-copy-title / warp | 2.3076 | 0.0004 | 0.1850 | 12.8383 | 15.3998/18.2308 | 64.9359 | 100/0/100 |
| hold-solid-clear-control / warp | 1.2227 | 0.0002 | 0.1671 | 14.6327 | 15.8168/31.5106 | 63.2239 | 100/0/0 |
| hold-solid-clear-candidate / warp | 1.0106 | 0.0002 | 0.1561 | 14.4119 | 15.6708/16.2818 | 63.813 | 100/0/0 |
| dock-fixed-full / warp | 1.2064 | 0.0002 | 0.1787 | 14.7842 | 16.0053/32.1450 | 62.4793 | 100/0/100 |
| dock-fixed-no-ink / warp | 1.6039 | 0.0002 | 0.1302 | 14.0661 | 15.5599/30.8482 | 64.2678 | 100/0/100 |
| dock-fixed-no-cover / warp | 2.9538 | 0.0003 | 0.2368 | 12.3173 | 15.4988/16.8136 | 64.5211 | 100/0/100 |
| dock-fixed-copy-title / warp | 2.0476 | 0.0004 | 0.1714 | 12.9546 | 15.1652/18.0935 | 65.9404 | 100/0/100 |
| dock-solid-clear-control / warp | 1.0426 | 0.0002 | 0.1754 | 14.2847 | 15.5191/20.9536 | 64.4367 | 100/0/100 |
| dock-solid-clear-candidate / warp | 1.0491 | 0.0002 | 0.1566 | 14.2011 | 15.5130/17.2750 | 64.4621 | 100/0/100 |
| settle-fixed-full / warp | 2.2665 | 0.0003 | 0.2360 | 13.0871 | 15.5358/20.4247 | 64.3675 | 100/0/100 |
| settle-fixed-no-ink / warp | 2.2955 | 0.0003 | 0.1795 | 12.8972 | 15.2194/19.4327 | 65.7056 | 100/0/100 |
| settle-fixed-no-cover / warp | 1.2027 | 0.0003 | 0.2027 | 14.0581 | 15.6465/17.6563 | 63.9121 | 100/0/100 |
| settle-fixed-copy-title / warp | 2.3006 | 0.0004 | 0.1634 | 12.8026 | 15.1992/19.0656 | 65.7929 | 100/0/100 |
| settle-solid-clear-control / warp | 2.3142 | 0.0003 | 0.2417 | 12.8231 | 15.4063/16.7346 | 64.9085 | 100/0/100 |
| settle-solid-clear-candidate / warp | 2.2604 | 0.0004 | 0.2340 | 12.9439 | 15.4728/16.5037 | 64.6295 | 100/0/100 |
| hold-copy-omitted / warp | 1.2222 | 0.0002 | 0.1593 | 14.5306 | 15.7179/32.6323 | 63.6217 | 100/0/0 |
| hold-duplicate-skip / warp | 0.8689 | 0.1537 | 0.0000 | 0.0000 | 1.0238/1.4331 | — | 0/100/0 |
| static-duplicate-skip / warp | 1.6319 | 0.0944 | 0.0000 | 0.0000 | 1.7279/2.1928 | — | 0/100/0 |

## 原39组：最终版本的阶段完成吞吐

这些控制强制实际draw，静态/hold的数值不能冒充优化后每个重复帧仍提交。wait/write不人为提前捕获。准备/截图是有界一次事务，其单帧重复测量不能当整个动画持续帧率。

| 组 / driver | CPU ms | 比较 ms | backend ms | 完成等待 ms | total median/p95 ms | 无限制完成 FPS | 实际提交/跳过/复制 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| static / warp | 2.2607 | — | 0.2141 | 28.4491 | 31.0606/34.2879 | 32.1951 | —/—/— |
| idle / warp | 2.2962 | — | 0.2031 | 28.5207 | 30.9984/32.4925 | 32.2597 | —/—/— |
| moving / warp | 3.0438 | — | 0.2222 | 27.7844 | 31.1289/43.7652 | 32.1245 | —/—/— |
| returning / warp | 2.8933 | — | 0.2124 | 27.2613 | 30.8458/46.5697 | 32.4193 | —/—/— |
| writing / warp | 2.3155 | — | 0.2038 | 28.4312 | 31.0367/45.2520 | 32.2199 | —/—/— |
| hold / warp | 2.3347 | — | 0.2265 | 28.6502 | 31.2438/46.6179 | 32.0064 | —/—/— |
| dock / warp | 2.2805 | — | 0.2321 | 28.7715 | 31.1830/46.3411 | 32.0688 | —/—/— |
| settle / warp | 2.1542 | — | 0.2086 | 27.0758 | 30.2712/46.4877 | 33.0347 | —/—/— |
| writing-fast / warp | 1.5739 | — | 0.1375 | 14.2380 | 15.8033/31.5330 | 63.2779 | —/—/— |
| hold-fast / warp | 1.0092 | — | 0.1333 | 14.3114 | 15.4592/30.8038 | 64.6864 | —/—/— |
| dock-prepared / warp | 1.1399 | — | 0.1500 | 14.1671 | 15.4745/31.2122 | 64.6224 | —/—/— |
| settle-prepared / warp | 2.2159 | — | 0.2350 | 13.2328 | 15.4806/17.1533 | 64.597 | —/—/— |
| solid-writing-full-root / warp | 1.4430 | — | 0.1291 | 14.3286 | 15.7329/31.5766 | 63.5611 | —/—/— |
| solid-writing-suppressed-root / warp | 1.4574 | — | 0.1246 | 13.9320 | 15.3956/17.2753 | 64.9536 | —/—/— |
| solid-hold-full-root / warp | 1.1062 | — | 0.1341 | 14.3471 | 15.6116/21.3465 | 64.0549 | —/—/— |
| solid-hold-suppressed-root / warp | 1.1475 | — | 0.1512 | 14.2038 | 15.4832/18.5775 | 64.5861 | —/—/— |
| solid-dock-full-root / warp | 1.1403 | — | 0.1447 | 14.3207 | 15.5824/31.1499 | 64.175 | —/—/— |
| solid-dock-suppressed-root / warp | 1.1225 | — | 0.1518 | 14.3928 | 15.6880/31.6813 | 63.743 | —/—/— |
| solid-settle-full-root / warp | 2.2686 | — | 0.2440 | 12.8653 | 15.3820/16.6412 | 65.0111 | —/—/— |
| solid-settle-suppressed-root / warp | 2.2806 | — | 0.2368 | 12.9716 | 15.4532/16.3059 | 64.7115 | —/—/— |
| wait-full / warp | 0.7431 | — | 0.1015 | 14.4096 | 15.2781/16.0120 | 65.4532 | —/—/— |
| wait-fast / warp | 0.2413 | — | 0.1053 | 15.0223 | 15.4560/16.7006 | 64.6998 | —/—/— |
| prepare-warm-fonts / warp | 1.3798 | — | 0.1686 | 28.9843 | 30.7878/46.5351 | 32.4804 | —/—/— |
| restore / warp | 3.7675 | — | 0.2483 | 11.7754 | 15.5453/30.8691 | 64.3281 | —/—/— |
| capture / warp | 2.0108 | — | 0.4424 | 28.6157 | 31.0215/32.2754 | 32.2357 | —/—/— |
| static / hardware | 1.6620 | — | 0.0568 | 0.7634 | 2.5366/3.2457 | 394.228 | —/—/— |
| wait-full / hardware | 0.8100 | — | 0.0383 | 0.6876 | 1.5423/2.0043 | 648.382 | —/—/— |
| wait-fast / hardware | 0.0417 | — | 0.0183 | 0.1757 | 0.2407/0.3189 | 4154.55 | —/—/— |
| writing-full / hardware | 1.8382 | — | 0.0550 | 0.8470 | 2.7502/4.2434 | 363.61 | —/—/— |
| writing-fast / hardware | 0.8814 | — | 0.0450 | 0.5277 | 1.4523/2.4221 | 688.563 | —/—/— |
| hold-full / hardware | 1.6428 | — | 0.0558 | 0.8105 | 2.5846/3.6705 | 386.907 | —/—/— |
| hold-cached / hardware | 0.8650 | — | 0.0503 | 0.6551 | 1.5716/1.9986 | 636.294 | —/—/— |
| dock-full / hardware | 1.7389 | — | 0.0589 | 0.9432 | 2.7001/4.0284 | 370.357 | —/—/— |
| dock-cached / hardware | 0.8689 | — | 0.0534 | 0.6612 | 1.5838/1.9825 | 631.393 | —/—/— |
| settle-full / hardware | 1.6465 | — | 0.0547 | 0.8450 | 2.5502/3.1427 | 392.126 | —/—/— |
| settle-cached / hardware | 0.8184 | — | 0.0551 | 0.6043 | 1.4889/1.8081 | 671.637 | —/—/— |
| capture / hardware | 1.6201 | — | 0.3858 | 0.4565 | 2.4597/3.3895 | 406.554 | —/—/— |
| prepare-warm-fonts / hardware | 0.8538 | — | 0.0395 | 0.5955 | 1.5003/2.0504 | 666.533 | —/—/— |
| restore / hardware | 1.6336 | — | 0.0539 | 0.5377 | 2.2339/3.0766 | 447.648 | —/—/— |

## 省Clear实验配对

- hold：正常Clear 15.8168ms → 局部实验 15.6708ms；median收益 +0.92%，p95 31.5106 → 16.2818ms。
- dock：正常Clear 15.5191ms → 局部实验 15.5130ms；median收益 +0.04%，p95 20.9536 → 17.2750ms。
- settle：正常Clear 15.4063ms → 局部实验 15.4728ms；median收益 -0.43%，p95 16.7346 → 16.5037ms。

这些实验均日志标注clear-policy=experiment-only-not-in-product；生产实际提交正常Clear。不能依据单轮某阶段波动推广为稳定收益。

## 瓶颈判断与当前帧率限制

固定完整/去字标/去蒙版/只copy与title的完成耗时仍接近15–16ms；几十个顶点与约1.2万个顶点两端没有数量级区别。当前缓存后主要成本在WARP提交后的软件执行/调度/完成等待路径，未证明艺术字体网格或单次全屏混合是主因。EVENT样本不能细分驱动、WARP worker、CPU调度/唤醒和等待开销；需要进一步ETW/时间戳诊断才可归因。hardware诊断用于确认设备路径差异，不改变产品设备选择。

相同静态/hold样本actual-submits=0、skips=100，Clear/copy/backend/EVENT全为0；保留CPU生成与memcmp约毫秒级，避免原15ms完成路径。这是降低重复提交/共享渲染线程占用，不能报成屏幕上千FPS。时钟和延迟更新继续。

生产共享FrameInterval=16,666,667ns（约60Hz）与Setting Present(1,0)仍在。离屏不经过二者，没有应用层FPS cap；表中完成吞吐不等于真实显示帧率，真实性能还受其他共享客户端、Present/桌面合成和业务资源影响。

## 正确性与验证

- 完整InkeysRepo.sln Debug|ARM64，动态定位VS原生ARM64 MSBuild，exit 0，22.3754s。同一PowerShell进程规范化Path/PATH并恢复单一canonical Path，MSBUILDDISABLENODEREUSE=1；没有修改全局环境或项目工具链。既有Setting浮点转int C4244 warning保留，与本次helper无关。
- 最终InkeysHeadlessTests.exe --no-window 全套exit 0，278.4643s，stderr 0。EXE SHA256 `5EA497E71DF5E8AD5321F521F308EF20CAAFD5BD258E70154241FC51CAD48EBD`。
- 60组性能数据全部完成（新增21、原39）。浅深真实静态首submit1后skip12、Hold skip10时钟继续、Dock和终态实际恢复；17字段/12键CPU回归、1MiB边界/callback/纹理事务、实际IndexMap失败下一帧重试。
- 真实Fluent hover、无新事件延迟tooltip、InputText实际键入与1.2s caret、caption/data文字变化。记录caret提交12/跳过140，其计数窗口包含附加文字/标题变化，不解释为纯caret独立次数。
- 浅深×七材质/边界×四时刻精确RGBA；目标先污染；完全不透明省copy、分数客户边界/部分alpha回退，局部省Clear非byte-grid RGB回退。分解cover→ink与原完整混合像素相同。
- 最初新测试fixture失败已修复：ImVector::resize不构造ImDrawCmd，须显式初始化；裁剪DrawList后须同步内部写游标/currentIdx，保留原AddDrawList断言。没有放宽像素标准/生产门禁。
- i18n check exit0，en-US/zh-TW均384/384；git diff --check（ignore-submodules=all）通过。工作baseline与最终七文件UTF-8/BOM/CRLF/结尾审计通过；Pages两文件逐字节相同。默认全仓子模块Git元数据权限问题单列，不修改Git配置绕过。
- Win7 SP1+KB2670838、真实HWND/Present/遮挡、完整设置交互及其他UI并行现场未运行；用户规则禁止GUI。本轮仅基础D3D11/FL11.0路径与源代码生命周期审查，不能声称Win7实机或屏幕FPS已验证。

## 工作树保护与证据

首轮research/worktree-audit.md已核对原Inkeys与9个stash，没有待恢复的设置动画内容。本轮未应用/pop/delete stash或覆盖原工作树。HEAD仍c747068265a7fb64bea853676f45c1a04a6a51ce、index为空、未commit/push。既有Inkeys/PptCOM.dll SHA256 AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105保持。

原始日志、状态、metrics.json、format-audit.json及baseline在Inkeys/Cache/setting-entrance-cache-20261014/submission-20261014；candidate-*保留已通过但随后撤回Clear候选的执行，attempt1-*保留夹具失败诊断，当前无前缀日志只对应最后源码。独立审查见research/submission-check-review.md。任务保留in_progress供用户审阅与继续，不执行自动archive/commit。
