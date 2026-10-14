# 主栏图标顺序与展开方向验证

## 实施状态

- 分支 feature/mode，基线16d6551f；Trellis任务 toolbar-order-expansion 保持 in_progress。
- 常规页选择与穿透下方增加主栏图标顺序（默认跟随主按钮）与主栏展开方向调整（默认当位置不足时）。两字段严格整数归一化、NoUpload、冻结副本异步保存，切换仅唤醒主栏布局。
- 顺序只镜像横坐标，保持按钮列表与上下填充；完整目标包络先计算再判断展开侧，当前侧不足且对侧够用才翻，两侧都不足保持方向/位置/缩放。启动、底部居中和白板/PPT放置锁沿既有行为，纵向方向不改。
- 更新配置/i18n和渲染规范。保留有限观察DTO尺寸与wire字段，以原flags bit8..9/bit10记录两配置，bit7继续非法。
- implement/check上下文与PRD/设计/实施/研究已保存；Trellis检查与完整构建反馈修正已记录，详见 research/check.md。

## 自动验证

| 验证 | 结果 | 证据 |
|---|---|---|
| i18n sync/check | PASS，en-US/zh-TW各357/357 | research/settings-implementation.md |
| 真实设置块与ComboClass无HWND回放 | PASS，三语×0.6/1/1.5/2共12组，每组writes/wakes=6，重复选择无额外动作；字形/文字裁切、缺字、标题控件重叠均0 | Build/ToolbarOrderExpansionValidation/settings-replay.log |
| Trellis全范围检查 | PASS，首编fixture访问权限问题已最小化修正 | research/check.md |
| Debug ARM64完整solution | PASS，最终退出0、0 errors；PptCOM/Inkeys/Headless均成功，已有warnings保留 | Build/ToolbarOrderExpansionValidation/solution.log |
| InkeysHeadlessTests.exe --no-window | PASS，退出0，Config metadata failures=0，全部纯策略/finite/停靠及既有套件通过 | Build/ToolbarOrderExpansionValidation/headless.stdout.log |
| Inkeys.exe --bar-eraser-offscreen-test | PASS，退出0、EraserVisual failures=0、PageControlScene failures=0 | Build/eraser-b/offscreen-results.log |
| 原文件编码/换行与diff | PASS，18个已有文件UTF-8/BOM保持，CRLF无裸LF；新增策略头CRLF；git diff --check通过 | 主会话检查输出 |

## 自动测试覆盖

- Config：两字段所有合法值真实落盘/重启/ReadMini、缺失及非法类型/越界/大整数默认、reset、冻结副本及NoUpload。
- 纯策略：4种顺序×左右展开、X镜像不改Y、当前侧/对侧/两侧均足够或不足、像素刚好容纳、目标宽度/位置/zoom变化、重复稳定、负显示器坐标、无效几何与锁定。
- Finite：全部8组排序/方向配置位合法性、语义变化与冻结比较；bit7未知位仍拒绝，sizeof72/wire合同保持。
- Bar离屏：真实PositionUpdate中轴/空间门禁与上下回归、两行按钮横向动画、隐藏Pierce锚点和稳定身份；既有选择/穿透与Recall等回归继续通过。
- 设置回放：3语言×4比例，4顺序轮换及2方向切换，重复选中无额外保存/渲染唤醒，文字与字形测量无裁切。

## 首次构建发现与修正

首次完整构建退出1，首个有意义的错误是新增离屏fixture直接访问受保护的bottomDockMode/bottomDockRecoveryActive，6处C2248。改为复用已有公开InitializeStartupPlacement入口准备浮动状态并恢复fixture，没有扩大产品字段权限或新增接口。另仅改名新增配置测试的循环变量，消除本轮引入的C4456。首次日志保留于Build/ToolbarOrderExpansionValidation/solution-first.log；修正后统一复建，不修改工程或工具链。

第二次完整构建退出1，首个错误为新增设置块中的裸config未限定命名空间，改为Inkeys::config。同步将设置回放fixture仅置于namespace Inkeys并增加抽取块限定名断言，重生成/编译/执行12组全部通过。第二次日志保留于Build/ToolbarOrderExpansionValidation/solution-second.log。第三次完整solution退出0，无剩余源码错误。

设置回放运行目录必须是仓库根：编译后首次从Build子目录执行无法解析仓库相对字体路径，修正WorkingDirectory后退出0；未修改字体或生产路径。

## 构建方式与环境

使用项目已有InkeysRepo.sln，Debug | ARM64，动态vswhere选择Visual Studio中的ARM64原生MSBuild。同一个PowerShell invocation先Remove-Item Env:PATH，再设MSBUILDDISABLENODEREUSE=1并运行构建。

沿用前任务已核对的本机命令范围参数 /p:VcpkgManifestInstall=false /p:CL_MPCount=1 /p:MultiProcMaxCount=1 /p:LinkIncremental=false /m:1 /nr:false，复用已安装依赖、串行编译与非增量链接；不改工程、依赖版本或工具链。持续等待允许至少5分钟，不因短暂静默重启构建。

最终构建仍出现既有applocal脚本找不到pwsh.exe的9009环境提示，MSBuild该步骤未使构建失败（最终exit0）。本机依赖为已安装静态ARM64版本，实际新生成二进制的无窗口CLI可运行；不修改工程或全局PATH绕过。本提示与源码错误明确分开。

CLI使用明确的早期无窗口入口，以Hidden Start-Process、WaitForExit(300000)和ExitCode确认结果。测试需普通进程权限，以免sandbox阻止配置测试的原子文件替换；没有启动交互GUI。

## 证据边界与人工待验

- 无窗口设置回放使用生产UI块、ComboClass、ImGui和仓库字体；保存/唤醒为计数fixture，不能替代生产线程FIFO或窗口呈现。真实配置codec/冻结副本由Headless覆盖。
- Bar离屏回归执行共享纯策略、真实PositionUpdate、BarUiValue动画与隐藏锚点，不执行整个私有RenderLoop。完整RenderLoop接线由源码检查和solution编译验证。
- 未启用需要HWND的Presentation harness，未启动GUI或Computer Use。
- 人工待验：真实设置页三语视觉、四排序×两侧的拖动与切换动画、左右边缘单侧/双侧空间不足、折叠后重开、动画中途切换、系统DPI/缩放与负坐标多屏切换、底部居中/非居中停靠及白板/PPT放置、上下倒转回归。
- 本轮不commit、不push、不finish/archive任务。

## 用户授权的后续提交与延续

用户随后要求先提交改动但不结束任务，已创建35afa7ee5f7752f6ae91442624ba05c4f840b1b6（feat(toolbar): add icon order and expansion direction settings），提交后工作树干净。继续同任务调查与修复拖动释放第三光源闪烁；本节覆盖此前“不提交”的初版执行约束，任务仍in_progress，新修复结果将单独记录。

光源延续修复已完成并通过自动验证：旧实现48条预期FAIL，修复后24组坐标/120静止帧与4组D2D像素均通过，完整ARM64 solution和Headless退出0。详情见[validation-drag-light.md](validation-drag-light.md)。新修复未再次提交，任务仍进行中。
