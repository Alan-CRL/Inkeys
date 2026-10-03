# 发布前任务续接：有限 R1 直接复审（2026-10-03）

核对 HEAD `21a37239336864b2b334abedec4e88d484d10473`、分支 `chore/publish`。用户只授权本次 Win7 诊断提交；不延续为其它 commit/push 授权。Win7 主画布任务保留 in_progress，目标机验证延期至明天。本 reviewer 只新增本报告，不修改生产、测试或项目文件，不启动 GUI、不构建、不运行产品测试。

## 上下文与范围

已读取完整 hook 保存文件、integration `check.jsonl` 所列资料、prd/design/implement，并重新核对父任务 `automation-20261003.md` FINAL13:28 和 d03713bd 的三个 closeout。历史检查只用于明确边界，未重审 611 个补丁。本轮直接检查 0a19182c 的九个 Bar 文件、21a37239 的六个产品源/header及它们的状态、mask、诊断和启动直接依赖。

较晚 d03713bd 明确已有 20260811a Canary 包与双方中文 UI 各三轮记录，覆盖 automation 较早“Canary未找到”的说法；匹配当前中文硬笔宽度5的绘图对照尚未完成。不能把旧包 Draw2/WARP/ULW 与当前 Draw3/Hardware/DComp 的内部耗时直接对齐。

## Findings (fixed)

无：当前有界实际调用复审未发现需要局部代码修补的确认缺陷，因此没有为方便验证改变产品。

## 直接核对结果

- `Bar.Main.cpp:274` 一份局部工具 snapshot 同步传 `StateUpdate(snapshot)` 和 `ThicknessDisplayUpdate(GetPenWidth(snapshot))`。`Bar.Button.cpp:855` 同步转发给 CalcState/PresetHoming/Draw/Eraser/Geometry，引用没有保存或逃逸；Whiteboard 独立 owner 保留。GetPenWidth(snapshot) 只读提供的值，不重新锁取另一代工具。
- `Bar.Interaction.cpp:5029/5355` 删除先行 Draw style 刷新后仍进入默认 updateState=true 的 UpdateRendering，保留 FineDial 取消及预检/实际提交间变化处理。未发现因这两处删调用而漏投影或漏 Request。
- `Bar.Scene.cpp:252/269` 新指针 hover 方法与原 Interaction 同曲线、时长、opacity、即时取消和 preserveVisual；完整按钮 wrapper 保留 Divider/缺少 fill 门控及现有 noexcept 语义。RenderLoop/EraserAttribute/Scene 直接依赖继续走完整按钮 wrapper。没有增加线程或额外动画调度。
- `Bar.Rendering.cpp:1619/1737/2223` rounded、exact、geometry 三路径共用 maskDC 恒白画刷；检查全部引用未见该刷的 SetColor/SetOpacity/SetTransform。DiscardDeviceDependentCaches 同时释放 brush、maskDC、effect和缓存。缓存键、尺寸、epoch及失败处理未变化。
- 默认采样残留核对：`RenderPipeline.Diagnostics.h:12` HealthySummariesEnabled=false；IdtMain 无临时 visible-auto driver、Draw3 runtime metrics enable/export；HostStartOptions.enableRuntimeMetrics=false。最终 automation 记录已撤回的 leaf-seed/radial-support/capacity/sincos/临时 fixture 没有混入上述最新提交。
- 新 Win7 诊断属于用户明确要求保留的诊断功能。RTS RecordInputDiagnostic/ShouldRecordInputPacket 在 Cursor=false 时先返回，不新增 clock/format/队列写入；仍有有界布尔门控调用开销。Cursor GPU 在 Draw3=false 时不读时钟、不查询 COM/GPU 状态、不格式化/输出；原 Map/Draw/绑定和失败返回保持。
- ULW 新模板默认/非采样实例不做额外逐像素统计。采样实例统计原 staging 和命中底层后的 DIB，不把 alpha=1 当墨迹；copy、Unmap、dirty ULW 和原 premultiplied 检查不变。Draw3=false 不读新增时钟；失败 API 错误在日志前锁存。启用诊断的读回统计/同步 stdout 会影响采样成本，不能拿该程序 flags-on 的时间直接当正常版本性能证据。
- `IdtMain.cpp:1103` 精确 `--draw3-renderer-pixel-test` 在配置、单实例、用户数据和产品 HWND 初始化前 return，不匹配的正常命令继续原路径。测试只建永不 Show 的独立 STATIC HWND，独立 WARP renderer/resources，既有生产 shader，局部诊断开关由 RAII 恢复；不启动 ProductHost、PPT、保存 worker，不读写用户配置/笔迹。
- Pixel fixture 六个读回对照中仅四个 production case 决定成功退出，两个 blend_disabled 为解释性对照；没有以禁混合成功覆盖正式失败。每个 GPU completion/statistics 查询各有3秒限期，外部 collector 有30秒总限；资源/context清理在 HWND析构前。正常图形后台、shader字节、feature fallback、FLIP_SEQUENTIAL与数据/PPT逻辑在该提交中没有更改。
- InitializeDebugConsole 新条件保留继承的 disk/pipe 输出，两路已重定向时不 AllocConsole；仅在原调试控制台初始化被调用时可达。未新增正常启动的控制台/文件写入。

## Findings (not fixed) / 未验证门禁

没有确认的新生产代码 finding。以下是证据缺口和任务判断项，不能当作源码缺陷，也没有擅自改产品：

1. Win7 staging全零的唯一 GPU 根因仍未证明。新 CLI 的 Win11 ARM64/x64绿色不能覆盖真实 Win7；待用户明天回传 probe、应用完整日志及可见反馈。原任务继续 in_progress。
2. 代码规范任务的真实 IdtState 跨入口、重复 intent revision/bridge不重发、迟到 PPT、FineDial取消/跨代竞争尚未确定性执行。原 Headless --no-window 不编译执行 IdtState入口；静态同步 snapshot检查不升级为这些动态 PASS。
3. UI3 GetDC内部WARP执行/同步未分离，使用后偶发慢未重现，WPR受到具体policy限制；已有不稳定候选全部撤回。不建议无证据重新优化已撤回方向。
4. Canary配对硬笔绘图未完成，UI自动三轮CPU与WGC外部观察不能证明全部性能不退化；当前新HEAD与旧采样必须重新标识程序身份。此前用户授权自动对照，不转交额外用户手测。
5. Draw3旧指标精确 terminal phase proof/Laser excluded群体仍是既有边界；局部满载 allocator CPU收益不能外推首笔、整体GPU或可见帧率。

## 精确下一步

Root先完成当前HEAD的实际 Release ARM64 solution和受影响 no-GUI检查，冻结EXE SHA及配置身份；若沿用旧Debug记录，明确它是21a37239已完成的Debug检查而非新的Release结果。随后在原授权自动场景完成当前中文硬笔宽度5与已保存旧包同条件配对。另一独立有限单位为实际IdtState早期CLI验证（setMemory=false、产品初始化前退出），只有明确scope/安全合同后才新增；FineDial真实session成功提交会写盘，必须单列隔离运行，不把空状态测试冒充成功持久化。无需为上述续接结束/归档Win7任务，也不要求新commit。

## Verification

- 静态实际调用/差异：本有限范围 PASS，未发现具体新增回归；不是全产品或首发就绪认证。
- Lint：git diff --check exit0；源码未修改。项目无独立本轮lint命令。
- TypeCheck/Build：本 reviewer 未运行，依 dispatch 由 Root执行现有 solution构建；不能先写 PASS。
- Tests：本 reviewer 未运行，Root实际结果另附；旧记录只按原配置/程序身份引用。
- GUI/Computer Use/Win7/Canary绘图：本 reviewer未运行，继续各自人工或自动证据门。
