# 实施计划：设置窗口触摸与笔经 Pointer/HiMsg 路由导入 ImGui

前置：`11-26-setting-touch-feedback` 已完成（其在 `ImGuiWndProc` 的 `WM_CREATE` 关闭系统反馈，本任务在同处追加 Win7 触摸注册）。

## 步骤

1. **抽取共享解码**（提交①）
   - `Inkeys/Inkeys/Input/UiTouchRouting.h`：声明 `PointerDecode`、`DecodedPointer`、`DecodePointer`。
   - `Inkeys/Inkeys/Input/UiPointerInput.cpp`：从 `HandlePointer` 搬出查询/分类，`HandlePointer` 改为调用；五窗专属撤销与 `DispatchDecoded` 留原处，日志字段不变。
   - 跑 `InkeysHeadlessTests` 的 `ui_pointer_tests`，确认零回归。
2. **设置本地路由**（提交②）
   - 新增 `Inkeys/Inkeys/UI/Setting/Setting.PointerInput.h/.cpp`：`ContactArbiter`、`HandleWindowMessage`（Pointer 与 Win7 `WM_TOUCH` 两条入口）、`CancelContact`、`FilterChannelMessage`、样本轨迹环与 `ReadTrack`。
   - `Inkeys.vcxproj` / `.filters` 加入新文件（只增条目，不改其它配置）。
   - `Setting.cpp`：`ImGuiWndProc` 头部接入取消类消息与 Pointer/Touch 入口；ImGui IO 写入在 `settingImguiMutex` 下进行，之后请求设置帧。若 `settingImguiMutex` 为匿名命名空间变量，写 IO 的小函数放在 `Setting.cpp` 内并由 PointerInput 通过回调/导出函数调用，避免暴露锁。
   - `Inkeys/IdtMain.cpp`：设置 `WindowSpec` 改 `bindMessages=true`、`messageOptions.filter=Filter::None`、`messageCallback=FilterChannelMessage`。
   - `Setting.cpp` 的 `ImGuiWndProc` `WM_CREATE` 追加 `!PointerMode()` 时 `RegisterTouchWindow`。
3. **测试**（提交③）
   - `InkeysHeadlessTests` 新增 `setting_pointer_input_tests.cpp`（并入工程）：
     - `ContactArbiter`：单接触、第二指/笔吞掉、只认同 device+contact+generation、同 id 再 Down 与同设备新主接触撤销旧接触、Cancel 幂等。
     - `FilterChannelMessage` 的纯判定部分（抽成接受 `message/wParam/extraInfo/pointerMode/contactActive` 的纯函数）：触摸/笔签名按钮与接触移动过滤、笔悬停放行、Win7 只滤触摸、原生鼠标放行。
     - 样本轨迹：增量读取、溢出标记、时间回退。
     - 借 `Touch::Testing::SetPointerQuery` 覆盖 `DecodePointer` 的 Contact/Swallow/Cancel/NotHandled。
4. `git diff` 检查 BOM + CRLF、无无关改动。

## 验证

- `InkeysHeadlessTests`：`ui_pointer_tests` + 新增测试（只跑相关用例，缩短时长）。
- ARM64 原生 MSBuild 构建 `InkeysRepo.sln` Debug|ARM64（≥ 5 分钟超时）。
- 实机（用户确认）：PRD 验收清单的触屏/笔/鼠标/标题栏项。

## 审查关口

- 提交①后：确认主栏/分页解码零行为变化（diff 只是搬移）。
- 提交②后：确认未登记五窗表、未跨线程访问 `targets`；HiMsg 回调不入队。

## 回滚点

- ①、②、③ 各自独立提交；回滚②即恢复设置原 ImGui backend 路径。
