# 执行计划

## 已完成的调查阶段（历史记录，未实施行为修复）

- [x] 建立独立任务与PRD，记录本轮授权和不修复范围。
- [x] 用ffmpeg分析全片和约44–53秒，核对末段清除与持续输入。
- [x] 三个Trellis研究方向：真实输入/准入、设置/窗口生命周期、呈现/取证；补充RTS恢复与历史。
- [x] 原生Win32脚本在隔离副本采集GUI线程focus/capture/owner、WCT和真实鼠标绘制基线。
- [x] 识别初始Debug二进制早于HEAD，单独记录结果，不把它当当前HEAD结论。
- [x] 完整构建当前HEAD Debug|ARM64，退出0；SHA256=9316b86f5d08f0135b8e583fe490211fbcfb59f1c5c0fa055a3abea8865347fd；--no-window退出0。
- [x] 解锁后的当前HEAD GUI基线完成：实际落笔、设置正常显示和真实关闭按钮、Explorer/选择往返。完整高频/真笔触摸矩阵未做。
- [x] 扩大三方向审计并验证F1同步输出阻塞：完整产品两次触发、三次栈采样、设置页仍响应、同进程恢复读端后继续绘制。
- [x] 验证F2真实PPT worker共享冲突与原码恢复闸门，独立ARM64 probe编译/运行0。
- [x] 验证F3真实Touch coordinator/SpeedEraser与原码等待循环，独立ARM64 probe运行0；Up能解除。
- [x] 汇总新证据和修复边界，独立交叉审阅；检查diff、格式和任务链接。
- [x] 第三轮复现F4隐藏Settings capture残留并以同进程取消捕获恢复工具点击；新笔仍正常，严格区分工具输入与画布冻结。
- [x] 三轮无诊断完整产品鼠标窗口流程、Win+D/Win+E边界、owner对照及200+200自然交错；18组真实OS/RTS鼠标/注入触摸边界记录完整Down/Up。
- [x] 完整产品触摸补测记录Pointer到达但未建立RTS/绘制基线；保留日志连续区间与丢记录的限制，没有宣称触摸窗口流程通过。
- [ ] 用户现场与F1/F2/F3/F4匹配尚待同场景故障证据，不能宣称共同根因已确认。

## F1–F4 实施与检查顺序（最终规划审阅后执行）

1. 父任务最终摘要由后续用户消息确认；这一份确认覆盖四个子任务的当前PRD/design/implement，不逐项重复询问。用户已在随后消息中明确批准，本项门已满足，父及四子任务均已start。
2. 在当前工作树复核基线与并行改动，记录各文件原编码/BOM/换行。读取各子任务implement.jsonl、check.jsonl；优先使用native Codex上下文注入，无法注入时由子代理完整加载清单。
3. 按Trellis默认子代理模式实施和检查。F1实现者负责标准流sink及其接线/回归；F2/F3同一实现者依次处理持久化分类与idle通知，避免Controller共享文件冲突；F4实现者负责Settings/Window Service/session回归。主会话保留最终共享文件集成权，不覆盖别人修改。
4. 四项分别按自己的最小合同提交可审阅工作区改动；check子代理核对真实生产函数、竞态、退出、文件身份和capture所属线程。局部机械问题可自修，超范围设计问题回主会话；无git commit/push。
5. 主会话串行执行完整InkeysRepo.sln Debug|ARM64；动态发现ARM64 MSBuild，同invocation规范化Path/PATH并关闭node reuse；至少允许五分钟。禁止多个worker同时构建同一缓存目录。
6. 执行InkeysHeadlessTests --no-window；复用 --draw3-ppt-current-load-retry-test、--draw3-pending-topology-load-test 等与修改匹配的生产CLI，增加F1/F3真实机制回归；坏参数不得进入普通GUI。旧摘录probe可以作红绿对照，不能作为唯一修复验收。
7. F1代码集成后再做开启诊断的GUI实验。沿原停读管道触发保持读端暂停，确认新笔迹与工具继续、真实关闭能退出；正常sink/恢复读取也检查输出。保留二进制hash、退出码、阶段及窗口状态。
8. 无诊断完整产品重复F4 Down→Alt+F4→Up和普通X/重开，检查捕获及同位置工具业务切换；再做设置/Explorer/选择往返、新线、橡皮、Clear/Undo。GUI通过原生程序化输入串行执行，不使用Computer Use，不扰动用户安装或已有窗口。
9. F3真实触摸/笔硬件与完整产品触摸基线未建立的限制单列，不能用隐藏输入额外wake代替；Win7和多屏未运行也逐项保留。F2文件回归与真实Office联动作用域分开报告。
10. 主会话在有实测结果后按trellis-update-spec / break-loop记录可执行合同；最终检查最小diff、编码/换行、源码/测试对应、task manifest和四项结果。不擅自归档产生自动commit；未完成的原用户现场归因继续保留。

## 子任务与依赖

| 需求 | 子任务 | 功能依赖 | 执行约束 |
| --- | --- | --- | --- |
| F1 | ../10-15-canvas-f1-diagnostic-output-fix | 无 | 开诊断的集成GUI前先合入；专用轨迹文件协议不在范围 |
| F2 | ../10-15-canvas-f2-ppt-load-retry-fix | 无 | 保留现有四次预算；与F3共享Controller变更顺序整合 |
| F3 | ../10-15-canvas-f3-eraser-idle-wake-fix | 无 | 同一实现者在F2后处理共享文件；不能由隐藏注入附加wake代验 |
| F4 | ../10-15-canvas-f4-settings-capture-fix | 无 | HWND owner清理与ImGui session失效分别验证；GUI与其他组串行 |

父任务负责最终集成和原现场调查边界。四个子任务已随父摘要批准进入in_progress；没有通过树位置暗示功能依赖。

## 本轮构建与验证入口

使用vswhere动态定位ARM64原生MSBuild。同一PowerShell invocation先执行仓库规定的PATH规范化和MSBUILDDISABLENODEREUSE=1。

首轮已核对本机vcpkg元数据的10个override版本，实际构建暴露Abseil头/库缺失，链接失败。随后核对主工作树完整provider的同版头/库、ABI与ARM64，以本次命令行VcpkgInstalledDir读取该provider，VcpkgManifestInstall=false，不改项目/SDK/依赖版本。首轮日志solution-head.log，后续solution-head-shared-provider.log，均在Build/CanvasFreezeInvestigation，退出码单独记录。

原生GUI脚本：research/native_probe.py，补充research/touch_inject_probe.py仅用于真实OS触摸注入。独立窗口实验为research/window_owner_capture_probe.cpp与research/rts-window-boundary-probe.cpp，编译产物在window-probes，完整证据索引见research/window-interaction-investigation.md。只操控本轮隔离副本，屏幕/输入采集使用已授权的正常桌面执行权限；沙箱屏幕采集Access denied属于环境隔离，非产品故障。

运行期证据目录：Build/CanvasFreezeInvestigation。测试生成物不纳入产品或配置源码。

## 回退点

实施前基线产品源码干净；当前按批准方案分工修改。后续四项改动按子任务分别保持可回退；运行测试只关闭能确认归属的隔离程序/Explorer，保留原有用户进程；不修改用户原始安装目录、配置或原始MP4。

## 本轮实际验证（2026-10-15）

四项补丁及独立检查已完成；最终主 Solution Debug|ARM64 和独立存储目标均退出0。F1真实生产背压自检、F2两项生产CLI、F3控制边界、既有存储矩阵、Headless --no-window通过。用户解锁后，同一最终EXE的F1 stdout背压完整产品鼠标GUI通过，停读时新墨迹/擦除/Clear/Undo/设置仍响应，恢复输出后再停读，真实设置关闭约0.9615s正常退出；F4无诊断强触发四轮、普通X、重开首次切页、Explorer/选择往返及按住退出通过，补测退出约1.2343s。首轮F4验证器600秒清理保留为forced，不算正常退出通过。默认窗口整套复测exit1/11 fail；HEAD A/B仅证明预置顶owner/Z序条件已有，不替全部失败归因。独立真实capture回归exit0。F3物理触摸/笔、F2真实Office、Win7、多屏、console/QuickEdit及原用户共同根因未确认。准确命令、SHA、原始日志和限制见父任务 research/fix-validation.md。无commit/push，任务不归档。

## 2026-10-17 用户调整优先级

用户要求先提交已完成的 F1–F4 改动。默认窗口套件的 11 项 owner/Z序失败保留为后续暂缓事项，本次不继续修复；随后优先调查新反馈：PPT 翻页后画布卡死。该调查应覆盖翻页事件、页面目标、加载完成及输入准入完整链路，既有 F2 冷槽 IoError 实验不替代这一新现场的归因。新方向仅调查，不默认实施额外行为修复。


## 2026-10-17 执行更新

- [ ] PPT target reconciliation 修复及真实共享逻辑无窗口红绿回归。
- [ ] 原 11 项窗口失败的源码归因、最小修复和编译验证；本轮禁止窗口运行回归。
- [ ] 画布准入恢复矩阵，修复确认的持久等待缺口并保留数据安全门。
- [ ] 独立 Trellis 检查，主会话串行完整 Debug|ARM64 solution build 与 --no-window / 明确无窗口 CLI。
- [ ] 更新本轮实际结果/未覆盖项，不创建新的 commit/push，不归档尚有现场验证的父任务。


## 2026-10-17 本轮结果

PPT目标交错、同页暂停回执、PPT存储锁等待及Setting带内排序已实现并独立审查。完整solution、存储目标编译及无窗口回归全部通过，见 research/followup-validation.md。原11项HWND断言未运行，其中3项根排序仍未定位；父任务保持in_progress，不将这几项标成完成。所有GUI/Office/真实输入设备验证延后；本轮未commit/push。


## 2026-10-18 收尾授权更新

用户现已开放电脑操控，要求完成剩余任务并收尾。覆盖先前禁止窗口的限制；允许完整HWND测试、隔离产品GUI、真实PowerPoint测试。无新增commit/push授权。窗口测试/GUI串行，由根会话统一操作；沿用现有补丁和证据，不复写用户改动。最终验收以重新构建产物为准。

## 2026-10-18 已完成验证与最后门

- [x] PPT/桌面索引锁真实worker超时、释放后恢复及非空旧墨迹保护；存储目标编译和两组测试退出0。
- [x] PPT accepted终态预留、worker异常原身份、无分配发布/取出与Host FIFO交接，真实operator new注入和生产CLI通过。
- [x] 当前无诊断构建完整Headless --no-window与生产PPT retry/topology/retained/control-fence回归通过。
- [x] 真实产品设置强触发三轮、首次重开点击、普通X、Explorer/选择往返、实际擦除、Clear/Undo；详见research/gui-ppt-validation-20261018.md。
- [x] 真实PowerPoint普通/快速/持笔翻页、结束页返回、放映重入及Inkeys分页按钮；每步目标/UI回执及新像素验证通过。同页黑屏未触发Host suspend，不替代同页暂停组件证明。
- [x] 窗口实际band遗漏和可见Setting刷新回归已修复；最终清理后重建12轮专项、默认全套及同最终产物GUI通过，保持严格全树断言。
- [x] 最终编码/换行、最小diff、独立审查及任务元数据已核对；无commit会话记录见workspace。


## 2026-10-18 最终验收

最终solution Debug|ARM64 exit0；同最终Headless12轮窗口专项和默认完整suite全绿；最终产品6项正常CLI exit0、坏参数2；PPT/Desktop真实存储目标测试exit0。最终console-off GUI在周期刷新后实际设置capture正常、隐藏后Up前释放，落笔/idle橡皮/PPT持笔翻页/Explorer/设置首次点击及按住正常退出通过。独立源码复核通过。统一SHA、原始失败及最终证据、设备和资源边界见research/final-validation-20261018.md。上文10-17禁止GUI及未解决根三项为历史，不再是当前状态。本轮工程验收结束；不新增commit/push、不自动归档。
