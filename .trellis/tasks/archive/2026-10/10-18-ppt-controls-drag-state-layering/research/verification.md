# 1–3项修复与验证记录

日期：2026-10-18。基线HEAD：13783fe7960135caaa0040672fca03eb2b9ebdae，分支bugfix/pptcom。

## 修复结果与证据边界

1. 拖动位置交接覆盖同pair两面和released pending；仅纯拖动位置立即落定，正常显隐、缩放与工作区动画保留。实际logical/presentation像素几何到位且呈现/窗口提交成功后才确认该面，旧帧不能提前消耗最终候选。Up采最后屏幕点，Page/background仅在同一系统阈值达到后转拖动；预览、Arrow、Whiteboard、Cancel与过期代次边界保留。
2. 中间竖向pair使用既有BarBottomDockCenterThresholdDip的40屏幕DIP，按布局屏幕DPI只换算一次，不乘控件倍率。先Clamp再试中心碰撞；中心不可行保留可行自由位置，不能把另一组推出去。没有新增配置或第二吸附常量。
3. Down的SetCapture移出非递归render事务锁，旧capture同步Cancel完成后才复核并建立新gesture/tracker；Up在锁内清理后锁外ReleaseCapture。保留原try_lock/render兜底及有效碰撞约束，并添加限频数值诊断（约束、inputLocked/门禁、pending mask、锁忙/窗口失败；沿用每client Retry证据）。用户现场仅控件停住、主栏可操作，本轮没有现场日志/栈或真实设备重现，不能断言唯一根因。
4. 页码-/-回归既有0eb1c7c5的同场busy数字、BasicControl、未知/新场次、pageAckEpoch与EndScreen门禁；未重复修改COM，未将旧数字伪造为新页ready。第4项层级策略未新增改动。

独立trellis-check完整审阅6个tracked文件，发现连续拖动raw/fit身份混用R1后，由原生产责任方修正并补真实离屏反例：tracker用D→S取得的published原始候选，runtime fit只承担几何。修正复核、构建及执行均通过，未留下阻断项。完整报告见implementation-check.md。

## 实际构建与运行

| 验证 | 实际入口/配置 | 结果 |
| --- | --- | --- |
| 完整solution/type-check | vswhere发现ARM64原生MSBuild；InkeysRepo.sln Debug\|ARM64，PreferredToolArchitecture=arm64，/nr:false；同一PowerShell先规范化PATH，监督900秒 | exit 0，29.02秒 |
| 纯策略/既有headless回归 | InkeysHeadlessTests.exe --no-window，监督600秒 | exit 0，99.48秒，PASS animation correctness |
| 产品私有实现离屏 | Inkeys.exe --bar-eraser-offscreen-test，监督900秒 | exit 0，24.11秒，[PageControlScene] failures=0，[EraserVisual] failures=0 |

完整solution实际重新编译PageControl.cpp/.cppm和page_control_tests.cpp。无本次文件相关warning/error；日志存在既有第三方hashlib++ C4267、IdtMain C4244及非致命pwsh PATH提示，未为其修改源码、依赖、SDK或项目配置。

使用已批准的正常权限runner路径，避免此前已用同命令对照确认的Codex sandbox PDB IPC/临时文件权限问题，没有改变产品代码绕过环境。监督脚本仅保存在忽略的Build/Validation下，不是新增构建系统。

原始命令、启动时间、PID、超时上限、真实退出码、stdout/stderr和artifact SHA256位于Build/Validation/ppt-controls-20261018；product-offscreen-results.log为本次共享harness完整结果副本。

## 覆盖与未覆盖

- headless四个新增函数已登记到RunPageControlTests：288个40DIP吸附组合、当前拟合倍率裁剪、碰撞回退/恢复与发布仲裁、Up阈值/点击边界、pair ownership及真实像素几何guard。直接调用生产cppm策略，没有复制吸附算法。
- 既有headless target不编PageControl.cpp，因此其通过不能充当私有Scene/AnimatedBounds/capture证据。产品既有offscreen CLI实际调用PageControl::RunOffscreenTests：真实另一线程持presentationMutex；两面移动第二面失败及0→1→0回滚；没有下一条Move的Up；released pending和立即第二次Down；真实Configure/Scene多帧bounds稳定；同步旧Cancel注入与新手势顺序；普通缩放/出现动画保留。
- 精确offscreen flag在IdtMain读取用户配置、单实例和任何正常HWND创建之前返回。harness仅使用共享WARP/D2D/字体资源，没有创建/显示窗口、真实SetCapture或启动Office。窗口移动/capture在生产接缝注入，不能外推USER32已验证。
- 真实鼠标/触摸、实际HWND轨迹/命中、故障用户设备局部停住、Office/WPP连续翻页及COM损坏设备：NOT VERIFIED。实际第4项窗口Z序也未执行GUI验证。

## 格式、范围与交付状态

- git diff --check通过；6个tracked文件UTF-8/BOM与原文件一致、CRLF保持。所有任务文本同样UTF-8/CRLF；无整文件换行或编码噪声。
- 3个编译输入相对构建前source-freeze.json SHA256未变；构建产物相对artifact-hashes.json未变。native-session-ui3.md在编译后仅补raw/fit规范说明，经独立复核；没有后补代码。
- 调查基线19个关键文件中，仅PageControl.cpp/.cppm及page_control_tests.cpp发生预期变化；另外16个保持原SHA256。COM DLL/TLB也保持原SHA256。无Window/Ppt/COM/ABI、配置或工程文件修改。
- 三份canonical规范已记录最终位置提交、Up终点、40DIP、capture重入及raw/fit身份规则；context摘录已同步当前规范。
- 用户只批准1–3实施；本轮未commit/push，任务保持in_progress，未归档。后续设备反馈应沿上述未验证矩阵继续，不将本轮自动验证写为实机已确认。

## 后续修订状态

本报告以上结果仅对应首轮40DIP实现。用户随后追加20DIP双条件及UI3吸附动画，当前修订需重新构建/运行；最新结果将记录于snap-revision-verification.md，本报告不作为修订已通过的证据。
