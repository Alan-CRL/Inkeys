# Research: 光标设置、历史方案与配置边界

- **Query**：恢复隐藏触摸光标选项，并为鼠标画笔、荧光笔及亮暗外观规划绘制设置中的光标块。
- **Scope**：internal。
- **Date**：2026-10-18。
- **源码基线**：`4c9fee5c551d984906cb43cb67fd09bafebb6979`，当前分支 `bugfix/cursor`。本文只做静态调查，不代表 Win7 或本机运行验证。

## Findings

### Files Found

| 文件与位置 | 已确认事实 |
| --- | --- |
| `Inkeys/IdtConfiguration.h:60` | `SetListStruct::hideTouchPointer` 仍为原子 bool 成员。 |
| `Inkeys/IdtMain.cpp:2590` | 旧默认初始化仍设为 false。 |
| `Inkeys/IdtConfiguration.cpp:572` | 每次捕获旧设置都会移除 `HideTouchPointerBeta`，不取决于 AutoClean。 |
| `Inkeys/IdtDrawpad.cpp:422` | 历史 WndProc 只有在 `HTCLIENT && hideTouchPointer && touchNum` 时 `SetCursor(NULL)`。 |
| `Inkeys/IdtRts.cpp:249`、`:464` | 历史 RTS 回调中按开关同步发送 `WM_SETCURSOR`，希望提前隐藏。 |
| `Inkeys/Inkeys.vcxproj:919`、`:926`、`:929` | 前两份历史源码是 `None`，当前产品编译 `IdtDrawpadFacade.cpp`。 |
| `Inkeys/IdtDrawpadFacade.cpp:8`、`:63` | 旧接口是兼容存根，不允许旧 RTS 与当前 Draw3 producer 同时绑定 HWND。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:3903` | 当前设置的绘制页为 `tab3`，含绘图设备、橡皮和触摸设置，目前无用户光标块。 |
| `Inkeys/Inkeys/Other/Other.Config.cppm:335` | 新配置已有 `Drawing.Eraser`，另有 `Experimental.Inkeys3.Draw3` 中的触摸与图形选项。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:4133`、`:4174` | 当前同页可更新新配置、调用 Host 窄接口，再通过 `QueueConfigWrite()` 持久化。 |
| `Inkeys/IdtState.cpp:215`、`:723` | 业务状态集中冻结为 ProductState；设置渲染期间的 Sync 先经 defer guard，避免持 ImGui 锁同步等待 HWND。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h:193` | 当前 ProductState 包含工具、宽度、设备、橡皮和 selectionMode，尚无产品光标偏好。 |
| `Inkeys/IdtMain.cpp:2723` | 新配置 ReadAll 后恢复 Host 的其他持久化选项，是未来启动同步可参照的现有路径。 |

### 历史恢复不等于重新挂接旧输入链

只读 `git log -S HideTouchPointerBeta` 与 `git show e6c729d2` 确认：提交 `e6c729d2081ed9f37e744316e0d3158177d169ee`（2026-09-20，`refactor(settings): clean up obsolete options`）删除旧键读取、写出、设置卡片及三语文案，并增加保存时清理。

该提交之前，中文说明将隐藏目标描述为触摸产生的白色圆点，鼠标或触控笔输入到来后恢复其光标。旧标题包含 BETA；旧设置仍不能证明当前 Draw3 的系统光标、Tablet feedback 或逐触点橡皮应该共用一个开关。

旧实现依赖触摸计数先更新、再处理 WM_SETCURSOR。用户已反馈其存在落笔闪现与延迟，这属于需要解决的产品结果；仅根据静态代码不能测定延迟、判定白点所属层或保证修改回调顺序便能消除。

当前恢复必须在 Draw3 的光标归属与当前 HWND 路径内完成。只恢复设置卡片或 `hideTouchPointer` 的读写不会控制当前 Draw3，也不能重新编译或启动 IdtRts/Draw2。

### 最新用户决定（2026-10-19）

用户明确隐藏触摸光标始终启用、没有开关；当前似乎已隐藏，重点是确认效果与 Win7 正常。这关闭了恢复旧选项、设置默认值及旧值继承的规划分支。下列历史代码事实保留，不作为新开关或迁移器的依据。

### 现有规范的影响

`.trellis/spec/native-desktop/configuration-i18n-and-assets.md:47` 起将 HideTouchPointerBeta 列为退役键，与最新用户决定一致：维持忽略与保存时清理，不恢复该键，不给隐藏行为新增持久化开关。本轮不改既有 spec。

当前 `Drawing.Eraser` 与触摸开关的活动消费者为新增 `Drawing.Cursor` 提供了就近模式。**建议**把新偏好作为同一配置组写入 `Inkeys/Config/main.json`，通过 Host/业务快照或窄接口同步，不给 `deploy.json` 再创建一个互相竞争的光标真值。这是技术设计建议，不把“新 module”本身当成统一迁移授权；后续实施前仍需审定字段归属和旧值策略。

旧 Beta 迁移议题已关闭。旧键为 true、false、缺失或非法都不能关闭固定隐藏；不读取为新偏好、不双写、不新增迁移器。Drawing.Cursor 候选只保存鼠标样式与三场景高亮，不保存隐藏触摸光标的值。用户认为当前似乎已隐藏，后续以现有链核验和 Win7 证据决定是否需要修复，不能把恒开启要求解释成必须新增一套隐藏机制。

### 设置同步、写盘与 i18n

- `Setting.cpp:420-446` 已有配置快照和业务 FIFO；光标设置应复用该链，避免在每帧直接写盘。
- `Setting.cpp:8813-8815` 在 ImGui 锁内恢复协程；`:8838-8843` 是 Sync defer guard。即使只改光标，任何将触达 HWND 的即时生效路径也必须检查此边界。
- `IdtState.cpp:215-229` 序列化一次状态；`SyncDraw3State:723-747` 的 HWND/owner 事务有既有排序。光标配置不应引入从设置线程直接调用窗口光标 API 的新路径。
- 以 `Inkeys/src/i18n/zh-CN.jsonc` 为基准，维护 `zh-TW`、`en-US`；后续有代码授权时才运行 `Scripts/i18n.ps1 sync` 和 `check`。`sync` 会改语言文件、生成头和快照，本轮不运行。
- 用户当前要求的是普通设置→绘制中的光标块；实验室的 `ConsoleOutput.Cursor` 是诊断开关，不能混作用户光标样式。

### 验证与构建事实

- 主工程为 Visual Studio/MSBuild，已有 `InkeysRepo.sln`；存在 `InkeysHeadlessTests`，其入口 `animation_tests.cpp:1648-1649` 支持 `--no-window`。
- 遵守本次会话明确的构建规则：后续实际验证产品时用完整 Solution、Debug|ARM64、从本机 VS 定位 ARM64 原生 MSBuild、同一 PowerShell invocation 先规范 Path/PATH 并禁用 node reuse，单次构建至少允许 5 分钟。
- 当前源码的 Solution/PptCOM 依赖声明与会话所给历史说明有差别；本任务无需改变工程或依赖，计划沿用用户指定的完整 Solution 验证，不借光标需求修改构建链。
- 本轮默认 Python 命令命中不可执行的 WindowsApps alias；改用 Codex 已安装的 bundled Python 执行 Trellis，未安装运行时或修改 PATH。
- `git status` 默认递归 HiMsg 时遇到工作树 submodule 元数据的访问限制；使用 `--ignore-submodules=all` 检查主仓变更。没有改 submodule 或权限，也没有据此修改源码。

### Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：Touch 视觉归属、来源过滤和光标诊断。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：双配置边界、退役键及 i18n 生成链。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：设置绘制作用域的同步合同。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一 Host、Window Service HWND 所有权与输入后端。
- `.trellis/spec/native-desktop/eraser-attributes.md`：橡皮尺寸、透明度、逐入口偏好不能被鼠标样式选项意外改变。

## Caveats / Not Found

- 当前未收到这次 Win7 复现对应的完整 Cursor 日志或确切 EXE 身份；历史任务有不同问题的现场记录，不能当作这次同一个根因。
- 2026-10-19 用户已确认鼠标工具默认、半透明/高亮边缘语义及三场景分别保存，见 cursor-appearance-clarification.md。隐藏行为已确认固定启用、无开关且无旧值迁移；历史调查不替代最新用户决定。
- 本轮只修改任务文档；未运行构建、headless、窗口 harness、GUI、Computer Use 或 Win7 真机测试。
