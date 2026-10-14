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

## 自动验证结果

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
