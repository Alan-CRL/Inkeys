# 手动预设高亮与形状三档：本轮验证

实际基线 `f8054a227174707a984291d875b4fcc6fadf605f`，分支 `chore/publish`，Active task `.trellis/tasks/10-04-pen-width-dip-defaults`，仍为 `in_progress`。本轮没有commit/push/finish/archive，没有启动可见GUI或Computer Use。

## 实现与边界

- Bar.Layout.cppm中的唯一IsBarThicknessPresetSelected供画笔/形状布局及hover共用。有效Preset身份优先；ExplicitDip按当前工具槽的同代Family/Context预设精确DIP匹配，容差8*double epsilon*max(abs(value),abs(target))，无绝对下限。既不比较显示整数/量化px，也不回写、吸附或改变ExplicitDip来源。异常尺寸统一fallback时只选索引0→2的首个匹配。
- 形状实际点击、读数和小线预览已经使用同Brush策略：普通1/3/6DIP，大屏参考2/4/6px按Context解析。无需重改已经正确的数值线路，修共用高亮即可；直线/虚线共用StraightLine槽，矩形独立，画笔槽独立，不改变跟随关系。
- Bar.Interaction.cpp既有CPU production probe新增真实FineDial非预设→候选发布→松手Commit(false)→单一高亮/Explicit不改，普通/大屏多Context与四笔family、直线/虚线/矩形策略一致及独立槽、分数/低于1DIP、nextafter容差边界/float偏差/同显示整数/量化碰撞、非法值、取消/过期和换屏保持显式值，以及异常fallback唯一性。
- 单位契约、连续Draw3绘制、Slider范围/步进、出厂Medium、已停用启动恢复、历史几何、激光/荧光/橡皮均保持上一轮。生产改动仅Layout共享高亮判断，Interaction仅测试增量；不增加工程文件或依赖。

## 精确执行记录

以下全部从仓库根通过research/run_verification.py运行，实际argv/cwd/exit/耗时和双流日志原样保留，单次900s超时。MSBuild路径由vswhere当前安装动态发现；同pwsh invocation规范化PATH并禁用node reuse，完整InkeysRepo.sln Debug|ARM64。LinkIncremental=false为此前已确认ARM64增量linker故障的一次性命令参数，未改工程/工具链。首轮正常路径完成后独立审查新增fallback修补，随后最终完整build及所有测试重新执行。

| record | exact argv | exit | seconds |
| --- | --- | --- | --- |
| preset-shape-bar-offscreen.result | `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | 0 | 16.358 |
| preset-shape-baseline-production.result | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 0.238 |
| preset-shape-build.result | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 88.501 |
| preset-shape-draw3.result | `Build/ARM64/Debug/Inkeys.exe --draw3-pen-width-test` | 0 | 0.747 |
| preset-shape-final-build.result | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 88.313 |
| preset-shape-headless.result | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | 0 | 57.945 |
| preset-shape-production-initial.result | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 0.444 |
| preset-shape-production.result | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 1.090 |
| preset-shape-state.result | `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | 0 | 0.709 |

补充检查：git diff --check（排除原样工具log）exit0；task.py validate两份14个真实context PASS，既有大Draw3文档注入大小提示由子代理完整读盘处理。两源码BOM保持、CRLF无bareLF；原41个dirty路径hash在正式journal追加前全部保持，见preset-shape-format-check.json。HEAD没有变化，未包含其他Win7编辑。没有独立C++ lint/typecheck命令，完整编译是类型门禁。旧Setting/thirdparty warning及LNK4075沿用，不扩大修改。

## 验收等级与回退

- 已实现/已自动验证：精确数值匹配与来源不变，单一高亮，真实FineDial提交/取消/过期，形状同Brush策略及独立槽，大屏分数DIP/异常fallback；完整ARM64编译、headless、实际CPU生产seams、Draw3宽度探针、真实无HWND离屏Bar。独立审查详见preset-shape-review.md。
- 源码确认：实际Slider/touch松手SetPenWidthIfRevision→候选清理→UpdateRendering(true)，layout/hover统一读取高亮→颜色/透明度动画及既有MarkChanged。生产probe没有驱动完整HWND消息循环或人工按钮视觉，不能将CPU seam当实机交互验收。
- 待人工：触摸/Slider/FineDial松手后按钮视觉高亮、shape实际属性栏三档与预览、PPT/mixedDPI/Win7体验；遵守无GUI限制未运行可见窗口测试。不把“整数文字相同但实际DIP不同”算作粗细相同，不新增自动吸附。
- 回退仅本次基于f8054a22的两个源码及文档delta，不reset工作区或改历史提交；查询无配置写入，无迁移/历史文件变化，不需数据回滚。任务继续in_progress，等待后续用户调整。


## 最终验收与结束授权（2026-10-05）

用户明确反馈“人工验证通过”，并授权结束本任务和提交commit。记录为用户人工验收通过；不将反馈扩展为逐项设备/架构均已运行。此前pending为历史阶段记录。自动门禁及独立审查已通过，见research/preset-shape-verification.md与preset-shape-review.md；本轮按脚本归档仅此任务，发布父任务和Win7工作保持不变。
