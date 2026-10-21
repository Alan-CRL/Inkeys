# 滚动批注捕获原型与真实 PoC 门槛

本合同来自 2026-10-21 的 scroll-follow-v1 实施。当前只加入独立捕获基础与诊断，尚未接入 Tracker、Draw3 或主栏。共享定格运行库的真实 PoC 未通过；已对齐STA COM的bare-lease及预建隐藏first-owner控件的global-once服务对照均在排除API失败。审查期间独立cold也出现NearBlack。不能宣称功能已可用或平台全面不支持Magnification；完整产品端到端未执行。

## 1. Scope / Trigger

修改 Graphics/MagCaptureService、MagnificationRuntime、定格运行库寿命或滚动批注捕获测试时应用。产品 Overlay owner 在 Window.cpp::RunGroup 中先 CoInitializeEx(STA) 再 CreateGroup/MagnifierHost；fixture 必须复现顺序并记录 HRESULT，不能忽略 RPC_E_CHANGED_MODE 后继续当作等价上下文。窗口与位图所有权、同步 API 超时及真实像素门槛为本阶段核心边界。

## 2. Signatures

命名空间 Inkeys::Graphics，普通 C++ 头文件/实现：

- MagCaptureService::Start(MagCaptureOptions, FrameCallback) noexcept → bool；Stop() noexcept；Snapshot() const noexcept → MagCaptureSnapshot。
- MagCaptureOptions：session（非零）、framesPerSecond（1..60，默认20）、verbose。
- MagCaptureFrame：BGRA span、width/height/stride、sourceRect/workArea、session/sequence/displayGeneration、sampledQpc/qpcFrequency、captureMilliseconds、quality/error。
- AnalyzeMagFrame(span, width, height, stride)、MagCaptureResultTimedOut(now, lastCompletion, initializing)、MagCaptureReasonText(reason)。
- MagnificationRuntimeLease::Acquire/Reset/Api；GetMagnificationRuntimeSnapshot；SupportsMagCapture(error)。
- InkeysHeadlessTests.exe --mag-capture-only / --no-window 为无窗口检查；--mag-capture-poc 为保留 Freeze lease 的真实门槛；--mag-capture-poc-cold 为独立冷启动对照；--mag-capture-poc-freeze-control 为预建真实隐藏first-owner控件且global-once的追加服务对照。
- --mag-runtime-thread-poc 与 --mag-runtime-owner-poc 是显式诊断实验，不能替代真实服务门槛。--mag-capture-pattern-child 仅用于测试自己启动的专用图案子进程。

## 3. Contracts

- Start 只表示异步请求被接受；不代表已有有效 F0。返回 true 后仍须检查状态和有效帧。重复启动在旧 owner 或 monitor 未退出时以 ERROR_BUSY 拒绝，不能覆盖原会话。
- 一次 Display 快照锁定主屏 bounds/workArea/generation/DPI；拒绝 fallback/无效几何。会话中固定 source，后续产品协调者负责显示变化时结束会话。
- 专用 owner 线程初始化 COM，拥有 Host/Child、消息泵、DibSurface，顺序执行窗口排除→Source→Redraw→PrintWindow。BGRA top-down、alpha 不参与质量判断，缓冲仅借用至本次同步回调返回。
- Host 先设 alpha=0 再显示，保持不可激活/穿透且不最小化。当前机的 hidden/offscreen 对照返回常量背景；不能把隐藏 HWND 等同于保留可绘制生命周期的透明窗口。
- 目标20FPS，无图像FIFO、慢帧不补发。历史有效cold轮次在2880×1920测得平均54.523/64.991ms、实际10.302/8.491FPS；审查复跑55帧NearBlack/0有效，不保证可重现。该结果不是20FPS或稳定可用证明。
- 初始化有独立5秒期限；进入采样后1秒无完成结果即撤销发布并请求停止。监督线程不强杀 owner，不提前销毁其 HWND/DIB/运行库。
- Stop/析构不等待同步系统调用或已准入回调。调用者必须保留回调所引用的依赖，直到 Snapshot.resourcesReleased；该值要求 owner 和 monitor 真正退出。关闭发布后的旧帧/回调仍须由后续会话身份闸门拒绝。
- 窗口排除、Source、Redraw或PrintWindow失败的帧不能标为有效。近黑/低信息有独立质量原因；相同图像可以是有效静止帧，禁止以 hash 不变判断捕获停止。
- 运行库 lease 维持进程级首次 Initialize/最后一次 Reset 才 Uninitialize，DLL 在所有使用者释放后才卸载；清理失败保留DLL并拒绝下一代。独立窗口按各自 owner 释放。当前该寿命保护不等于已证明跨 owner 捕获可用。
- SupportsMagCapture 的版本/WOW64/能力门禁只控制此可选功能；动态解析较新接口，不提高整个主程序的 Windows 7 导入下限。仅当前 Win11 ARM64 已实际测试。
- [ScrollFollow] 生产诊断复用唯一 DiagnosticOutput 会话和有界异步队列；服务不创建控制台或第二个输出会话。测试控制台输出不得被迁入生产热路径。

## 4. Validation & Error Matrix

| 条件 | 必须行为 / 当前证据 |
| --- | --- |
| 参数、几何或版本/WOW64不合格 | Start返回false，保留具体原因，未创建正常会话 |
| 旧线程尚存 | 返回ERROR_BUSY，不新建worker、不替换旧状态 |
| 系统调用或回调长时间未结束 | 监督撤销发布；资源继续由旧owner保留，UI不join |
| 静止纹理画面 | 同图仍可有效；不产生伪超时 |
| 近黑/纯色/尺寸或缓冲非法 | 帧无效，原因和质量统计可追溯 |
| 冷启动真实服务 | 历史两轮真实通过；审查同源码复跑55帧NearBlack/0有效，不能用历史通过覆盖最新失败 |
| 预持Freeze lease再启动捕获 | bare-lease及创建即隐藏的first-owner Host/Child（identity Transform/ColorEffect、不先Source/Filter/Show）对照均130帧Filter/error50、0有效，阶段未放行 |
| 额外每owner Initialize或最后一次Uninitialize实验 | 不能凭BOOL成功替代像素和资源检查；尚无满足批准合同的安全替代方案 |
| first-owner幸存控件首次显示读回 | 追加fresh对照中API成功但读回黑帧，缺少相同样式独立正对照，不能推定捕获破坏Freeze |
| 同owner两个独立控件 | 对齐STA后真实像素与交替更新成功；private bytes随轮次增长，尚未证明长期资源有界，不能视为已验收修复 |

## 5. Good / Base / Bad Cases

Good：独立外部测试图案确实绘制多个阶段，读回其已知像素；同进程覆盖窗口排除后读到图案，再核对停止、再启和资源。

Base：定格仍经原有隐藏准备/排除/Source/重绘/显示事务，只窄改运行库 lease 寿命；原型尚无主栏入口。

Bad：只看到约20次/秒回调便宣称可用；为了通过门槛移除预持Freeze lease用例；对额外Initialize隐藏计数；只因API成功便接入穿透。

## 6. Tests Required

- 无窗口检查必须覆盖尺寸/stride/缓冲、黑帧、低信息、暗色纹理、相同有效帧、参数和两种超时边界。
- 显式真实PoC使用专用外部子进程、共享阶段/painted确认、已知像素、同进程覆盖窗、自动关闭和Job清理；分别记录源绘制、API结果、质量、真实源匹配。
- 冷启动与预持Freeze lease必须分别从新进程运行，不能用冷启动通过替代共享场景。记录实际FPS、mean/p95/max耗时、元数据、窗口状态、重复启停和清理。
- 任何替代生命周期方案须验证另一仍存活控件在清理后继续获得新的源像素，检查多轮GDI/USER/handle/private-bytes趋势；调用次数只是证据的一部分。
- 完整Debug|ARM64 solution、现有--no-window套件、i18n、PE导入和物理编码/EOL检查仍须完成。用户未授权的桌面交互不能自动开启；本任务已获专用自动关闭GUI测试授权。
- 真实共享门槛通过前，Tracker/Draw3/UI/UInk集成保持未开始，未通过任务不得归档为完成。

## 7. Wrong vs Correct

Wrong：MagInitialize只调用一次，所有线程都拿到同一函数表，因此假定所有新Mag HWND均可捕获；PrintWindow返回TRUE就认为图像有效。

Correct：先保护全局寿命，再对实际创建/调用线程、真实像素、排除和清理做独立验证。若共享场景失败，保留安全原型与日志并停止产品接入；修复路线须重新通过同一个门槛。

参考：[PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)、[Magnification概述](https://learn.microsoft.com/en-us/windows/win32/winauto/magapi/magapi-intro)、[MagSetWindowFilterList](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist)。详细实测见任务research/capture-poc.md；跨线程关联为本机观察，不能写成微软已保证的API规则。
