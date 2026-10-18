# 实施与验证交付记录

## 基线、任务与边界

目标分支 `chore/publish`；实际起点 `01a953d41adb5084e25899592cdde3bcb6c4770d`。参考 `529722e6877a35d2aa5e804657e55543436610ef` 未用于回退。task.py 创建并挂接发布准备父任务的子任务 `.trellis/tasks/10-04-pen-width-dip-defaults`，设计独立审查通过后 start。当前 `in_progress`：代码及可执行自动门禁完成，未提交/归档，真实设备人工项保留待验。

最初41个已有改动的SHA256均保持；新增journal记录会通过正式no-commit脚本追加。没有reset/stash/commit/push/PR/release；没有Computer Use或可见GUI。研究、PRD/design/implement、11项真实implement/check context均在任务目录，超注入大小的Draw3 spec要求代理另外完整读盘。没有空上下文门禁绕过。

## 主要实现文件

| 文件/范围 | 职责 |
| --- | --- |
| Business/PenWidthPolicy.hpp、PenWidthPreferences.hpp | 唯一预设、DIP/canvas转换、安全整数适配器、版本化配置/迁移codec |
| IdtState.h/.cpp | 现有槽保存Preset/ExplicitDip来源、同代目标Context、revision和double权威值 |
| IdtConfiguration.h/.cpp | 启动优先级恢复、旧px估计迁移、未知schema保护、现成atomic writer安全保存memory |
| IdtMain.cpp | 显示就绪后偏好恢复、首RTS之前Host callback接线、无窗口生产测试早退出 |
| Draw3.Bridge/Product/Host/WindowControl/DrawingController | double DIP+Context成组发布、目标全幅显示上下文、Down锁定样式、最终连续canvas适配 |
| Bar.Layout/Interaction/State/Main/RenderLoop/I18nFormat | 预设身份、连续滑条、FineDial取消/真实步进、同目标预览、诚实整数展示 |
| zh-CN/zh-TW/en-US源与Scripts snapshot | DIP单位文案，走sync/check流程 |
| Inkeys/InkeysHeadlessTests工程、animation_tests、pen_width_tests | 仅登记新增头/测试；实际policy/codec/state/Bar/Draw3生产路径测试 |
| native-desktop/pen-width.md、index、configuration规范 | 单位/来源/上下文/迁移可执行契约；不改原有dirty input-and-ink |

## 单位与精度实际情况

UI数字仍整数，非整数用 `≈`，有效小于1 DIP显示 `≈1 DIP`；展示数值不回写。Slider double连续，FineDial从精确origin加减整数个1 DIP，半整数步数向远离零取整，仅真实调整提交，进入/关闭/取消保留原Selection。

Selection/Resolve/bridge/style保留double；原state宽度float只是缓存。Draw3原本支持连续float名义宽度，终端CanvasDiameterForTool -> PenWidth::CanvasWidthPx才把受保护像素double缩窄为float。没有把生产墨迹降级为整数；每点压感、平滑、预测、GPU坐标保持原有浮点。

唯一策略入口 `Resolve`；scale=DPI/96，ideal/applied canvas px=resolved DIP*scale（受上限保护）。独立整数兼容器 `RoundPositiveCanvasPx` 为max(1,floor(px+.5))，没有提前取整DIP/半径。非法DIP setter/codec拒绝或单槽Medium回退，异常DPI回退96，非法尺寸回退1080p参考；DIP上限65536、canvas上限1048576，过小值保护float可表示性。

普通硬/软1/3/6 DIP、荧光16/24/36 DIP；大屏Brush参考2/4/6px、荧光16/24/36px，乘 `min(max(W,H)/1920,min(W,H)/1080)` 再除scale。W/H来自实际Drawpad目标显示全幅当前输出，不用EDID/workArea/虚拟桌面/Bar DPI。大屏1440p275%中档日志resolved1.939393939394 DIP、ideal/applied约5.333333333333px、整数适配5px。

无有效偏好默认Preset Medium；首启与点击中共用策略。Brush基础滑条1/scale～30 DIP，Highlighter min(16,1/scale)～100 DIP，动态扩至当前合法值和全部预设，不打开即夹断。Laser3/5/7 DIP默认5，仍只转一次。硬软共享，实际形状独立StraightLine/Rectangle槽继续原关系，未凭旧注释增加跟随。

Preset身份随设备/目标变化重解析，ExplicitDip保持；等价刷新不重置/存盘，活动Down样式锁定，下一笔才消费新Context。Bar预览使用同一目标canvas px，UI自定义zoom仅布局；光标最低可见尺寸、软笔预览和Laser光晕是保留的视觉规则。

## 配置和迁移

deploy PenWidth.Version=1 / Unit=DIP / Startup；memory同版本Selections。优先级：有效且启用记忆 > AutoDefaultWidth=false的有效启动选择 > Medium。临时改宽度只写记忆，记忆关闭不恢复会话值，切工具/PPT不再次初始化。

核实旧Preset flags/default fields及Draw.Brush1/Highlighter1.Width写入单位是canvas px；无可靠绑定历史源DPI，因此采用迁移时目标有效DPI除一次，尽量保留升级设备像素外观。保留Legacy原值、EstimatedSourceDpi/Estimated及说明；这是估计，不能还原便携配置原设备精确DIP。旧Laser若存在字段按DIP，不误除；没有虚构旧Laser启动字段。新schema幂等；未来/不认识schema原对象保留，AutoClean不能降级。部分/非法槽各自回退。bounded parse及现成atomic writer保留损坏原文件和失败保存前原文件，不影响其它配置。

## 精确执行命令和结果

所有命令从仓库根，经research/run_verification.py保存stdout/stderr/result JSON，900秒超时。实际ARM64原生MSBuild由vswhere定位。完整solution必须同一pwsh invocation先Remove-Item Env:PATH和MSBUILDDISABLENODEREUSE=1，随后MSBuild InkeysRepo.sln Debug|ARM64。下面命令列取自实际argv，没有把静态阅读列为运行PASS。

| 记录名 | 实际argv | exit | 秒 |
| --- | --- | --- | --- |
| baseline-build | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 50.900 |
| baseline-config | `Build/ARM64/Debug/Inkeys.exe --config-boundary-test` | 0 | 1.359 |
| baseline-draw3-map | `Build/ARM64/Debug/Inkeys.exe --draw3-renderer-map-test` | 0 | 0.172 |
| baseline-headless | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | 0 | 59.498 |
| baseline-i18n | `pwsh -NoProfile -File Scripts/i18n.ps1 check` | 0 | 1.115 |
| baseline-state | `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | 0 | 0.428 |
| final-bar-offscreen | `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | 0 | 25.153 |
| final-build | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 17.255 |
| final-config-boundary | `Build/ARM64/Debug/Inkeys.exe --config-boundary-test` | 0 | 1.286 |
| final-control-fence | `Build/ARM64/Debug/Inkeys.exe --draw3-control-fence-test` | 0 | 0.098 |
| final-draw3-commit-failure | `Build/ARM64/Debug/Inkeys.exe --draw3-renderer-commit-failure-test` | 0 | 0.110 |
| final-draw3-map | `Build/ARM64/Debug/Inkeys.exe --draw3-renderer-map-test` | 0 | 0.151 |
| final-draw3-width-rerun | `Build/ARM64/Debug/Inkeys.exe --draw3-pen-width-test` | 0 | 0.610 |
| final-draw3-width | `Build/ARM64/Debug/Inkeys.exe --draw3-pen-width-test` | 0 | 0.566 |
| final-fallback-controller | `Build/ARM64/Debug/Inkeys.exe --draw3-fallback-stable-controller-test` | 0 | 0.082 |
| final-fallback-lane | `Build/ARM64/Debug/Inkeys.exe --draw3-fallback-stable-lane-test` | 0 | 0.080 |
| final-headless | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | 0 | 97.258 |
| final-laser-raster-failure | `Build/ARM64/Debug/Inkeys.exe --draw3-laser-raster-failure-test` | 0 | 0.195 |
| final-loaded-retained | `Build/ARM64/Debug/Inkeys.exe --draw3-loaded-retained-install-test` | 0 | 0.086 |
| final-parked-desktop | `Build/ARM64/Debug/Inkeys.exe --draw3-parked-desktop-exit-test` | 0 | 0.385 |
| final-parked-ppt | `Build/ARM64/Debug/Inkeys.exe --draw3-parked-ppt-retained-test` | 0 | 0.076 |
| final-pending-topology | `Build/ARM64/Debug/Inkeys.exe --draw3-pending-topology-load-test` | 0 | 0.087 |
| final-ppt-load-retry | `Build/ARM64/Debug/Inkeys.exe --draw3-ppt-current-load-retry-test` | 0 | 0.085 |
| final-state | `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | 0 | 0.667 |
| final-width-production-rerun | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 0.744 |
| final-width-production | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 1 | 0.726 |
| integrated-build-1 | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 1 | 97.173 |
| integrated-build-2 | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 1 | 161.973 |
| integrated-build-3 | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 21.469 |
| integrated-headless | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | 0 | 94.385 |
| integrated-i18n | `pwsh -NoProfile -File Scripts/i18n.ps1 check` | 0 | 1.304 |
| integrated-policy | `Build/ARM64/Debug/InkeysHeadlessTests.exe --pen-width-only --no-window` | 0 | 0.182 |

构建1的首个源码错误是新探针near名称与Windows宏冲突，已最小修复。构建2编译完成但MSVC ARM64 linker 14.44发生C0000005 / LNK1000 IncrBuildImage，是工具链增量链接内部故障。仅本次 `/p:LinkIncremental=false` 后build3和final-build exit0，没有改工程链接设置、SDK或依赖。

首次width生产probe的preferences/state已PASS，Bar断言漏算1DIP range下限，已修expected clamp并新增实际session范围内部正负半步测试；final-width-production-rerun和final-state均exit0。最终Draw3 probe记录连续/整数适配差异并PASS。最终headless包含大值非整数近似、格式/步进和全部原回归，exit0。其后最后源码变化仅上述Bar.Interaction测试断言，最终Solution已重新编译和执行该生产测试；headless源码未再变化。

构建保留既有thirdparty C4267/Setting C4244警告；禁用incremental触发LNK4075 EDITANDCONTINUE忽略。PATH规范化后vcpkg尝试pwsh不可见的日志在基线已有；项目现有targets按9009回退系统PowerShell，构建exit0且产物可正常运行所有上述探针。没有为了零warning改无关文件。

补静态检查：git diff --check exit0；task.py validate通过（大spec注入截断告警已用读盘补足）；format-check.json记录现有编码/BOM/CRLF、无裸LF，preexisting_changed=[]。独立trellis-check source review PASS并修复double展示、DIP/px溢出比较、实际formatted文字measure缓存；最终自动gate证据在implementation-review.md。

## 验收证据等级

| 等级 | 内容 |
| --- | --- |
| 已实现 | 真DIP统一、两类预设/默认、Preset/Explicit身份、启动/记忆/安全迁移、连续Slider/FineDial/预览/光标/Draw3/形状/激光适配、规范与工程集成 |
| 已自动验证 | 本机ARM64 Debug完整solution；策略矩阵/非法输入/幂等往返；真实配置启动/优先级/失败保护；真实state revision；Bar真实交互取消/步进/选中/溢出；Draw3转换/cursor/Down锁定/CPU历史；WARP map/commit/laser；离屏UI；现有保存恢复/PPT CPU控制；全headless；i18n |
| 仅源码确认 | Host首次graphicsReady/RTS准入前同步目标准备、硬件Display消息到状态的时序、现有uink/GPU几何未新增DPI乘法、Windows兼容封装沿用；CPU seam不能替代真实硬件消息/可见窗口 |
| 待人工验收 | 真实触摸与hold锁定手感、软笔压力、实时PPT/WPS、混合DPI换屏/活动笔、首笔可见外观、Win7/Win32/x64设备；详见manual-acceptance.md，未执行可见hidden/Computer Use |

## 剩余限制、回退

大屏按目标全幅，PPT黑边不识别；现多屏能力不扩建跨屏单笔架构。Laser名义/光晕按捕获Context，既有全局GPU粒子是独立瞬态缓存，跨DPI时粒子外观仍需人工。无旧源DPI迁移只能估计。ARM64运行证据不代替其它架构/Windows实机。原增量链接故障已通过一次构建参数恢复，不宣称工具链已被永久修复。

必要回退只反向本任务delta；保留baseline已有改动和Legacy/迁移信息。回到旧版本不会自动理解新schema，需用保留Legacy按原字段人工恢复偏好；禁止整仓reset或覆盖便携配置。没有commit/push/发布，任务保持in_progress便于发布父任务跟进人工项。

收尾补证：add_session.py --no-commit正式追加Session39；journal原1181行重建CRLF后的SHA256与baseline完全匹配，index只更新会话索引。final-worktree-check.json确认其余39项preexisting指纹全部相同，git HEAD/分支未变；最终git diff --check exit0。

## 当前提交授权与任务状态

用户确认本轮人工验证通过并授权本次本地commit；保持in_progress，不finish/归档，后续调整继续。此前“未提交/人工待验”文字为原验证时点记录，本反馈不冒充代理实机验证，也不覆盖未说明的逐设备范围。提交仅包含当前任务delta，原Win7工作区改动排除，不push。
