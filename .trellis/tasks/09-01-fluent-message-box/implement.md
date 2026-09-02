# 实施计划：全环境 Fluent 自绘 MessageBox

## Ordered Checklist

- [x] 1. 用户批准最终规划后运行 `python ./.trellis/scripts/task.py start .trellis/tasks/09-01-fluent-message-box`，加载 `trellis-before-dev` 的 Phase 2 上下文，并再次确认 worktree 中既有改动；批准前不得执行本项。
- [x] 2. 建立 `Inkeys/Inkeys/UI/MessageBox/` 模块骨架和 `Inkeys.UI.MessageBox` 公开同步合同；在 `Inkeys.vcxproj` / `.filters` 与 `InkeysHeadlessTests.vcxproj` 中显式登记源文件，但此时不迁移任何产品调用点。
- [x] 3. 增加 `Inkeys/src/message_box/error.png`、`resource.h` 和 `Inkeys.rc` 的 PNG resource 项；测试工程增加 `message_box_test.rc` 引用同一 PNG/ID。确认透明通道、资源 ID 唯一，资源加载失败仍允许无图标显示。
- [x] 4. 实现并先测试无 HWND 的基础逻辑：Request 校验、按钮/default/dismiss 规范化、fallback flags/result 映射、DIP 像素取整、monitor/work-area 选择、文本/按钮布局、命中区域和超限判定。
- [x] 5. 实现私有 GDI+ runtime 与资源 RAII：Win8+ 按次 token、Win7/探测失败的私有长生命周期 token、top-down 32-bit DIB、memory DC、字体/画刷/画笔，以及像素图标复制和 PNG -> premultiplied BGRA 解码。
- [x] 6. 实现 Fluent 深色 renderer：上下内容分区、标题/正文/图标、闭合 `1 DIP` 内边框、separator、等宽按钮及 normal/hover/pressed/disabled/focused 状态；所有输出先绘到不透明 DIB，再在 `WM_PAINT` 中一次提交。
- [x] 7. 实现固定尺寸 Win32 顶层窗口：标准 frame style、自绘 non-client/title、close hitbox、拖动、DWM dark/corner attributes、禁止 resize/minimize/maximize、DPI change、monitor 居中/clamp 和 Windows 7/10 API fallback。
- [x] 8. 实现输入与结果状态机：mouse capture、capture-loss 清理、Tab/Shift+Tab、Enter/Space、统一 `TryDismiss()`、单次结果提交及 hide-before-destroy。
- [x] 9. 实现同步协调器：进程级单框门、thread-local reentry guard、Normal 串行、CriticalNoWait 旁路、临时 UI 线程、owner 健康探测与双阶段禁用/恢复握手，以及唯一非递归 `MessageBoxW` fallback。
- [x] 10. 扩展 `InkeysHeadlessTests`：纯逻辑/资源测试始终可在 `--no-window` 运行；隐藏 HWND 测试覆盖 style、owner、hit-test、DPI、销毁顺序、并发与 owner 恢复；测试 build 通过宏开放窄测试钩子，生产模块不导出。
- [x] 11. 在自绘模块与测试稳定后，逐个迁移已确认调用点：`IdtMain.cpp`、`IdtPlug-in.cpp`、`Bar.Interaction.cpp`、`Setting.cpp`、`Window.Legacy.cpp`、`Helper.CrashHandler.cpp`、`SuperTop/IdtSuperTop.cpp`；每处按设计矩阵显式传 custom/fallback policy，保留原文案和调用分支。
- [x] 12. 静态核对 MessageBox 搜索结果：产品范围内只保留组件内部 `MessageBoxW` fallback；`Timeout`、历史未编译源码与 `IdtMain.cpp:1685-1705` 调试辅助调用保持不变。
- [x] 13. 检查所有修改文件的原编码/CRLF、资源脚本与项目文件配对、module import 顺序和 Windows 7 可用 API 的动态解析；运行 `git diff --check`、完整 ARM64 solution 构建及无窗口/隐藏窗口测试。
- [x] 14. 运行已授权的专用可见测试入口，自动生成限定窗口区域截图、关闭全部测试 HWND，并核对 Windows 11 ARM64 的圆角、DWM 阴影、四边边框、文本布局、图标、焦点和按钮状态；全程不使用 Computer Use。
- [x] 15. 汇总实际验证结果和限制：明确 UI Automation provider 未实现，Windows 7/10 若未上机则仅为静态兼容覆盖；不提交 commit，等待用户验收。

## Implementation Details By Step

### A. Project And Resource Wiring

- 新增文件遵循现有模块命名和中文关键注释规范；普通内部 helper 保持 anonymous namespace 或模块私有，不新增无必要 abstraction。
- `Inkeys.vcxproj` 与 `.filters` 必须同步登记；测试工程直接编译同一产品源文件，避免复制实现。
- 资源 ID 在 `resource.h` 当前编号范围内选择未占用值；`Inkeys.rc` 使用仓库既有 `PNG` 资源写法；测试 `.rc` 引用同一文件/ID，不另存第二份 PNG。
- GDI+/DWM 依赖优先复用当前 toolchain 可用库；仅为旧系统 API 探测使用窄的动态加载包装，不引入第三方 GUI 或图像库。

### B. Pure Logic First

- 将所有可在无窗口环境验证的状态转换保持为纯函数，WndProc 只负责把 Win32 消息转成这些事件。
- fallback flags 与结果映射必须可注入测试替身，自动化测试不得真的打开系统 MessageBox。
- 布局快照至少包含 window/content/command/title/body/icon/button/close/border rect 与 fallback reason，便于精确断言四边和重叠。
- 对窗口宽高、DPI、乘法和坐标加法执行显式溢出检查；影响对话框完整性的非法请求进入 fallback。icon 尺寸/stride/字节数非法时不得读取输入，只省略图标并继续。

### C. Renderer And Window

- DIB 尺寸变化时先成功创建新 surface，再替换旧 surface，避免 `WM_DPICHANGED` 失败后丢失可绘表面。
- `BeginPaint/EndPaint` 必须成对；禁止在 paint 路径访问 Bar/Setting 状态或持有产品锁。
- 四边框用单一闭合 geometry 和统一像素取整，不用四个独立 fill rect 猜测边界。
- `WM_NCHITTEST`、`WM_GETMINMAXINFO` 与 `WM_SYSCOMMAND` 三层同时禁止 resize；测试覆盖边缘、角落和 caption 双击。
- 所有失败分支保留首个 Win32/GDI+ error 供 Debug/test 诊断，Release 不增加逐帧日志。

### D. Threading And Cleanup

- fallback 前先恢复 owner 并释放全部自绘窗口/线程状态；已取得 admission gate 的外层协调器保持持有直到 `MessageBoxW` 返回，以维持普通调用串行。`FallbackToSystem()` 自身不得再次取锁，busy/reentry 旁路则在无锁状态直接调用。
- 临时 UI 线程先完成 GDI+/PNG/字体/布局 preflight 并报告结果，调用线程仅在 `PreflightReady` 后禁用 owner；不得为了测量文本在调用线程创建 GDI+ 对象。
- owner 恢复使用 scope guard，覆盖 UI thread 创建失败、HWND 创建失败、用户选择、`WM_CLOSE`、异常和测试强制退出；结果提交后的清理失败不得覆盖结果或再弹 fallback。
- UI 线程确定结果后只 signal “hidden”，必须等调用线程 signal “owner restored” 才销毁；owner thread 同步调用时使用只服务 sent-message 的受限等待，不分派普通 posted/input 队列。协调器另设超时/线程退出兜底，测试 harness 也有自动关闭 watchdog，避免失败时留下可见窗口。
- `CriticalNoWait` 不等待 admission gate 或 owner 线程，不执行跨线程 modal disable；CrashHandler 仍可能因进程破坏直接退化为系统框，此限制写入验收。

### E. Call-site Migration

- `IdtMain.cpp` 四处启动早期调用不获取任何 Window/Display/Logger 服务。
- `IdtPlug-in.cpp` 与 `Bar.Interaction.cpp` 传当前有效 `floating_window`；无效时使用各自原系统 fallback policy。
- `Setting.cpp` 从现有 `Inkeys.Window` service 读取 `WindowRole::Setting` HWND 作为 custom owner，但 fallback owner 保持 `nullptr + MB_SYSTEMMODAL`。
- `Window.Legacy.cpp` 故障发生于 Window Service，自绘请求不得反向调用 service。
- `Helper.CrashHandler.cpp` 使用内置 error icon、`CriticalNoWait`、ownerless create-time topmost；系统 fallback 保留 `MB_ICONERROR`。
- `SuperTop/IdtSuperTop.cpp` 只增加模块 import 与对应请求，不改变 token/进程启动逻辑。

## Automated Validation Matrix

| Layer | Required cases |
| --- | --- |
| Request/result | 三种按钮；合法/非法 default；dismiss enabled/disabled；close hidden；Win32 ID 映射；fallback failure |
| Layout | 96/120/144/192 DPI；320/548 DIP 边界；标题一/两行；中英正文；zh-CN/zh-TW/en 按钮文字；0/1 icon；1/2 buttons；work-area clamp |
| Overflow | title 第三行、正文超 756 DIP、小于最小 work area；断言 HWND 创建计数仍为 0、owner 未被禁用且完整原文进入 fallback payload |
| Icon | 正 BGRA/透明像素；非法 stride；尺寸/字节溢出；有效/损坏 PNG；内置资源；失败后正文仍存在 |
| Input | hover/press/release/capture lost；Tab 正反循环；Enter/Space；Esc/Alt+F4/close 共用结果；结果只提交一次；提交后清理失败不再 fallback |
| HWND | owned/ownerless styles；无 resize hit-test；SC_SIZE/MAX/MIN 被拒绝；DPI rebuild；hide-before-destroy；owner enabled 恢复；owner thread sent-message 等待不分派 posted command |
| Concurrency | 两个 Normal（含初始化失败后的系统 fallback）串行；CriticalNoWait 在 busy 时无锁直接 fallback；同线程 reentry 不取门；fallback 函数本身不操作门 |
| Lifetime | 重复创建/关闭；内部 live-object 为 0；GDI/USER handle 回到允许波动内；无残留 UI thread/HWND |

## Build And Test Commands

实现完成后先定位 ARM64-host MSBuild，最终路径必须包含 `MSBuild/Current/Bin/arm64/MSBuild.exe`。构建完整 solution，超时至少 10 分钟：

```powershell
& '<ARM64-host-MSBuild.exe>' InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

先运行严格无窗口用例，再运行允许创建隐藏 HWND 的集成用例：

```powershell
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
.\Build\ARM64\Debug\InkeysHeadlessTests.exe
```

若实际输出目录与现有工程不同，以构建产物路径为准并在结果中写明。测试失败不得通过启动 Inkeys 主程序绕过。

静态检查：

```powershell
git diff --check
git diff --stat
git status --short
rg -n "MessageBox(?:W|A)?\\(" Inkeys Timeout
```

## Authorized Visible Validation

可见验证只在自动化与完整构建通过后执行：

```powershell
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --message-box-visual-test .\TestResults\message-box-visual
```

入口合同：

- 创建本测试进程的纯色 backdrop/owner，使截图 crop 内不出现其他应用。
- 依次显示默认 OK、Yes/No 键盘焦点、带 error icon 的代表性窗口；通过本进程 `SendMessage/PostMessage` 驱动状态。
- 通过 screen DC 捕获窗口矩形外扩的阴影 margin，编码为 PNG；每个 case 设超时并自动关闭。
- 自动检查图片非空、尺寸稳定、边框四边采样与 accent/背景关键色存在；随后使用本地图片查看工具目视核对，不使用 Computer Use。
- 结束时断言测试 HWND/UI thread 数为 0；若任何窗口未自动关闭，终止专用测试进程并报告失败，不操作其他应用。

视觉验收至少记录：

- Windows 11 ARM64 系统版本与 DPI。
- DWM 圆角/阴影是否存在且没有双影、裁切或异常黑边。
- 四边 `1 DIP` 内边框是否同粗，separator 与圆角接合是否完整。
- 标题、正文、图标、close glyph 和按钮是否无重叠/裁切。
- normal/focus/hover/pressed 截图中的 Fluent 灰阶和青色 accent 是否符合参考图。
- 与 `research/references/02-dark-content-dialog.png`、`03-title-icon-close.png` 对照深色结构与图标/关闭布局；`01-light-content-dialog.png` 仅对照几何，不要求浅色输出。

## Risky Files And Rollback Points

- `Inkeys/Inkeys/UI/MessageBox/MessageBox.Window.cpp`：WndProc、DWM 与资源销毁核心；任何 shadow 修正只能在标准 frame/minimal non-client 范围内，禁止 layered/region/helper shadow。
- `Inkeys/Inkeys/UI/MessageBox/MessageBox.cpp`：并发门与 owner 恢复；`FallbackToSystem()` 不得自行取锁，外层已获准调用可持 admission gate 到 fallback 返回；CriticalNoWait 不得阻塞。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：business worker 与 Setting UI 跨线程；必须保留 owner 健康探测和完整恢复。
- `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp`：故障路径；改动仅限替换提示入口，不增加对日志、Window Service 或 D2D 的依赖。
- `Inkeys/Inkeys.vcxproj`、`.filters`、`Inkeys.rc`、`resource.h`：必须成组修改；缺少任一项会导致模块或资源在部分配置失效。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` 与 `message_box_test.rc`：测试宏只允许定义在测试工程，测试资源必须引用同一 PNG/ID，不能泄漏测试入口到生产构建。

主要回滚点：

1. 产品调用点最后迁移；在此之前新模块不改变运行时行为。
2. 若迁移后出现故障，可逐调用点恢复原 `MessageBoxW`，模块仍可留在工程中继续测试。
3. 若 DWM 外框不稳定，只回滚 frame/non-client 实现，不改变 Request/Result/fallback 合同。
4. 完整撤销时按“调用点 -> 工程/资源登记 -> 模块/测试文件”逆序移除，无配置或持久化数据需要恢复。

## Pre-start Gate

- [x] Goal、范围、非目标和验收标准已明确。
- [x] owner、topmost、dismiss、超长正文、图标、并发、旧系统退化和可访问性边界已确认。
- [x] 调用点、现有初始化顺序、渲染依赖与系统 fallback 语义已调查。
- [x] `prd.md` 已收敛，`design.md` 与 `implement.md` 已创建。
- [x] `implement.jsonl` 与 `check.jsonl` 已写入实现/检查所需的规范和调查上下文。
- [x] 用户已在本次最终规划摘要之后明确批准进入实现。
- [x] 批准后已运行 `task.py start` 并加载 Phase 2 / `trellis-before-dev` 上下文。

## Validation Results

- `InkeysRepo.sln` 使用 VS 18 ARM64 host MSBuild、`Debug|ARM64` 完整构建通过；仅保留仓库既有的转换类 warning，MessageBox 新模块无编译错误。
- `InkeysHeadlessTests.exe --no-window` 通过：覆盖 Request/result/fallback、三组按钮文字、96/120/144/192 DPI、自适应宽度、文本超限和透明 PNG/BGRA 输入。
- `InkeysHeadlessTests.exe` 通过：覆盖固定 frame、owner 禁用/恢复、标题拖动、八方向 resize 拒绝、DPI 重建、键鼠结果、并发门、CriticalNoWait、GDI/USER 基线和残留 HWND。
- `InkeysHeadlessTests.exe --message-box-visual-test .\\TestResults\\message-box-visual` 通过并生成 5 张限定窗口截图：OK、Yes/No focus、透明错误图标、primary hover、secondary pressed；像素断言和人工检查均通过。
- Windows 11 ARM64 上运行时读取到 DWM dark/corner 属性；标准 frame、闭合内边框和圆角结构通过。受测试桌面限制，screen-DC 不包含测试窗口，截图入口回退到 `PrintWindow`，因此外部 DWM shadow 由标准 frame/DWM 属性验证，不宣称有逐像素阴影截图证据。
- 未实现完整 UI Automation provider；Windows 7/10 未上机，只完成动态 API、无 layered/region/helper-shadow 路径和 GDI+ 生命周期的静态兼容覆盖。
- 未提交 commit。
