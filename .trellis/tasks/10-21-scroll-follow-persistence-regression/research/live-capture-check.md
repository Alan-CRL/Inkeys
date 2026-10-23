# 正式 FreezeCapture 实屏独立检查（2026-10-22，解锁后首门槛）

本报告对应独立 checker 的安全修复及第一次解锁后正式运行。源码/构建/GUI 已冻结交还主会话；后续扩展测试如产生新源码或产物，应以新的报告和 hash 为准。本报告不覆盖该后续实现，不更新父任务完成状态。

**结果：原定格 Mag pair 上的正式 `FreezeCapture::Start` → 原 `MagnifierThread` → `PrintWindow(child, 2)` 路径，在本机解锁后的专用图案上成功取得 12 帧。4 个 phase 各 3 帧，变化正确、静止接受、排除正确，未登记缩放回调。** 这通过了正式首帧/变化内容的有限门槛，不能代替 20 次启停、普通 Freeze 互斥、真实 Business/WindowService ACK、桌面墨迹跟随或长期性能验收。

## 范围、授权和上下文

- 当前任务：`.trellis/tasks/10-21-scroll-follow-persistence-regression`；基线 HEAD `10abcac3`。
- 已按 fallback 读取本任务 PRD/design/implement/check.jsonl 真实引用，包括父授权、旧 check、原 Mag 设计、捕获/产品规范及 `live-product-test-design.md`、`live-capture-implementation.md`。最后复核 `get_context.py --mode packages`：没有 packages，受影响层为 `native-desktop`；读取该 index、可复用的 native index Quality Check 及跨层线程/借用/呈现规范。没有变更 shader、PPT、配置、i18n 或依赖。
- 主会话转述用户明确手动解锁并继续测试，解除此前安全层停止；只创建/调整本次专用窗口，不使用 Computer Use，不改外部窗口或安全设置。正常应用入口、配置/单实例/更新/Hook 等不运行。未 commit/push，未 finish/archive。
- 新测试只从最早严格 CLI 入口调用；Release/harness-off 返回 2。本轮未做 Release 重编，guard 结论来自静态核对，ARM64 Debug 完整编译已实际执行。

## Findings（fixed）

全部修复局限 `Inkeys/IdtMagnification.cpp` 的 harness 安全，不调整生产 Mag 初始化、原 readyPromise、owner/worker、正式 Start/Stop、刷新/截图算法或等待参数。

1. **安全界面检查的位置不足。** 原检查在窗口/部分屏幕诊断之后，`Default` 名称或自身 cloak0 不足以证明安全。增加只读命中窗口 class/进程 basename 检查，在 `RunFreezeCaptureLiveTest` 的 Display/夹具窗口/屏幕读回前早退；泵等待及 owner probe 再检查，发现安全层抛出并走清理。精确识别 `LockScreenBackstopFrame`，不会因普通 explorer.exe 名称误判。检查不读取标题或内容，不操作外部窗口。它不是所有安全场景的普适检测证明，源专用内容 GT 仍为正式取帧前的必要门。
2. **private owner message/跨线程借用缺少完整凭据。** 将 `LiveReadProbe*` 改为 atomic release/acquire 发布/读取；只由本 CLI 显式启用，普通 Debug inactive 时在任何指针借用前返回，不解 `lParam`。校验 operation、原 host/child/当前 targets、真实 owner TID、当前非零 session/occupied/inFlight/held。probe session 绑定 Start 结果，不以消息到达本身当作有效授权。
3. **raw span、crop 和独立 readback 边界不足。** 验证 span 大小、最小尺寸、held 与当前 fixture/child client 几何一致；crop 分配失败捕获并记录，避免穿过 noexcept。owner 的 PW 使用独立 DibSurface，绝不与 worker 同写；ownerProbeMutex 确保 worker 超时也不会提前释放 owner 正在读的借用 span。
4. **诊断计数有跨线程普通读写。** 共享 `PrintWindowTest::threadMismatches/apiFailures` 改 atomic，格式化日志 `.load()`，数值语义不变；适用于复用结构的旧诊断，不改变旧测试流程。

probe 的 producer 持有有界 3 秒 event 等待；主诊断调用有 2 秒 SendMessageTimeout；清理先放行，再 Stop/原 worker join，再 WindowService owner join，最后撤 atomic 指针和关闭 event/log。同步 PW 若永不返回不能强杀线程或提前释放 DIB，专用子进程 90 秒 watchdog 仅作为失败退出兜底。此路径本轮没有实际进入，因此上述 probe 结论是静态安全检查，不是对照实验通过。

## Findings（not fixed）及证据边界

- 旧 `live-owner-chain` 的 17 次 NearBlack 没有完整 source 屏幕 GT，无法归因于 PW、Mag 更新或线程，不能倒推每帧都由锁屏导致。之后另一次确实发现 LockScreenBackstopFrame 并停止 GUI，见历史报告；这与本次解锁后的 PASS 分开。
- 本次正式结果没有失败，不运行驻留 probe。owner-only Flush、owner-only PW、owner Source/Invalidate/Redraw 的分步对照仍**未执行**，不能以编译或正常 PASS 声称这些诊断分支有效。
- 20 次 Start/Stop、Stop 中取消、Busy/Freeze 互斥、初始化失败和长期资源趋势未执行；单次 `gdi=11→21/user=4→5` 不能称零泄漏或重复寿命验收。原 pair 销毁与 owner MagUninitialize 已真实确认。
- 下述短测试同 phase 间隔约 150 ms，约 6.67 次/s；20 Hz 是调度目标，不能写成实测达标。当前没有为追求目标改渲染或等待参数。
- 没有执行真实 Business + StateMonitoring + 系统 WindowService 双窗 ACK、可见墨迹滚动/退出，既有隐藏 ACK seam 和本次蓝 Bar 排除均不能替代。没有浏览器/Word/PDF/真实笔触控/旧 Windows/WOW64 验收。
- 本轮未做 x64/PE 或 harness-off 重编；主会话负责决定后续 final 产物相关验证。没有新增 WGC、SetWindowDisplayAffinity 或新 Win8+硬导入；新增 metadata API 为既有 Windows 7 可用 API，不能由此声称在旧系统运行通过。

## Verification

### 构建、static 与严格 argv

- 完整 `InkeysRepo.sln Debug|ARM64`，复用 `Build/ScrollFollow/build-motion-tracker.ps1 -Round live-check-safety`。vswhere 定位 ARM64 原生 MSBuild，在同一 PowerShell 规范 Path/PATH、禁 node reuse，普通宿主，允许 10 分钟。**exit 0，外部计时 18.820 s**，无本轮环境错误/ILK 重试。
- 构建日志：`Build/ScrollFollow/motion-tracker-build-live-check-safety.log`、同名前缀 `-console.log/-stderr.log`；stderr 空。仅原 hashlib++ 的 3 条 C4267，没有新增夹具警告。C++ compile/type check pass；本仓库本次无独立 lint 工具，静态审核与 diff/物理格式检查作为 lint 范围。
- `Build/ScrollFollow/run-live-bad-argv.ps1`：10 条全部 **exit2 / stdout0 / stderr0**，单次 103–604 ms。结果 `Build/ScrollFollow/live-bad-argv-210350453/result.json`。覆盖两个 flag 的 `=bad`、错位、trailing、重复及互相组合，未落入普通 GUI。
- `git diff --check --ignore-submodules=all` exit0；三源均 UTF-8 BOM、工作区 CRLF，无 bare LF；`IdtMain.cpp` 保留无末尾换行，其余两个保留末尾换行。Git blob 的 LF 由既有文本策略处理，不是全文件改行尾。变动相对 HEAD 为 Mag.cpp `546+/2-`、Mag.h `3+`、Main `4+/2-`，大部为实现者新增专用 harness；checker 未格式化其它区域。
- 不重复既有 Tracker/持久化/长 no-window 矩阵：本次仅 harness 分支，正式算法、编码和保存源码未变化。相关既有完整证据仍见原 check-current.md，不伪称已在本次 hash 上重跑全部测试。

### 解锁后正式运行

命令：`Build/ARM64/Debug/Inkeys.exe --freeze-capture-live-test`，从仓库 cwd 启动，stdout/stderr 重定向，专用窗口自然关闭。无 probe、无 callback 图像、无模拟 backend，消费者执行原正式 API。

- 外部结果：**exit0，6.364 s**，stdout 12,517 B，stderr 2,504 B（WindowService 角色诊断），未 watchdog/kill。包装结果 `Build/ScrollFollow/live-check-unlocked-formal.json`。
- 原日志：`Build/ScrollFollow/live-check-unlocked-formal.log`、`live-check-unlocked-formal-stderr.log`；原私有目录 `Build/ScrollFollow/live-2484-210352218/result.log` 与 stdout 一致。
- PID **2484**；main TID **84704**，原 WindowService owner **22192**，原 MagnifierThread worker **79832**。MagInitialize/host/child/transform/color/Uninitialize 在 owner，正式 Filter/Source/Invalidate/Redraw/Read 保留原 worker。回调也在 worker；`wrongThread=0` 表示保持原设计，不是声称所有 API 都在一个 TID。
- Start 前专用屏幕 GT **63/63**：54 个 source 点正确、蓝覆盖层 9/9；额外屏幕 ROI `(64,64,256,192)` 每像素精确，之后才 Start。source 自身完整全屏位于本组原 root 后，蓝层有实际 owner 链；只操作本次自身窗口，`foreground_own_source=0` 不是 GT 失败。
- 主屏 **2880×1920**，原 Mag host/child **2880×1919**；原裁切/identity 映射未改。角色过滤数 **5**，专用 source 不在过滤表，Bar 在。冷启动 host alpha0；采样期间 `host_visible=1/child_visible=1/alpha=0`，保留绘制生命周期且自身 Mag 不可见，不采用隐藏/最小化/屏外替代。
- `callback_registered=0/get_callback_null=1`；没有 MagSetImageScalingCallback。正式 Start=Started/session1；seq1–12，4 phase 各3帧；所有帧 `painted==matched`，accepted12/rejected0/API0/unmatched0/old0。相同图像的静止帧正常接受。
- 结束原 Mag pair 均无有效 HWND，owner `MagUninitialize=TRUE`。没有把过程 counter delta 当资源释放全证据。

### 实际耗时（不含驻留 hook）

`frame.statistics.lastReadDuration` 从正式 Session 调用 Backend::Read 前到返回后。包含几何检查、清 DIB、PW(2)+GdiFlush、后检查及 alpha 整理；**不是纯 PrintWindow 时间**。该正常运行没有启用驻留 raw hook，不能把 probe 3 秒等待算性能。

| 统计 | n | min ms | mean ms | median ms | P95 ms | max ms |
|---|---:|---:|---:|---:|---:|---:|
| Backend::Read | 12 | 58.146 | 70.975 | 65.974 | 85.867 | 85.867 |
| 同 phase Read 完成时间间隔 | 8 | 124.469 | 149.822 | 152.527 | 176.445 | 176.445 |

P95 用 nearest rank，样本很少，故等于 max。间隔只取 phase 内 2 个相邻对，共8对，排除3个 phase 切换间隔；不包含启动暖机或终止。约 **6.67 次/s** 只是该有限 Debug 夹具的回调采样，不是长期性能或浏览器实际滚动 FPS，回调 GT 检查开销也在完整节奏内。最后 skipped23 是调度跳槽计数，没有积压 FIFO。

F0 从调用 Capture::Start 前记录时刻到首 Read 完成 **972.785 ms**；其中首 Read **84.800 ms**，余下 **887.985 ms** 包含固定 **800 ms** 首帧等待及 Begin/Update/线程调度等。这个起点不包括更早的窗口/Display 准备，因此不称总进程首帧启动耗时，也不能把差值当 Mag 刷新确认信号。4 个 phase 在本次都正确，不推出所有系统的 compositor ready 标志。

### 离线图片复核与 hash

`Build/ScrollFollow/live-check-unlocked-offline.json` 留存帧表、精确分布、图片/源格式及 hash。Pillow 离线验证每张图 **256×192 / 49,152 像素**，与专用 phase/grid 公式比较 RGB 最大误差均 **0**，alpha 全255。phase crop 位于屏幕中心蓝覆盖层范围，读到该 phase 的内容色；这结合全屏63点验证，证明本次 Bar 排除有效。screen crop在左上专用内容区，仅保存经过精确 GT 确认的内容。未保存未知个人窗口内容。

图片标注须区分阶段和坐标：`screen-dedicated-roi.png` 在蓝 Bar 已显示、after-source-order 验证成功后、Capture::Start 之前取自屏幕 `(64,64)`，位于覆盖层范围 `(960,640)-(1920,1280)` 外的 source col0/row0，精确期望 **RGB(79,63,93)**，紫灰色正确。蓝层本身用屏幕九点 **RGB(0,0,255)** 验证，没有保存蓝色 crop。`phase-1-roi.png` 则在正式 PW 图像的中心 `(1312,864)` 取256×192，位于蓝层范围内，期望被排除后露出的 source col4/row3 **RGB(176,194,32)**，所以黄绿色正确。两张图不是同一坐标，不能直接比较颜色判断排除。

| 文件（`Build/ScrollFollow/live-2484-210352218/`） | SHA256 |
|---|---|
| screen-dedicated-roi.png | d89208c0f8bb4faf40b12ae4fb2807e9c1e405913d0bc3958486daedefa5a1f0 |
| phase-1-roi.png | 9ec299391564d536744be8a4cdefc25f48ca19a3c8d40f268c2f2ee26e9f804c |
| phase-2-roi.png | 71976c84624d8f14f0c50c046b8b6c1c94ddcf52b6c301a08a71f8d89a4eaa9f |
| phase-3-roi.png | 4fa70032cfd5cf71d3cff4f5c58999bc741f57824abc046cd3838c8e691c6375 |
| phase-4-roi.png | f64f2f13d3da3a94843afed97538dce8d382dd794bd64819906810bab0c26a1f |

## 本次冻结产物

| 文件 | SHA256 |
|---|---|
| Inkeys/IdtMagnification.cpp | 297e96c2531e3dcdc17d5ddc59add70c965781be4d6de8b672c232220c249ac3 |
| Inkeys/IdtMagnification.h | 2902e83ef2be8ef81eacb44e975f975dd5b60db68cf50a1a2d921e47a7513197 |
| Inkeys/IdtMain.cpp | 120a4f0ce121a3f6aaa2924160541dbb59782b2fc36ab7cd7961d8a6eb874eb1 |
| Build/ARM64/Debug/Inkeys.exe | a80ddef767a37989fd2b005d878ee0d3a92a1333c6c9d11210f5156c210e50fe |

上述 hash 对应本报告的安全修复、build、badargv 和解锁后12帧。历史报告 `live-capture-implementation.md` 的 c84e... EXE/hash 对应更早冻结版本，不得混用。本报告完成后仅写文档，不抢占主会话后续桌面/源码/编译所有权。

允许主会话接续下一必要门槛；**不允许据此把父任务或完整功能标完成**。目前手动解锁阻塞已在本次实验解除；若后续再出现安全层，需要立即停 GUI，等待用户手动解锁，不能绕过。
