# 发布前有限增量复审（2026-10-05）

Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 只读近期已提交的粗细增量及直接发布依赖；只写本报告，不运行构建、测试、GUI，不改产品/spec/Win7 工作区，不 commit/push/归档。完整构建和自动窗口验收由主会话独占。

## 范围与结论

- 当前 HEAD `8d15d051`，分支 chore/publish。检查 `79e5fe08` 的 DIP 及显示上下文、`f8054a22` 的整数标签/触摸预览、`849e7107` 的 ExplicitDip 高亮；后续两个提交仅记录/归档该子任务。
- 已读本任务 check.jsonl、prd/design/implement、保存 hook 中适用规范、当前 pen-width/build 合同，并复用 archive/2026-10/10-04-pen-width-dip-defaults 的 implementation-review、followup-review、preset-shape-review 和最终验证。原始历史审查不重新执行。
- **没有发现新的范围内确认源码缺陷。** 原三轮独立审查修补及最终完整 Debug|ARM64/真实 production probe 均有 exit0 记录；子任务 task.json 为 completed、manual_acceptance 为 passed_user_reported（2026-10-05）。这不外推为所有架构、Win7 或设备组合已人工运行。
- 当前 IdtMain/presenter/hidden-test/input spec 等脏改动属于独立 Win7 任务，不是上述已提交增量；本报告不审查/覆盖它们。最终二进制若包含这些改动，身份必须由主会话记录，不能以 HEAD 单独标定。

## 直接代码路径核对

1. State 的 Selection/Context 同锁读取，宽度 setter 检查 revision，解析兼容缓存后解锁再保存及发布 bridge；Context 等价更新不重设显式宽度。粗细的唯一策略是 PenWidthPolicy，Draw3 在 CanvasDiameterForTool 边界转物理像素一次；Down 保留样式，不改 UInk 既有物理几何。
2. 主启动在输入准入前准备 Context，Host 以实际绘制目标同步回调；InitializePenWidthPreferences 只首次 Medium，已注释禁用启动记忆恢复，后续保留会话值。future schema 与 legacy codec 的保存保护保留，没改 HTTP/PPT 保存政策。
3. Layout/hover 共用 IsBarThicknessPresetSelected：Preset 身份优先，ExplicitDip 用严格 double 相对容差且只选首个重复 fallback；不按整数文字吸附。形状槽保持独立。标签只显示正数整数，不反写状态。
4. 活动候选经共用 metrics 到实际动画 val 的 SetDirect，并将 thicknessPreviewDragging 纳入既有 MarkChanged；非交互保持 SetTar，未新增刷新循环。旧三轮审查的类型/单位/失效修补已经进入提交。

## 正常发布配置和产物

- `Inkeys/IdtMain.h` 的 `IDT_RELEASE` 仍注释；工程 Release 的 NDEBUG/静态 CRT 并不等于该产品宏。CI 在 validate_release=true 时只接受该头文件精确 `#define IDT_RELEASE`，然后普通完整 solution Release build；不存在现成替代的正式宏参数脚本。
- 同 invocation `CL=/DIDT_RELEASE`、独立 IntDir/OutDir 的完整 Release 构建可以验证既有正式分支而无需修改并行源码；必须真正编译新对象。启用单实例/自动更新及禁用调试控制台是既有产品差异。GUI 仅私有配置关闭 AutoUpdate 以冻结身份，不修改用户配置。该本地检查不能宣称 CI 源标记门、CI 版本元数据、签名或实际上传已通过。
- 实际 rc 登记 PptCOM DLL(222)、manifest、8 份 TTF、3 语言 JSON、ImGui 两 shader、Draw3 shader 301–304。PptCOM 在完整 solution 依赖链导出 TLB、复制 DLL/TLB；PptCOM manifest 使用 CLR v4.0.30319，Office interop EmbedInteropTypes=true。主 C++ 静态 CRT/vcpkg 静态依赖没有因本增量改变。最终 PE 必须确认实际资源存在和正常启动组件成功，不能只凭源登记判产物有效。
- CI 完整 ZIP 路径将 EXE/Tips 与 LICENSE、NOTICE、ThirdpartyLicenses 同置；复制 ErrorAction Stop，更新 ZIP 仍仅 EXE。有限本地 dry-run 可以核 ZIP 内容，不替代实际三架构正式 CI/签名包。旧依赖风险沿用已有审查，不进行新公告/许可大审计或升级。

## 必需补验与边界

只补最终正式宏 Release 的完整构建及程序身份/嵌入资源、必要 State/粗细生产入口、现有自动窗口场景和隔离完整包内容；已有 Debug/人工记录足以覆盖近期算法增量，不重跑全部 CLI 或历史矩阵。性能结论须来自对应最终程序；回调、WGC 或离屏数据不能改称光学输入延迟。当前 finite harness 能否用于 0811 要由主会话确认，旧版无同等生产 probe 时保留指标边界。

Win7 ULW 独立修复尚未验收、正式 CI 发布门、未取得的驱动/Office/设备矩阵仍需分别列明，不作为本报告发现的新代码问题。本轮用户已报告普通 GUI/Office 人工通过，不重复要求用户手测。

## Verification

- Lint：本 reviewer 未运行（只读分工）；已有子任务 diff/i18n 检查通过，无独立 C++ linter。
- TypeCheck：复用 preset-shape-final-build.result.json 的完整 Debug|ARM64 exit0；本轮正式宏 Release 等主会话结果后追加。
- Tests：复用 preset-shape-headless/production/state/draw3/bar-offscreen 最终 exit0 和用户人工反馈；本 reviewer 未重复运行。
- Findings (fixed)：无；没有编辑生产代码。
- Findings (not fixed)：上述发布和测量证据门；未确认新源码缺陷，不硬修或扩大历史任务。

## 最终程序及主会话证据复核

证据根 `Build/automation-perf/release-closeout-20261005/`。本 reviewer 直接读取 artifact-manifest/checks、构建退出文件和关键日志、各生产入口输出、窗口 summary/input CSV、正常退出日志及实际 ZIP entries；只读计算当前 EXE 和预存文件散列，没有运行新测试。

- 实际程序 `ARM64-Release/Inkeys.exe` SHA256 **F81F790F7DABEA30A1F230F58E03B5DFB991710A57DFCA02E2CDB0F52D5092E9**，与 manifest 相等。HEAD 8d15d051 加 source-before.json 所列 11 个预存差异共同限定本轮源码身份；复核这 11 项当前字节未漂移，不把它写成纯净 HEAD 构建。Win7 差异仍属于另一线程，不由这里宣布修复通过。
- 主会话以原生 ARM64 MSBuild、完整 solution、CL=/DIDT_RELEASE、独立 OutDir 执行 `/t:Rebuild`。保留各工程原独立 IntDir 并 Clean/重编与新 IntDir 的缓存隔离目的相同；日志实际重编 IdtMain、Bar、Host 等，不因仅更换 OutDir 沿用旧对象。首次 exit1：PptCOM 先生成共享 OutDir 的 DLL，Inkeys CleanPptCOM 随后删除它，PptCOM.Tests 出 CS0006；不是本次生产粗细源码错误。相同正式宏 `/t:Build` 修复依赖顺序后完整 exit0，日志实际生成 Inkeys/PptCOM.Tests/Headless；没有改源、升级或隐藏该首次失败。
- 宏证据应准确限定：主会话同 invocation CL 参数加 PE 中真实 release-only `Inkeys_` mutex 字串；CL.command.1.tlog 有实际主 TU 等编译记录，但 **不记环境 CL 中的 IDT_RELEASE**，不能引用它单独证明宏。源码 IDT_RELEASE 标记仍注释，正式 CI 标记门和版本元数据仍未代办。
- 五个正向检查实际 exit0：state、width、draw3-width、headless --no-window、PptCOM.Tests；额外参数 state-extra exit2。checks.json 的各隔离 cwd filesInCwd=0。实际日志有跨入口/迟到/FineDial PASS，preferences/state PASS、连续 DIP->像素/Down/cursor/history PASS、216 eraser layouts failures=0 与 animation correctness PASS、PptCOM descriptor/session/owner PASS。没有删除/放宽检查。
- PE manifest 实际包含 DLL222(67584B、匹配源 PptCOM)、8TTF、3JSON、ImGui shader299/300、Draw3 shader301–304及manifest221；非空尺寸和散列已记录。隔离完整 ZIP 的9个真实 entries 为同一 InkeysArm64 根的 EXE/Tips/LICENSE/NOTICE/5份 ThirdpartyLicenses，解出 EXE 字节相等；ZIP SHA256 `5187B1DDC2DA9B0DDA4757C9464767284BE8CBE2E5A1FED424F3580FC494097C`。这是 unsigned ARM64 本地验证包，不是三架构正式签名 CI 产物。
- 当前实际 Release 三轮 UI3 均为7次输入注入成功，业务接受未逐条核实，WGC delivered47/44/48、ROI changed各13，captureError/actionFailed/resize均0；书写、擦除撤销及Laser窗口 summary也无捕获/输入错误，主会话有粗细3→6/高亮等截图。真实程序日志记录结束所有线程并关闭。普通 GUI/Office 用户人工通过继续有效；这些自动数据不外推为所有硬件触摸/Office版本覆盖。Settings 的透明捕获旧限制仍保留，不作为本增量确认缺陷。
- 截至本报告复核，0811本轮主按钮采样被 ShellExperienceHost 通知挡住，保护拒绝发送输入；无对应成功样本可作最终 PE 的前后比较。旧515AB/WGC/累计CPU数据不能升级为这份 F81F790F 最终程序的光学延迟、可见FPS或完整“不倒退”通过，留给主会话完成/如实记录环境门。

本次有限规范/直接安全审查未新增确认必须修复的源码问题；无需新增生产补丁或重复历史审计。编译、可得CPU/功能自动验证及本地产物核验已由上述实证补齐，剩余正式发布/Win7/缺失呈现指标仍分别保留。
