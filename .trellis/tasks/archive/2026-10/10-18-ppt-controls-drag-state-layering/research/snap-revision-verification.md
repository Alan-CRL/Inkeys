# 20DIP双门槛与UI3吸附修订验证

日期：2026-10-18。用户在首轮1–3实现后追加授权，覆盖初版40DIP方案。基线HEAD仍为13783fe7960135caaa0040672fca03eb2b9ebdae，分支bugfix/pptcom；未创建新commit。

## 最终行为

- 只有中线Y偏移绝对值<=20屏幕DIP，且主体距对应侧边扣除既有间隙后的额外距离<=20屏幕DIP，才尝试中轴吸附；两条件含等号。复用BarBottomDockThresholdDip，阈值只按布局屏幕DPI换算一次，不乘控件倍率。middlePairWidth已表达扣间隙后的额外距离，阴影不参与，也不强制X归零。
- 仅吸附Y采用140ms UI3 PageControl既有cubic曲线/RenderPipeline过渡，两面共享时刻，X仍跟随。入口从两面当前实际Y开始，避免先跳到raw位置；一次大跨度输入直接进带也只运行同一次140ms。带内Move、页码发布、Up不重启，完成后不重播，离带即时恢复自由拖动。
- 中间帧不确认最终pair，released pending等两面实际几何与呈现/窗口提交成功才结束；吸附不新增inputLocked。真正Cancel/新手势/失效session等可清退，Up触发的无活动pointer旧CaptureChanged保留刚释放的吸附。
- 普通自由拖动及非吸附fallback维持此前直接落定；raw/fit身份R1、Up最终采样、锁外capture及页码门禁修复保留。COM/ABI/DLL/TLB、Window/第4项层级、Bar自身20/40行为、配置和工程文件均未改。

## 独立审查补正

审查发现R2：RenderPipeline批次frameTime可能早于owner刚采样并直移的实际时间，最新revision也可能被旧时间算出的Y拉回。生产最小补正为pair共享sampledAt=max(传入时间,已有sampledAt)，由既有T锁保护；started及140ms终点不变。

真实Scene用例执行owner60ms更新X/Y→同一最新revision的旧batch40ms Configure→80ms，不跳过旧批次，断言两面Y不回退、X最新、started不变，再执行Up85ms和原140ms终点。独立检查者复核修正后，没有其他遗留阻断项。详见snap-revision-check.md。

## 本修订实际构建与执行

| 验证 | 配置/参数 | 实际结果 |
| --- | --- | --- |
| 完整solution/type-check | InkeysRepo.sln Debug\|ARM64；vswhere发现ARM64原生MSBuild，同一PowerShell规范化PATH，/nr:false，PreferredToolArchitecture=arm64，900秒监督 | exit0，29.95秒 |
| headless纯策略与既有回归 | InkeysHeadlessTests.exe --no-window，600秒监督 | exit0，77.67秒；PASS animation correctness，stderr为空 |
| 生产私有实现离屏 | Inkeys.exe --bar-eraser-offscreen-test，900秒监督 | exit0，23.66秒；[PageControlScene] failures=0，[EraserVisual] failures=0 |

本轮确实重编PageControl.cpp/.cppm和page_control_tests.cpp。无本次文件相关warning/error；日志仍有既有hashlib++/IdtMain warning及非致命pwsh PATH提示，未为环境或第三方问题修改项目、SDK和依赖。

使用已批准的正常权限runner，规避此前已用同命令对照确认的Codex sandbox PDB IPC/临时目录问题，没有修改产品绕过环境。首轮40DIP日志保留，未复用为本次结果。

## 测试覆盖与边界

- 1152策略组合直接调用生产ResolvePptDragCandidate/ResolveSurfaceLayout：2屏幕×3 DPI×4倍率×2侧×正负Y×19.9/20/20.1×侧距19.9/20/20.1/120；检查双条件、含等号、间隙只扣一次、X保留和远离侧边不吸附。中心碰撞/可行自由候选及raw碰撞/中心可行夹具均已按新门槛重新验证。
- 真正生产离屏调用私有Begin/Update/Finish/Configure、AnimatedBounds/Scene与提交仲裁，覆盖起点/中间帧/单调/双面同步、X即时、持续Move/发布/Up不重启、旧候选拒绝、单面成功仍pending、完成不重播、离带/Cancel/新手势/场次、真实另一线程持P及第二面非零X失败0→1→0回滚后的收敛；原R1和自由拖动回归保留。
- headless工程只编cppm；它通过不代表cpp私有动画已执行。生产精确offscreen flag在正常配置/单实例/HWND启动前返回，harness只用共享WARP/D2D资源，窗口移动与capture在生产接缝注入；本次没有创建或操控真实窗口、启动Office或执行Computer Use。
- 真实鼠标/触摸、USER32捕获交错、实际窗口命中/Z序、故障用户设备局部停住、Office/WPP及COM损坏设备仍NOT VERIFIED。离屏通过不能替代实际观感及设备验收。

## 范围、格式与证据

- 6个tracked文件（两份PageControl生产实现、一份headless测试、三份规范）相对snap-source-freeze.json的SHA256未变；新构建EXE和COM artifact哈希记录于snap-artifact-hashes.json。
- 调查基线19个关键文件中，只有上述3个C++文件按预期变化，其余16个及COM DLL/TLB保持基线SHA256。UTF-8/BOM/CRLF和git diff --check通过；canonical规范及注入摘录已同步当前20DIP/140ms/单调进度合同。
- 新日志与监督结果位于Build/Validation/ppt-controls-20261018/snap-*.result.json、对应stdout/stderr和snap-product-offscreen-results.log；完整范围/格式证据见snap-final-scope-format.json。
- Trellis context validate通过，任务保留in_progress。没有commit/push或归档；前任务的提交授权不延续。
