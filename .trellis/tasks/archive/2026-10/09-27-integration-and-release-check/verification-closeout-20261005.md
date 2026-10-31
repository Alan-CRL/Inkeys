## 2026-10-06 后续更正

本文件保留Oct5构建/检查/资源/GUI实证；通知阻挡后来解除。最新实际自动对照见verification-closeout-20261006.md。双方150ms反向序列的有效工作量因既有300ms门控不相同；本文“7次成功点击”仅指输入注入成功，不能升级为7次业务toggle。350ms一次复测亦未通过端点检查，完整性能不退化继续未达目标。冻结F81F仍有效，不能表征随后Win7线程的新脏源码；本轮无生产/测试代码或构建变化。

# 首发剩余事项收口（2026-10-05）

本轮按用户最新范围停止新性能优化，复用已有自动操控/检查；不处理独立 Win7 ULW，不 commit/push/发布。主目录 chore/publish 实际 HEAD 为 8d15d05119b85777d8b0aabac4c99930cf438045；近期粗细改动与人工验收已提交。预存 Win7 源码、规范、任务记录和四份 CSO 均保留。

## 冻结事项与结果

| 事项 | 结果与证据 |
| --- | --- |
| 有限规范/安全复审 | 近期 DIP、标签、触摸预览和预设高亮沿用三份独立审查，再核直接状态/资源/发布依赖；没有新的确认源码缺陷。见 research/release-incremental-review-20261005.md。不重审611历史集合。 |
| 实际 Release 构建/资源/包 | 已完成并验证，见下节；这是本地未签名验证包，不是正式CI发布。 |
| 必要逻辑与GUI回归 | 状态、粗细、Draw3粗细、Headless、PptCOM均PASS；真实Release主钮三轮及属性、书写、擦除/撤销、Laser操作已自动执行，正常退出。设置绘制的捕获限制保留。 |
| 0811 性能对照 | 当前最终程序三轮已执行；新0811配对暂被ShellExperienceHost通知遮挡，guard未发送输入。旧同机三轮数据继续有效，但完整可见帧/延迟“不倒退”没有可信的共同测量证据，不能标目标达成。 |
| Win7 ULW | 按用户要求由另一线程独立处理，本会话不审其根因、不修改或宣称通过。 |

## 构建、程序身份与包

- 完整 InkeysRepo.sln、Release|ARM64、VS安装动态定位的 ARM64 原生 MSBuild；同PowerShell先规范化PATH及禁用节点复用，CL=/DIDT_RELEASE。完整Rebuild保留每项目原IntDir，使用本轮新OutDir；后续Build重编主程序和恢复依赖。产品源开关仍注释，未改项目/SDK/依赖或全局环境。
- 首次 Rebuild exit1：共享自定义OutDir时，Inkeys的既有CleanPptCOM把先构建的PptCOM.dll清掉，PptCOM.Tests随后CS0006。第二次同solution /t:Build exit0，恢复依赖而无源码/配置补丁；这是本次输出目录/调用顺序问题，不包装成产品修复。release-build.log/.exit、release-dependencies-build.log/.exit完整保留。
- 最终 EXE：Build/automation-perf/release-closeout-20261005/ARM64-Release/Inkeys.exe；SHA256 F81F790F7DABEA30A1F230F58E03B5DFB991710A57DFCA02E2CDB0F52D5092E9。ARM64 PE32+，实际8字体、3语言JSON、ImGui两个shader、Draw3 301–304、manifest、PptCOM DLL222均存在；嵌入DLL哈希等于本次实际DLL。
- source-before.json记下HEAD及11份预存脏文件/新头文件哈希；source-after-checks.json再次核对无变化。该EXE包含当时未提交的Win7诊断差异，不能只用HEAD表征；独立修复之后需在其实际最终源码上重建对应发布版本。
- 完整Rebuild调用环境和实际PE中release-only Inkeys_互斥体字串共同支持宏路径身份；CL.command.tlog未记CL环境宏，不能声称tlog单独证明。未另测单实例/更新动作，未把正式CI源标记门、版本元数据、三架构、签名和上传算PASS。
- 本地 InkeysArm64-validation-20261005.zip，SHA256 5187B1DDC2DA9B0DDA4757C9464767284BE8CBE2E5A1FED424F3580FC494097C；9项含EXE/Tips/LICENSE/NOTICE/完整ThirdpartyLicenses，ZIP校验及提取EXE哈希一致。artifact-manifest.json为身份/资源/包依据。

## 最小验证

checks.json记录真实argv、cwd、退出与耗时。--state-mode-production-test、--pen-width-production-test、--draw3-pen-width-test、InkeysHeadlessTests --no-window、PptCOM.Tests全exit0；state附加参数为预期exit2；六个空cwd均0文件。Headless输出216 layouts/0 failures和PASS animation correctness。复用既有Debug及用户2026-10-04 GUI/Office、2026-10-05粗细人工验收，不重跑全CLI/全部架构/Office矩阵。

实际GUI运行私有current/canary根，仅复制配置，未复制原笔迹/自动保存；只关闭私有自动更新并统一中文，动画速度1、光影开启、Bar.Zoom=1、DPI192、2880×1920/60Hz。现版UI3实际日志WARP/ULW/物理zoom2；Draw3画布NOREDIRECTIONBITMAP而非LAYERED，支持正常DComp路径，但本次无新增运行期适配器枚举，不沿用Bar WARP替代Draw3结论。先前Adreno X1-85硬件记录仅历史参照。

当前main-fold每轮22秒、7次输入注入（150ms反向、末尾闲置11秒再操作），注入全部成功，业务接受未逐条核实，captureError=0；产品累计CPU 2109.375 / 2250 / 2015.625 ms，中位2109.375，范围2015.625–2250。WGC交付47/44/48、主钮ROI各13次变化；GDI每轮31→31，USER首轮58→59后稳定。这些不是Present/FPS/光学延迟，不能由交付数/累计CPU认证“不倒退”。当前普通工具初始与操作后截图未见主栏裁切或笔迹残留异常，不认证逐帧视觉一致。

属性粗细3→6预设与高亮截图相符；正常软笔三笔可见，擦除/撤销后恢复原三笔，Laser操作后仅见原笔迹与指针光点；轨迹与原测试笔迹重合，本轮不认证独立消退时序。此为功能操作/截图，不是书写时延对照。切入画笔模式时全窗口捕获器因尺寸改变退出2（ROI dimensions changed）；换用现有固定ROI继续成功，未改产品或放宽测试。退出捕获器因程序正常结束退出2（target process exited），实际日志确认所有线程正常结束；不能归为产品崩溃。设置WGC为透明背景，Computer Use亦显示背景且无控件树，与双方旧记录一致，本次不验证其绘制、不操控背景QQ/登录。

旧 --ui3-presentation-benchmark 是有界后台动画驱动，但隐藏窗口、216目标第二目标More/SVG Overwrite旧exit90在measured开始前失败，且0811无相同CLI/raw数据。研究确认适用边界，未再重跑、跳过目标或放宽断言；不新增大型benchmark，不恢复缓存容量、A8或低收益sin/cos候选。

## 差异与保留门禁

本轮生产代码/测试代码/配置修改为零；仅现有任务的研究/验证/交接、完成状态和journal记录。忽略目录保留原始CSV/JSON/BMP、日志、二进制和本地包，不清理归属不明文件。

代码规范三项原AC已有生产逻辑、正常副作用人工验收与独立构建证据；有限工程收尾完成。安全覆盖与findings收口及本轮增量复审完成，用户保留的HTTP回退/来源认证风险与未运行的故障链不标已修。完整性能不退化、独立Win7修复/目标机验收、正式发布CI/签名/上传门仍分别保留，不把整个首发任务标全部完成。

最终Canary通知遮挡未解除，尚无本轮配对样本；确认PID28876属于私有根、标记存在且Inkeys下仅Config（无笔迹）后结束该空白测试进程，所有文件保留。当前程序则正常退出，二者不混称。gui-results.json为最终操作/CPU/阻塞/清理汇总。当前源及预存Win7记录仍与build前指纹一致（journal另由本会话有意追加）。
