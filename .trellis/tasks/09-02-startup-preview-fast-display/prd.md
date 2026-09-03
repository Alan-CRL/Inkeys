# 启动预览与快速显示主栏

## Problem Statement

Inkeys 最终进程越过 SuperTop 后仍需依次完成日志、配置、PptCOM、共享渲染管线、字体、Window Service、Draw3、Setting、Whiteboard 和 Bar 初始化。现状直到正式 Bar 首次成功提交后用户才看到主栏；若某个初始化阶段较慢，启动过程没有即时界面或真实进度反馈。本任务不承诺缩短全部初始化耗时，而是在不复制图形设备、不破坏 Win7 基线的前提下尽快显示可信的主栏预览，并在正式 Bar 的首个完整 committed frame 后可靠交接。

## Product Requirements

- **PR-01 启动边界**：只有最终 Inkeys 进程越过 SuperTop 重启/提权分支后才能记录 T0、创建 Tracker 或 Preview；SuperTop 父进程和辅助进程不得创建 Preview。
- **PR-02 配置**：新增 `Experimental.Inkeys3.UI3.StartupPreview.Enable`，默认 `true`，首次启动也开启；Setting 的 UI3 实验选项必须说明“下次启动生效”。关闭后不得创建 Preview，并尽量保留现有初始化顺序。
- **PR-03 T0**：T0 必须在 SuperTop 分支结束后立即由 `steady_clock` 记录，并只作为真实启动进度的统一时间边界。进度条可见门从 Preview 首帧成功提交并请求显示时另行起算，不得从日志、RenderPipeline 或 Bar 初始化开始计算。
- **PR-04 真实进度**：Tracker 从 T0 工作，按真实、一次性完成的 milestone 累加；多线程乱序和重复报告不得回退或重复计数。显示值可平滑追赶但不得超过真实值，不得按时间伪造进度。
- **PR-05 完成条件**：只有正式 Bar 首个 `presentCompletion.IsCommitted()` 才能报告 100%。失败必须冻结真实值并显示红色，不能补齐未完成权重。
- **PR-06 Preview 窗口**：独立 owner/message thread 创建无激活 layered tool window；窗口吞掉范围内点击，不抢焦点，创建、定位、隐藏和销毁均在 owner thread 执行。
- **PR-07 共享图形体系**：Preview 必须作为 `RenderPipeline::Client::StartupPreview` 使用现有共享 D2D1.1/WARP/单渲染线程。禁止第二套 D2D/D3D device、渲染线程或 Win10-only effect。
- **PR-08 早期管线**：开关开启时可条件化提前初始化 RenderPipeline；关闭时保留原位置。原位置必须接受“已初始化”，字体集合继续留在原有较晚阶段。共享 RenderPipeline 初始化失败保持致命语义。
- **PR-09 默认资源**：嵌入一份由真实默认 Bar committed frame 可复现导出的中文、深色、当前默认按钮和 96 DPI canonical 预乘 BGRA BIN；不得模型绘制、AI 生成或另做 mock 场景。
- **PR-10 磁盘缓存**：缓存最近一次满足稳定条件的真实 Bar 帧到 `<exe>\Inkeys\Cache\StartupBarPreview-v1.bin`。读取须区分 Missing、Valid、Incompatible、Corrupt；失败只能降级，绝不能阻止主程序。
- **PR-11 防御解析**：BIN/cache 显式 little-endian 序列化，不直接写 C++ struct；校验版本、尺寸、stride、payload、矩形、溢出、上限和 IEEE CRC-32 后才分配/使用像素。
- **PR-12 缓存写入**：只从成功 committed 且无 hover、点击、菜单、弹层或暂态动画的稳定帧抓取；渲染线程完成 crop 和 CPU staging，普通内存由单个后台 writer 用临时文件、FlushFileBuffers 和原子替换落盘，只保留最新 revision。
- **PR-13 Preview 渲染**：内嵌帧用 cubic 缩放、D2D GaussianBlur（Balanced/Soft）并扩展 effect bounds；shimmer 只覆盖源 alpha，使用 `FillOpacityMask` 和恢复后的抗锯齿状态。
- **PR-14 3 秒进度条**：Preview 首帧成功提交并请求显示满 3 秒仍未完成时，约 180ms 渐显确定进度条；成功达到真实 100% 后先满格停留约 300ms，再在 120-160ms 内隐藏；失败不等 3 秒，立即绘制当前真实比例的红色帧。进度条在 Z 轴最后合成，坐标以包含主按钮的完整主栏为基准水平、垂直居中，采用 WinUI 3 的 3 DIP 指示条覆盖 1 DIP 轨道样式。
- **PR-15 Bar 事务**：Preview 存在时 Bar 首帧以全局 alpha 0 提交；alpha 具有 requested/attempted/committed 三态。alpha 改变强制全窗口 ULW，失败不推进 committed alpha 或业务 dirty transaction，并请求全脏重试。
- **PR-16 帧桥接**：每个成功 Bar frame 发布精确 crop、viewport、screen destination、monitor geometry、visual signature、device generation 和安全非 target bitmap；失败帧不得推进进度、交接、缓存或代理。
- **PR-17 显式失败**：Bar 启动发布 WindowMissing、ClientRegistrationFailed、FirstFrameCommitted、StartupFailed、StoppedBeforeReady 或等价状态，不再静默 return。Window Service、Draw3、Freeze 和 PPT UI 的可验证子阶段也应轻量报告。
- **PR-18 三类交接**：Valid 使用清晰缓存并在单 Preview 窗口内切换到实时代理后按提交边界切到 Bar；Missing/Incompatible 使用嵌入模糊帧、高模糊替换代理并解除模糊；Corrupt 使用 Preview 淡出、30-50ms 全透明、Bar 淡入，禁止代理解除模糊。
- **PR-19 失败流程**：已显示 Preview 后发生致命启动失败时，先尝试提交红色真实进度帧，最多等待 350ms，再进入现有错误弹窗；Preview 本身失效时直接弹窗。
- **PR-20 生命周期**：device generation 变化时清理 Preview 的全部 device-dependent 资源；退出时先注销/停止 StartupPreview 和 topmost observer，再停止 Bar、Window Service 与 RenderPipeline；所有等待必须有上限。

## Non-goals

- 不要求缩短所有真实初始化耗时，不重写整个 `wWinMain` 或 Bar 渲染循环。
- 不建立多 DPI、多主题、多语言资源矩阵；本阶段资源基准只有中文、深色和当前默认按钮。
- 不增加 EXE 资源 ID 1 manifest，不改变 PptCOM 221 号 activation manifest，不扩大或缩小现有平台承诺。
- 不引入 GDI+、WinUI Runtime、DirectComposition 依赖或 Windows 10 专属 API/effect。
- 不把 Office 实例连接当作启动完成门；只跟踪当前主启动路径上的 PptCOM activation 与 PPT UI client 注册。
- 不借此任务整理无关旧代码、改变业务 dirty 语义或重新设计 Window Service。

## Win7 Compatibility Contract

- 最低环境保持 Windows 7 SP1 + KB2670838、D3D11、D2D1.1、Feature Level 11.0 和 WARP。
- Win8.1+ 的 Shcore DPI API 必须动态解析；Win7 回退 `SetProcessDPIAware()`，不得形成加载期静态依赖。
- D2D effect 限于 Platform Update for Windows 7 提供的 D2D1.1 GaussianBlur；alpha mask 使用 Win7 可用的 `FillOpacityMask`。
- 构建不得破坏 Win32、x64、ARM64；仓库没有 ARM64EC 工程配置，第二阶段只做 `_M_ARM64EC` 源码兼容审计，不擅自新增配置。

## Acceptance Criteria

- [ ] AC-01：开关默认 true、首次启动开启、Setting 文案明确“下次启动生效”；关闭后未创建 Preview 且现有顺序无非必要变化。
- [ ] AC-02：SuperTop 父/辅助进程无 Preview，最终进程在合法边界记录唯一 T0。
- [ ] AC-03：进度单调、真实、可并行合并；时间流逝不会增加值；仅 Bar 首个 committed frame 产生 100%。
- [ ] AC-04：Preview 不激活、不抢焦点、吞掉点击；所有 HWND 操作与销毁均在 owner thread。
- [ ] AC-05：Preview 与 Bar 共用唯一 RenderPipeline；Bar 在 dispatch order 中先于 Preview；device loss 后无旧 generation 资源。
- [ ] AC-06：有效 BIN/cache 通过格式、边界和 CRC 测试；截断、溢出、超大、非法矩形、未知版本和 CRC 错误均安全分类。
- [ ] AC-07：默认 BIN 可由真实 Bar committed frame 重现；资源内容与 visual signature/layout epoch 一致。
- [ ] AC-08：Bar alpha 的 requested/attempted/committed 与 ULW 事务一致，失败后不推进 committed 状态且全脏重试。
- [ ] AC-09：Valid、Missing/Incompatible、Corrupt 三条交接路径均以提交成功为前提，且不存在两个 layered window 长时间半透明造成的颜色加深。
- [ ] AC-10：启动失败时红色进度帧最多等待 350ms；Preview/blur/shimmer/cache 失败均按契约降级，RenderPipeline 失败仍为致命。
- [ ] AC-11：缓存只从稳定 committed frame 产生，写入使用 durable temp + atomic replace；只读目录不影响启动，writer 在退出前可界定地停止。
- [ ] AC-12：`InkeysRepo.sln` 的 Debug x64 完整构建和 `InkeysHeadlessTests.exe --no-window` 通过；第二阶段补齐可构建架构并执行 Win7 VM 手工矩阵。

## Key Product Decision

- **已批准 D-P01**：Valid Cache 分支采用“单 Preview HWND 内从 cache bitmap 切到实时 Bar proxy，随后在 Bar alpha 255 的 committed frame 边界立即隐藏 Preview”的交接；禁止两个 layered HWND 同时以中间 alpha 长时间 cross-fade，避免改变透明边缘的有效覆盖率和颜色。
- 其余实现项采用 `decision-log.md` 中已批准的安全默认值。
