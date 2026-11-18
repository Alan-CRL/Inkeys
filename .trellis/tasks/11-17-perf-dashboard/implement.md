# 第2步-C 实施计划

> 分工（2026-11-18 用户确认）：Claude 编写全部实现；完成后 Claude 独立审阅 → Codex `gpt-6.2-sol` / high / `service_tier=default`（不用 Fast）只读审阅 → 修复 → 复审。
> 改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF（Git Bash `sed -i` 会改 CRLF，改完核对）；构建 `InkeysRepo.sln Debug|ARM64`（ARM64 原生 MSBuild，`-t:Inkeys;InkeysHeadlessTests`，超时 ≥ 5 分钟，PowerShell 内 `Remove-Item Env:PATH` + `MSBUILDDISABLENODEREUSE=1`）；增量链接偶发 LNK1104/LNK1000 删 `Build/ARM64/Debug/Inkeys.ilk` 重试。新增源文件要同时加入 `Inkeys.vcxproj`（及 `.filters`），纯逻辑头被 `InkeysHeadlessTests.vcxproj` 引用。
> 开工命令：`python ./.trellis/scripts/task.py start 11-17-perf-dashboard`。**审阅批准 prd.md 中 R1–R4 之前不开工。**

## 步骤

0. **开工核对**（有矛盾先停下汇报）：
   - 帧尾计数位置：C:23686 `canvasDebugOverlay` 块、`debugOnlyFrame` 判定、`canvasFrameElapsedMilliseconds` 与 `frameLatencyWaitMs_` 的取值时点；`selectionMode`、`outputTargetChanged`、设备恢复帧在该处可见的变量名。
   - 空闲移交点：C:22287 `completeUlwBeforeIdle()` 与 C:22314/22329/22349 各 `WaitForWake` 分支，确认哪些属于“真正空闲”（无接触、无动画）；Run 正常退出路径。
   - `RuntimeStroke::tool`（`DrawingTool`）在 Down 时锁定、橡皮（含手掌、速度橡皮）均为 `DrawingTool::Eraser`；激光 `laserLifecycle.phase` 与 `canvasPan.Active()` 的作用域。
   - 实际呈现模式与设备类型从哪里取（`TransparentPresentationController` / Renderer 的实际 Hardware/WARP、DComp/ULW/其他），运行中回退时的通知点。
   - 显示器模式刷新率：`Draw3.TransparentPresentation.cpp:1479-1495` 的 `EnumDisplaySettingsW` 结果能否发布给 Host。
   - Host 生命周期（`Draw3.Host.cpp:2096` 起 Start、`2605` Stop）：账本能否在画布初始化失败时仍启动；`HostStartOptions` 加字段的透传方式（参照 `autoSaveRoot`，IdtMain.cpp:3931）。
   - 设置页：S:4625-4694 性能页结构；`ConditionalBlock`、`PageBlockScope`、`NavigationTween`、`IconButton`、`RenderChevronLerp`（文件内静态）可用性；`Palette` 结构与 Acrylic alpha 调整位置。
1. **纯逻辑头 `Draw3.DevicePerformanceModel.h`**（design §2.1、§3、§4、§5）：维度枚举与工具映射、`PerformanceTally`、`PerformanceSignature`（规范化 + 可读键）、日桶合并（7 个使用日淘汰、签名上限 8）、报告计算、档位与帧率建议（返回枚举 + 数值参数，不含文案）。无 Windows 依赖。
2. **单测 `InkeysHeadlessTests/draw3_device_performance_tests.cpp`**（design §10 纯逻辑部分），在 `animation_tests.cpp` 注册并加 `--device-performance-only`；加入 `InkeysHeadlessTests.vcxproj`。先让它通过再接产品代码。
3. **账本模块 `Draw3.DevicePerformance.cppm/.cpp`**：`DevicePerformanceLedger`（Start(path)/Stop/Submit/Report/Clear/SetRecording）、jsoncpp 读写、写盘线程（30 s 防抖、Stop 立即写）、坏文件改名、加载与内存合并。JSON 往返与坏文件测试放进步骤 2 的测试文件（用临时目录，路径注入）。
4. **Host 接入**（`Draw3.Host.h/.cpp`）：`HostStartOptions.devicePerformancePath`；Impl 持有账本，Start 早于绘制线程、Stop 晚于绘制线程 join；公开 `DevicePerformance(uint64_t, DevicePerformanceReport&)`、`ClearDevicePerformance()`、`SetDevicePerformanceRecording(bool)`；报告附带显示器刷新率。`IdtMain.cpp` 传入 `<exe目录>\Inkeys\Memory\device-performance.json`。
5. **Controller 接入**（C）：Run 局部 tally 与当前签名；帧尾按 design §2.2 判定并 `Add`；呈现模式/设备变化时以旧签名移交再换签名；空闲移交点与 Run 退出移交；读记录开关原子量。新增代码处加中文注释说明口径（等待不计入、为何排除恢复帧等）。
6. **配置（若 R3 批准）**：`Other.Config.cppm` 新增 `Drawing.DevicePerformanceRecording`（NoUpload，`IdtAtomic<bool>`，默认 true），启动与设置变更时调用 Host setter。
7. **设置页 `Setting.PerformanceDashboard.cpp/.h`**（design §6）：卡片绘制、档位底色过渡、大数字过渡、展开按钮与 `ConditionalBlock`、6 行明细与条形、元信息行、无数据/上一签名/记录暂停/样本少各状态；`Palette` 新 token（浅/深/Acrylic）；S:4627 后插入一行调用。
8. **i18n**：`src/i18n/zh-CN.jsonc` 的 `SettingsUI.Performance.Dashboard.*`（标题、4 档名称与结论、6 维度名、建议 6 条模板与附加说明、元信息模板、空态、开关与清除按钮、标签）；`pwsh ./Scripts/i18n.ps1 sync` 后补全其余 19 种语言译文，`check` 通过。带数字的模板用占位符，不拼接语序。
9. **构建与自测**：Debug|ARM64 解决方案构建；`InkeysHeadlessTests.exe --no-window` 全套与 `--device-performance-only`；i18n check；BOM/CRLF 与 `git diff --check`。不启动设置窗口与产品 GUI。
10. **审阅**：Claude 独立审阅（热路径无分配/无锁、锁内无 I/O、生命周期顺序、文件损坏路径、文案与规则一致）→（如授权）Codex 只读审阅 → 修复 → 复验。
11. **交用户实机**：
    - 画一笔后打开设置 → 性能页立刻看到数据；依次用荧光笔、橡皮、激光笔、图形、漫游，展开后各行逐步出现。
    - 深浅主题、Acrylic 下四档配色（可用压力工具 `Scripts/Start-StressLoad.cmd` 制造黄/红档）；展开/收起动画与中途反向。
    - 改画布帧率并重启：显示“设置变更前”，画几笔后切到新签名；改回原帧率重启，旧数据恢复。
    - 帧率建议文案在本机 120 Hz 屏下与 60/240 预设的表现。
    - 退出后 `Inkeys/Memory/device-performance.json` 内容合理；手动破坏文件后启动不崩溃且生成 `.bad`。
    - 画布调试负载与本功能开启前对比无可见差异。
12. **收尾**：实机通过后更新规范——`settings-performance-and-drawing.md`（性能页顶部看板、新配置键、签名口径）、`draw3-integration.md`（计数帧与工作耗时口径、移交点合同）；更新父任务 `prd.md` 子任务表与 `handoff.md`；经用户授权提交。

## 回退点

- 步骤 1–3 独立于产品代码，可单独提交或丢弃。
- 步骤 5 的接入点集中在帧尾与空闲移交两处；出现问题时去掉这两处调用即恢复原行为，账本与 UI 显示空态。
- 档位阈值、配色、建议阈值都是常量，实机后直接调整。

## 风险

- 帧尾判定条件遗漏导致把一次性重帧计入：步骤 0 逐一核对，单测覆盖可抽出的判定函数；实机用画布调试的负载对照。
- 设置页 Setting.cpp 现有大量静态工具函数，新文件需要导出少量接口（`RenderChevronLerp`、颜色过渡）；只加声明、不改行为。
- 20 语言译文质量：中文为准，其余由 Claude 翻译，用户抽查。

## 进度（2026-11-18）

- 步骤 0 开工核对：5 路只读核对完成，设计调整记入 design.md §12。
- 步骤 1–3：`Draw3.DevicePerformanceModel.h`、`Draw3.DevicePerformanceJson.h`、`Draw3.DevicePerformanceLedger.h/.cpp`；单测 `InkeysHeadlessTests/draw3_device_performance_tests.cpp`（`--device-performance-only`）覆盖映射、累加、签名、日桶、报告、建议矩阵（含刷新率容差）、JSON 往返/坏文件/单桶非法、账本文件级（坏文件 `.bad`、Stop 落盘、重载合并、同一路径重启不翻倍、清除删文件）。
- 步骤 4–6：Host（启动/停止顺序、刷新率采样、公开接口）、呈现层 `EffectiveState()`、Renderer `UsesZhaoxinCompatibleUpload()`、Controller 计数与移交、配置键 `Drawing.DevicePerformanceRecording`、IdtMain 路径与启动下发。
- 步骤 7：`Setting.PerformanceDashboard.h/.cpp`、Palette 新 token（浅/深/Acrylic）、性能页接入。
- 步骤 8：zh-CN 文案、`i18n.ps1 sync`、`IdtI18n.cpp` 格式模板登记；其余 19 种语言翻译交给一个子代理（用户 2026-11-18 允许：主进程或单个子代理或 Codex，不用工作流）。
- 分工更新：用户要求完成后由 Codex `gpt-6.2-sol`（high，`service_tier=default`，不用 Fast）只读审阅，同时 Claude 独立审阅。
- 审阅（2026-11-18）：Claude 独立审阅修复账本 2 处竞态（空段唤醒、关机时清除）。Codex gpt-6.2-sol high/default 三轮只读审阅：第一轮 9 条，成立并修复 #1 帧内签名切换加锁（改为第二预分配槽，空闲移交）、#3 清除与加载代次、#4 坏文件与记录关闭/改名失败/写阻断重置、#7b 重启提示比较当前会话帧率、窄宽标签；不修 #2（写盘在后台线程空闲时开始）、#5（节流帧工作真实发生）、#6（跨午夜已接受）、#7a（签名描述设置组合）、#9（设置页字符串开销）；#8“已匹配可升 240”文案并入用户下一轮文案修改。第二轮补修：段清除代次（MarkBusy 返回、Submit 锁内拒收）、Submit 锁内复查记录开关、暂停标签优先级、测试先判容器再取下标。
- 用户实机：功能基本通过（2026-11-18）。下一轮：文案修改（含 #8），提交后进行。
