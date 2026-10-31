# Win7 22:48 反馈定位与诊断入口修补验证（2026-10-04）

本轮继续旧任务 `10-02-win7-main-canvas`，当前状态保持 `in_progress`。本次 Win7 鼠标复现已缩小到实际 GPU 绘制/写入至 staging 拷贝读回之间；仍未证明唯一 GPU 根因。这里只修复已经证实的诊断入口缺陷，不将采集修复写成画布修复。

## 本次现场证据

原始目录 `G:/Inkeys/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys-Win7-Gpu-Probe-x64-Release/20261003-224836-752`；实际 EXE SHA `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE` 与前次交付候选一致。四个文件已完整分析并复核 SHA，未写入 G:。

- Mouse Cursor 共 2508 条事件全部 accepted；RTS Down/Up 到达及发布各 25，采样 Packets 发布 22。Controller 有 MouseLeft / Mouse 的代次 1..24 正证据；短事件及节流使缺少 gen25 不能单独当作丢输入。
- 15 条 CursorGPU 是 5 组 upload/before/after。上传正常，实际查询的 10 条 before/after 均 viewport1920×1080、RTV↔backbuffer canonical identity、VS/PS、b0/t0 匹配，removedReason=0。upload 阶段 bindings=0 表示未查询，不是绑定失败。Draw(void) 已发出不证明 shader 实际执行或像素写入。
- 39 条 ULW 原始 staging 源 alpha/RGB/最大四通道均零，API均 success/error0；37 Primary 最终 DIB 只有 alpha=1 CPU命中底层，2 Selection最终透明。底层可解释消息不穿透，不能当成笔迹。
- stdout14611–14617 同 tick56460693：白色填充 EraserGrip 光标中心819,955，32×32/opacity0.5；上传与实际绑定正常，dirty801,937,837,973涵盖整个36×36quad，但1296源像素全零。第二组14898–14901同样闭合。系统光标已按工具状态隐藏，正式应用光标未得到可见源像素。
- 有68个 Laser tip 视觉请求；stdout8755–8758同 tick56442643 的红色tip及其dirty读回仍零。不是仅采到未画空白帧。
- 源码核对实际 Cursor 后直接 Present→dirty Copy→Map，没有同帧再次清屏/合成覆盖；dirty坐标/目标offset/RowPitch一致，目标纹理实测匹配。现有shader数据布局、shape6数学输出、blend/raster/depth静态设置未发现足以证明本案唯一缺陷。

结论：本次 Mouse contact 已通过 RTS 并被消费，缺少 Win8 Pointer/Pen API 或 RTS 完全无消息不能解释当前全部不出墨。实际数位笔/Touch不在本次已验证范围。ULW收到的源已透明，优先分界应放在GPU执行/写入与读回，而不是先改USER32 dirty窗口。

## 为什么还缺分界，以及本轮具体修补

现场 identity 第6行 `pixel-test-error=无法使用指定的命名参数解析参数集。`，没有pixel-test stdout/stderr，独立 Clear / production Draw / blend对照未执行。

1. 采集器probe同时使用WindowStyle Hidden与重定向，PS2参数集冲突。最小改为NoNewWindow复用采集控制台；该早退CLI只创建永不显示的fixture。新增ProbeOnly与Run-Win7-PixelTest.cmd，使下次不用重复正常画布操作。
2. 本机实际Windows PowerShell复验进一步发现：Start-Process给wWinMain的lpCmdLine尾部添加U+0020（Hidden和NoNewWindow均出现），旧EXE全串比较失配。一轮旧二进制测试因此意外进入正常启动，已由30秒上限终止精确本任务子进程，原始输出保存在collector-validation/actual shipped exe；之后停止旧EXE测试，改用不建HWND的参数dumper证明。IdtMain仅将GPU flag放入已有CommandLineToArgvW分支，匹配且argc2执行；额外参数返回2，早于配置/单实例/产品主窗。
3. 采集器立即缓存进程Handle、不Refresh后再读ExitCode；null退出码显式error。超时与取得子进程后的异常尽力Kill/Wait该精确子进程，不按名称杀进程。普通采集错误后继续，ProbeOnly错误返回1并早退。

本轮生产C++只有IdtMain.cpp参数分派的8增4删；未改输入准入、shader、混合、swapchain、呈现或项目配置。源保持UTF-8 BOM/CRLF，脚本ASCII/CRLF。GPU自检本身复用上一轮已经交付的实现。

## 验证及实际边界

| 检查 | 结果 | 证据与限制 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug ARM64 | exit0 | 通过vswhere定位ARM64原生MSBuild；同PowerShell规范化PATH，/m:1 /nr:false；900秒上限；输出隔离 |
| 完整Solution Release x64 | exit0 | 同一构建链，候选输出隔离；既有conversion/LNK4075警告，不修改无关源码 |
| ARM64 Headless --no-window | exit0 | ARM64-Debug/headless.stdout.txt、stderr；纯逻辑回归 |
| 实际Win32 argv边界 | PASS | 无HWND parser验证quotes/case/trailing-space/tab、extra/empty-extra等12例；旧raw-match失配有红证据 |
| 实际WinPS5.1新ARM64 Debug自检 | exit0 | 六例PASS，四production failures0，visible0；无普通启动或配置副作用 |
| 实际WinPS5.1最终脚本+新x64 Release | exit0 | 六例PASS，四production failures0，visible0；actual x64 corrected/Win7-Diagnostics/20261004-074303-285 |
| 实际x64额外参数 | exit2 | WinPS5.1，stdout/stderr零，无普通启动副作用 |
| collector双流大输出/非零/launch/exit-code/wait异常/30秒超时 | PASS | 双流各超过1MiB且尾标记完整；失败ProbeOnly1；普通分支仅mock；超时和等待异常精确PID已消失 |
| PS2参数集 | 模型红→绿 | 限定参数集模型与现场原始错误相符；不是实际Win7 PS2运行 |
| 独立检查及spec同步 | PASS | check-followup-20261004.md；没有剩余本地code finding；Win7根因仍待证据 |
| 保留现场/已有资产 | PASS | 8个CSO、4个G:采集文件hash一致；gitignore无diff；旧包/日志保留 |

本机是Windows11 ARM64 / Windows PowerShell5.1，不能替代同Win7设备的结果。fixture自检为128×128 Circle shape4、全量CopyResource及显式同步；如果现场六例全部正常，下一层才比较实际EraserGrip shape6、1920×1080、dirty CopySubresourceRegion与阻塞Map。自检四正式用例决定exit0，两个关闭混合对照单独读取，不能仅看尾行PASS。

## 新候选身份与最短实机动作

- 源码基线HEAD `529722e6877a35d2aa5e804657e55543436610ef`，加本轮未提交IdtMain scoped补丁；不是干净HEAD构建，也不是旧06CD候选。
- 新Release x64 EXE SHA `2D468B91156333C5CBD9E1E3C82D5713961DD94647ECCF4B44285FFC8EDDD72C`；PE machine0x8664。
- Collect-Win7.ps1 SHA `6E56DC40B66592487FC69EFEDD06B58706D3962257F9F08FBE0D1DE8B06F4CAA`。
- ZIP `C:\Users\alan-\.codex\visualizations\2026\10\03\01a100c6-aa67-7cc3-9d01-6cda328f70c3\win7-224836-followup\Inkeys-Win7-Gpu-Probe-PS2-x64-Release.zip`；29203669bytes；SHA `B9901CD1E64A636CE37D3B3CA4B2C6CF59CC9BA014155A02062FCE410306C22F`，ZIP CRC和逐文件hash已核。
- 包内manifest记录源码和产物身份；source-diagnostics.patch为基于当前HEAD的生产源码delta。新包单独保存，不覆盖G:旧包。

退出旧Inkeys，将整个新包解压到独立目录，双击Run-Win7-PixelTest.cmd，回传Win7-Diagnostics下这次整个时间目录。只需要这一轮独立自检，不需再手动画笔/橡皮/激光。缺Clear/Draw分界是当前真正阻塞唯一根因的外部证据。

任务保持in_progress，未commit、push、发布、archive。详见capture-224836-analysis-20261004.md（输入/像素逐行证据）、gpu-pipeline-narrow-20261004.md（GPU源码与官方合同）、collector-validation.md（本机采集边界）及独立检查报告。
