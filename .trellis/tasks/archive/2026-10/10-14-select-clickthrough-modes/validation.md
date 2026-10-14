# 选择与穿透操作模式：实施与验证记录

日期：2026-10-14。工作树：D:/Project/Inkeys/Repo/Inkeys。分支：feature/mode。

## 实施状态

- 代码实现、Trellis implement/check 审查、相关规范同步和自动验证已完成。
- 用户更正分支后，当前工作树已切换到 feature/mode；之前和之后的本任务改动均在该分支。
- 实施与验证阶段未创建 commit；后续用户明确授权提交本次改动，并要求不要结束任务。提交范围包含功能代码、测试、规范与本任务记录。
- 未 push，未启动交互式 GUI，未使用 Computer Use；不执行 task finish/archive 或 journal 自动提交。
- 任务保留 in_progress，implementation=complete、automated_validation=passed、manual_gui_validation=pending；人工验收尚未执行。

## 功能范围

- 配置 UI.Bar.SelectAndClickThroughMode：0 合并式 / 1 分离式，默认/缺失/非法值为0，NoUpload，沿用异步冻结副本 FIFO 保存。
- 常规页标题后、自启前新增说明卡与下拉框；说明卡复用 PPT 信息样式，按实际本地化文字高度计算；三语言文案按批准需求。
- 独立穿透工具复用 Draw3 selectionMode 与既有保留墨迹/窗口穿透链，覆盖工具高亮、设置 owner 和鼠标滚轮判断。
- 双向模式映射保留墨迹；选择清空使用 Draw3 事务，拒绝不切换；穿透与定格互斥，定格恢复最近的画笔/图形/橡皮工具。
- 穿透/定格使用运行时布局，不改 A2 持久化顺序；定格不可用时隐藏，两种模式均覆盖；空页选择态隐藏穿透。
- 清空首击保留工具、空页再击进入选择；空页且已选择时灰色不响应。清空/撤回/重做/定格共用主题 TextPrimary 和0.30禁用透明度。
- 英文主栏 Select / (Clear)、Click- / through 使用局部两行字号，覆盖实际 DWrite 换字中点及恢复普通字号。

## 初次实现自动验证结果（3f3946cf）

| 验证 | 结果与边界 | 证据 |
|---|---|---|
| i18n sync/check | exit 0；en-US、zh-TW 各349/349，100% | research/settings-validation.md、research/check.md |
| 设置无 HWND 控件回放 | exit 0；3语言×4比例共12组，clipped/glyphClipped/missing/overlaps/failures均0；双向变更各写一次，重复选择不写盘 | research/settings-validation.md、research/replay-select-clickthrough-settings.py |
| 完整 InkeysRepo.sln Debug ARM64 | exit 0；最新必要增量编译/完整链接35.94秒，0 errors、6个既有 hashlib++ C4267 warnings | Build/SelectClickThroughValidation/solution-final-fixtures.log |
| InkeysHeadlessTests.exe --no-window | exit 0；真实配置 codec/保存、32组合 A2布局、Recall动画及既有套件均通过 | Build/SelectClickThroughValidation/headless-unrestricted.log |
| Inkeys.exe --state-mode-production-test | exit 0；生产状态机、跨入口/重复意图/过期回调、操作模式/穿透定格/清空事务通过 | Build/SelectClickThroughValidation/state-mode.stderr.log |
| Inkeys.exe --bar-eraser-offscreen-test | exit 0；真实主栏状态/显隐/大小/隐藏按钮命中、三语言 DWrite 测量与字号过渡通过；EraserVisual failures=0、PageControlScene failures=0 | Build/eraser-b/offscreen-results.log、Build/SelectClickThroughValidation/bar-offscreen.stderr.log |
| diff/格式检查 | git diff --check exit 0；29个已修改跟踪文件原 BOM、UTF-8 与 CRLF 保持，无新裸 LF | 最终 git diff 与逐文件 HEAD 字节格式对比 |

无窗口回放的 setter 与保存计数使用桩；配置 codec 和状态机另由真实 Headless/production 测试覆盖。离屏测试不创建 HWND，不能证明真实系统鼠标/触摸穿透或最终交互式设置窗口显示。

## 构建与环境记录

通过 vswhere 动态查找现有 Visual Studio 的 ARM64 原生 MSBuild。在同一个 PowerShell invocation 中先执行 PATH workaround，再构建完整 solution。每次使用持续进程等待，允许至少5分钟；没有因短暂无输出误判卡死。

最终使用的命令参数：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
$modeMsbuild = & 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\arm64\MSBuild.exe' | Select-Object -First 1
& $modeMsbuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:VcpkgManifestInstall=false /p:CL_MPCount=1 /p:MultiProcMaxCount=1 /p:LinkIncremental=false /m:1 /nr:false /nologo /v:minimal /clp:Summary
```

- 沙箱完整构建在预任务阶段失败（solution.log）；正常权限执行后定位到本机 vcpkg files.cpp(338) Value was null / MSB3073（solution-unrestricted.log）。
- 先核对所需 ARM64 依赖已经安装且指定版本/port revision符合 manifest/overrides，再以本次命令参数 VcpkgManifestInstall=false 复用已安装依赖。依赖核对见 Build/SelectClickThroughValidation/dependency-check.json。
- 原生并行编译出现 C1090 PDB API error24、C1060 heap（solution-installed-deps.log）；仅本次命令限制编译进程数后成功编译，随后遇到增量链接器 INTERNAL LNK1000 IncrBuildImage（solution-serial.log）。
- 仅本次命令禁用增量链接后完整构建通过（solution-final.log）；修正两个新增测试夹具后，必要重编译及完整链接再次通过（solution-final-fixtures.log）。
- post-build PATH 环境中 pwsh.exe 不可用；独立 i18n sync/check 已通过。未修改全局 PATH、SDK、依赖版本、solution 或工程配置。
- Headless 沙箱首次运行19项原子文件保存/配置写盘失败（headless.log）；正常权限原命令通过。未通过源码绕过环境权限失败。
- GUI subsystem 的 Inkeys.exe 测试用 Start-Process -WindowStyle Hidden -PassThru、WaitForExit(300000) 及 ExitCode 明确等待和检查退出码。未把空 LASTEXITCODE 或仅启动成功当作测试通过。

## 检查后修复

- 新多行英文文案换字时先缩小目标字号，防止透明中点仍以旧字号裁切；退出多行时在短标签替换后恢复普通字号。
- 旧撤回主栏/弹窗固定灰色与其他禁用状态不一致；统一主题颜色和禁用透明度，保持命令可用性、动画时限和几何规则。
- 生产模式测试的白板断言按现有 IsWhiteboardFeatureEnabled=false 门禁调整；未改变产品白板开关。
- 离屏测试新增 ready/Pen 更新路径前补齐 ThicknessDisplay Word 夹具，并在测量后恢复普通字号；未新增产品测试注入或窗口。

## 人工待验项

1. 桌面上鼠标、触摸在穿透时操作下方窗口且保留墨迹；从画笔/图形/橡皮进入、重复点击、其他工具退出。
2. PPT 运行期定格不可用时两模式隐藏定格；分离式穿透扩为2×2；空页选择态隐藏穿透；状态变化后真实按钮命中符合显示区域。
3. 真正设置窗口三语言及系统 DPI 的说明卡、下拉框、主栏两行英文与统一灰色视觉；即时切换保留墨迹，关闭/重开及重启恢复。
4. 有墨迹时分离式选择清空、撤销/重做恢复；清空两击；穿透后定格恢复最近工具且不恢复旧定格。

人工项保持 pending，不作为已经自动通过的结果；依据用户的无 GUI 验证约束本轮未执行。

## 后续截图反馈修复（2026-10-14，基线3f3946cf）

本轮继续feature/mode原任务，保持in_progress；修复和验证阶段未创建commit/push。后续用户明确要求确认改动生效后提交commit，但不要结束任务；不push，不finish/archive。

- Pierce原已固定留在运行时列表，问题是隐藏位置补位遗漏。复用现有隐藏锚点规则，将隐藏Pierce定位到Freeze或最近可见邻居；保留原位置动画曲线，不修改用户A2顺序。
- 非白板空页Selection的Undo/Redo门禁移入统一Recall视图投影，覆盖可用性刷新、计时器与弹窗入口；只覆盖显隐，不丢弃原3秒保护和布局。
- 中文说明改用「选择」/「穿透」、繁体「選擇」/「穿透」，分离式说明去掉清空括注，英文同为Select；主栏选择(清空)规则不改。三段双换行改为单换行，同字符串/字体/宽度测高，信息卡继续自适应。
- 新回归覆盖生产运行时按钮身份、反复模式/PPT变化、实际隐藏位置动画、真实Recall刷新与计时到期；设置回放验证紧凑高度/实际绘制高度一致。

已完成：i18n sync/check349/349；无HWND设置回放三语×4比例共12组通过，裁切/缺字/重叠/spacingFailures均0。1.0下中文文字45DIP/卡85DIP，英文75DIP/卡115DIP，比同文案双换行少30DIP。日志Build/SelectClickThroughValidation/settings-feedback.log，细节research/feedback-settings.md。

完整源码check及本轮自动验证均已通过：

| 验证 | 本轮结果 | 日志/记录 |
| --- | --- | --- |
| Trellis全范围源码check | 无需新增产品修复；锁顺序、隐藏命中、迟到popup门禁与CLI不写用户配置均已核对 | research/feedback-check.md |
| 完整 InkeysRepo.sln Debug ARM64 | exit 0，0 errors，4分4.65秒；185个既有告警，未修改无关告警代码 | Build/SelectClickThroughValidation/solution-feedback.log |
| InkeysHeadlessTests.exe --no-window | exit 0，RecallAttribute failures=0，全套PASS | Build/SelectClickThroughValidation/headless-feedback.stdout.log |
| --bar-eraser-offscreen-test | exit 0，EraserVisual failures=0、PageControlScene failures=0，实际隐藏定位动画/按钮身份/历史计时回归通过 | Build/eraser-b/offscreen-results.log、Build/SelectClickThroughValidation/bar-offscreen-feedback.stderr.log |
| --state-mode-production-test | exit 0，生产操作模式/清空事务及既有过期回调等PASS | Build/SelectClickThroughValidation/state-mode-feedback.stderr.log |
| 新产物三语言资源 | Inkeys.exe包含当前三个完整JSONC字节，确认嵌入最新文案 | 构建后只读二进制核对 |
| i18n与设置回放 | sync/check349/349；12组布局/行距与写盘计数均通过 | research/feedback-settings.md、settings-feedback.log |
| 最终diff/格式 | diff --check通过；本轮跟踪文件BOM/UTF-8/CRLF保持，无裸LF | 主会话及check的HEAD字节对比 |

本轮沿用上文已验证的构建命令参数与PATH workaround，动态定位ARM64原生MSBuild，完整solution成功。未升级依赖或改动工程配置；post-build的pwsh.exe PATH提示与既有环境相同，独立i18n和嵌入资源核对已通过。测试进程均用Hidden+WaitForExit(300000)+ExitCode确认，无交互窗口。

任务implementation=complete、automated_validation=passed、manual_gui_validation=pending，status继续in_progress。修复和验证阶段未创建新commit；后续用户已授权提交本轮改动，任务仍不结束，不push、不finish/archive。实际GUI动画、HWND计时唤醒、PPT和最终设置视觉仍人工待验；未启动产品窗口或使用Computer Use。

### 提交前确认
- 当前所有待提交产品/测试源文件都早于已验证的最新Debug ARM64产物；既有成功构建、Headless/production/offscreen结果对应当前源码。
- 重新只读核对Inkeys.exe包含当前zh-CN/zh-TW/en-US完整JSONC资源，最新说明已进入产物。
- 用户明确授权提交本轮修复及相关测试/规范/任务记录；沿用SSH签名。继续保持当前活动任务in_progress，不执行finish/archive、journal自动提交或push。
- 没有新的产品源码改动，不重复完整构建和全套测试。没有GUI授权，实际窗口视觉仍人工待验。

## 穿透按钮再次点击恢复工具（2026-10-14，基线d444f6a7）

- 最新用户要求：穿透时再次点击穿透按钮，退出并恢复既有lastAnnotationMode记忆的Pen/Shape/Eraser；保留墨迹、历史及具体工具属性，不恢复旧Freeze。
- 新增用户按钮统一入口ToggleClickThroughFromButton；显式ChangeStateModeToClickThrough继续表示幂等进入，不让同步意图误触退出。
- 同一状态锁/修订和既有SyncDraw3State链处理进出；不改配置、i18n、布局/动画和选择清空合同。
- 主会话已同步PRD/design/两份spec；implement与全范围check均完成，未发现需遗留的源码问题；Debug|ARM64和本轮无窗口回归已通过。
- 本轮保持feature/mode原任务in_progress，不commit/push、不finish/archive；真实点击/触摸仍人工待验，不启动GUI。

### 本轮切换验证结果

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| Trellis implement/check | 源码冻结，入口/锁/修订/记忆与真实按钮接线审查通过，无未修问题 | research/clickthrough-toggle-implementation.md、clickthrough-toggle-check.md |
| 完整InkeysRepo.sln Debug ARM64 | exit 0，0 errors，4分6.99秒；149个既有告警 | Build/SelectClickThroughValidation/solution-clickthrough-toggle.log |
| --state-mode-production-test | exit 0，clickthrough toggle/freezing等PASS；三工具入/出/重入、属性/bridge、一次修订、迟到回调与FIFO不增Clear通过 | Build/SelectClickThroughValidation/clickthrough-toggle-state.stderr.log |
| --bar-eraser-offscreen-test | exit 0，EraserVisual failures=0、PageControlScene failures=0；真实Pierce回调和三工具高亮恢复通过 | Build/eraser-b/offscreen-results.log、Build/SelectClickThroughValidation/clickthrough-toggle-bar.stderr.log |
| Headless --no-window | exit 0，全套PASS，Recall/Eraser failures=0 | Build/SelectClickThroughValidation/clickthrough-toggle-headless.stdout.log |
| Diff/编码/换行 | diff --check及本轮跟踪文件UTF-8/BOM/CRLF核对通过 | 主会话/检查代理对HEAD字节检查 |

沿用已验证的ARM64原生MSBuild、同invocation PATH workaround及命令参数；允许至少5分钟的持续构建等待，未修改solution/工具链/依赖版本。所有CLI使用Hidden、WaitForExit(300000)和ExitCode确认结果。配置/i18n未改，不重复设置回放或sync。

状态测试未启动Host，内容/历史快照和Undo/Redo哨兵FIFO证明切换不发画布命令，不代表真实墨迹呈现与鼠标/触摸穿透已人工验收。真实GUI/PPT/HWND视觉仍pending，未启动产品窗口或Computer Use。

本轮implementation=complete、automated_validation=passed，任务status仍in_progress；当前feature/mode，无新commit/push，无finish/archive。
