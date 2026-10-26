# 动画连续性修订验证

日期：2026-10-24。基于首轮commit aeb178bc继续同一Trellis任务，本轮不自动commit/push，不结束或归档任务。设置字体和GUI测试均排除。

## 最终结果

| 验证 | 结果 | 证据（.trellis/.runtime/ui-font-pixel-rendering/） |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug\|ARM64 | exit0，99.24秒，0 errors / 130 warnings | solution-build-animation-verified.log及.result.json |
| 新Headless --no-window | exit0，61.89秒，PASS animation correctness | animation-headless-final.stdout.log及.result.json |
| 新Inkeys --bar-eraser-offscreen-test | exit0，20.97秒，EraserVisual及PageControlUI3 failures=0 | animation-offscreen-final.*、Build/eraser-b/offscreen-results.log |
| 三语sync/check | en-US/zh-TW各437/437通过 | Scripts/i18n.ps1输出；仅说明改为静止字号/动画连续 |
| diff/编码/BOM/换行 | 通过 | 最终tracked改动逐项与git cat-file --filters HEAD:path核对 |
| Trellis完整审查 | 已修复所有具体发现 | research/animation-check-review.md |

MSBuild由vswhere动态查找ARM64原生程序，完整solution、Debug|ARM64、/m:2、/nr:false、PreferredToolArchitecture=arm64，15分钟预算；同一PowerShell先规范化PATH并禁用node reuse。沿用本任务已验证的沙箱外构建方式，未修改solution、项目、SDK、工具链或第三方源。vcpkg applocal的pwsh查找失败后按现有targets回退系统PowerShell成功，不影响exit0。

最终Headless二进制UTC时间为2026-10-24T12:41:27.5248968Z，产品EXE为2026-10-24T12:42:25.2690961Z，均晚于对应最后源码修改；之后不再改产品/测试源。测试TEMP/TMP仅指向本任务workspace scratch；最终Headless预算600秒，离屏300秒，均未超时。

## 有效回归与边界

- 生产c动画类及生产目标helper：13→15DIP、zoom=0.6，7→9px两端整数，中间小数连续；零dt中途重定向、同帧重复采样、重复目标不重启、末帧到idle无补floor、raw中点/tar不变、禁动画/forceReplace及T=0退出。
- 真实Button/D2D：按压和释放的连续轨迹、按住12px/释放13px端点、同帧双draw只推进一次、内容中点和Back过冲不改c目标。13px目标在当前press=0.95等FLOAT矩阵下恒定，不受float矩阵与double比值抵消影响。
- PageControl真实无HWND回调：先明确断言baselineIdle，再只改字体政策；返回Continue、多帧提交，最后回Idle，证明c尾段未被绘制前idle判断吞掉。
- BottomDock生产共用目标helper：0.6/1/1.5/2倍率，输入旧offset5DIP、真实抓手重基准offset6DIP反例；使用实际mapping.rigidOverlayTranslationYDip得到整数末态，独立capture-bottom弹簧目标0及释放目标1不污染终点。
- 既有离屏静止解析继续覆盖DPI/PIXELS、非均匀变换、旋转/剪切、DWrite细刻度布局和缓存。Eraser/Recall/父子pose、tooltip、FPS的目标元数据做完整源码审查；不将它们称为分别新增了专用动态轨迹测试。
- MeasureText保留原始未截断上界；配置key/default、设置字体、图标/几何和抗锯齿不变。

## 实施审查与编译修正

- 首轮修订完整solution exit0，202.08秒；初次新Headless exit0，177.47秒。
- 审查发现direct-hold目标仍读取未经重基准的frame offset，已改为实际解析mapping；新增生产/测试共用小helper与反例回归。原抓手和几何逻辑未改。
- 此helper首次误用不存在的elasticOffsetDip成员，复编以C2039失败（17.13秒）。重新核对结构及Resolve返回后只改为现存rigidOverlayTranslationYDip，不增/改字段或ABI；补baselineIdle断言后，取得上述最终全绿结果。这是已修复的源码错误，不是环境问题。
- 本轮沿用已确认有效的构建环境，未重复已知会无诊断失败的沙箱扫描。首轮环境诊断另见research/validation.md。

## 保留限制

未打开交互GUI或使用Computer Use。数值与真实离屏通过说明实现及回归满足目标连续性，真实大屏的最终观感仍需独立人工观察。任务保持in_progress，本轮修改保留未提交。

## 用户验收与收尾授权

用户确认改动有效，明确授权结束本任务并提交commit及push。此前保持in_progress/未提交为实施阶段记录；现按用户最新授权提交、归档并推送当前分支，不修改其他任务。
