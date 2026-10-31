# Research: Startup visibility conflict and minimal fix proposal

- Query: 为什么分页控件创建时隐藏，启动后仍可能形成左上角 L 形落笔死区；进入 PPT 后为何恢复；如何最小修复？
- Scope: mixed（既有内部调查的聚焦补充；引用主会话已核对的微软文档）
- Date: 2026-10-12

## Findings

### 用户观察与证据分级

用户补充：进入 PPT 后，左上角 L 形区域恢复正常，可以直接开始新笔画。结合原调查中“区域外开始的笔画可继续进入该区域”，此现象支持分页 HWND 初始布局/显隐异常的解释。它不是运行时 HWND 或笔事件追踪结果。

已由静态代码证实的是两个显隐入口存在冲突：创建阶段保持隐藏、PageControl 按业务模式维护显隐，但 TopWindow 在启动门禁就绪后无条件显示四个分页窗口。尚未证实的是故障现场实际发生 Hide-before-Show 的顺序，以及这些窗口具体如何截获真实笔输入。

### Files found / Code patterns

| 文件与行号 | 用途及证据 |
| --- | --- |
| `Inkeys/Inkeys/Window/Window.cppm:83` | `WindowSpec.visible` 默认 false；坐标默认 `(0,0)`。 |
| `Inkeys/Inkeys/Window/Window.cpp:1126` | 创建路径按 visible 参数使用隐藏状态。 |
| `Inkeys/IdtMain.cpp:2901` | 四个分页窗仅覆写尺寸：底部两窗 230×80、中部两窗 43×165，初始位置相同；其并集为 L 形。 |
| `Inkeys/Inkeys/Window/Window.Legacy.cpp:149` | TopWindow 启动时无条件 Show 四个分页 HWND；不检查放映状态、布局或呈现完成。 |
| `Inkeys/IdtPlug-in.cpp:971` | `pptWindow=true` 表示客户端已注册、启动门禁已就绪；注释明确未放映时四个分页窗口不提交首帧。变量名不能当作实际可见状态证据。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2845` | Acquire 注册四个渲染客户端，成功后 RequestAll；调度请求独立于 HWND 是否显示。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2942` | PublishPptState 仅在业务状态改变后 RequestAll；不能保证重复隐藏外部 Show 出来的窗口。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2000` | PageControl 渲染事务按 shouldShow 执行 SetBounds 后 Show，或 Hide；可见窗口先经过布局和呈现流程。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2161` | 隐藏目标或输入锁定时 WM_NCHITTEST 返回 HTTRANSPARENT；不能由此证明所有真实笔/触摸/RTS 路由穿透。 |
| `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:250` | 放映可见状态发布进入 PageControl；业务状态改变可触发重新布局和显隐处理。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1370` | 已建立的 RTS contact 按已有 binding 发布移动；与新笔画起点失败、既有笔画继续进入区域的观察相容。 |

初始隐藏并不保证随后保持隐藏。可能的执行顺序是：Acquire 请求一次 Hidden-mode 帧并隐藏窗口，TopWindow 在 Bar/Freeze 门禁完成后又 Show，业务状态未改变便不再请求纠正帧。这个具体时间顺序仍属合理推断。

进入 PPT 后，状态变化唤醒分页渲染，启用的控件布局至底部或屏幕中部，禁用的控件隐藏，能够消除初始 `(0,0)` 矩形状态。这解释与用户新观察一致，但不替代实际输入验证。

### Minimal fix proposal

唯一拟修改的产品文件：`Inkeys/Inkeys/Window/Window.Legacy.cpp`。

1. 删除 TopWindow 中针对 `PptBottomLeft`、`PptBottomRight`、`PptMiddleLeft`、`PptMiddleRight` 的四个无条件 Show。
2. 将“PPT 五窗……唤醒其首次提交”的现有注释改为准确的中文说明，例如：

```cpp
// 分页窗口由 PageControl 在布局和呈现提交后决定显隐；
// 启动阶段不能直接显示，否则初始矩形可能滞留在左上角。
```

3. 保留其他启动门禁、Freeze/Bar 显示、置顶刷新、PageControl 初始化请求及业务状态发布流程。

PageControl 应为这四个分页窗口的业务显隐唯一所有者。Acquire 已经发起 RequestAll，不需要由 TopWindow Show 来唤醒首次渲染。本方案不增加周期 Hide、不移动初始坐标、不永久添加 WS_EX_TRANSPARENT，也不改绘制 admission 或 RTS 路由。

预期行为：普通桌面无放映、白板未展开时保持四窗隐藏；放映时按配置布局并显示；白板展开时底部共享窗按白板布局显示、中部窗隐藏；退出放映或收起白板时按业务状态隐藏。

### Validation plan for later implementation

- 保持源文件原有编码与换行；检查产品 diff 仅含四次 Show 删除及对应中文注释修正。
- 静态确认四个客户端仍由 Acquire 注册并 RequestAll，显示仍经 PageControl 的布局/呈现路径；现有启动门禁与其他窗口行为不变。
- 若实际验证 Inkeys 编译，复用 `InkeysRepo.sln` 的 `Debug | ARM64`，定位已安装 Visual Studio 的 ARM64 原生 MSBuild；不能独立构建 Inkeys.vcxproj，因为依赖 PptCOM.csproj。构建至少允许 5 分钟。
- MSBuild 前在同一 PowerShell invocation 执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue` 和 `$env:MSBUILDDISABLENODEREUSE = '1'`；环境失败与源码失败分别报告。
- GUI 与真实笔测试仅在用户明确授权后执行：启动无放映时检查四窗实际可见性；比较 GetWindowRect/IsWindowVisible 与 RTS StylusDown 到达情况；验证 L 范围内直接落笔，进入/退出放映及白板展开/收起的布局、分页点击和拖动。编译或静态审查不能替代真实笔回归。

### External references

- Microsoft Learn, Window Features / Layered Windows（Win32 在线文档，无固定库版本）：https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows 。主会话已核对：layered 窗口在设置透明属性或 UpdateLayeredWindow 前不呈现可见内容。因此“不可见画面但参与输入”的具体机制不能从未提交位图直接推导。
- Microsoft Learn, WM_NCHITTEST（Win32 在线文档）：https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest 。HTTRANSPARENT 的命中语义不能直接当作真实笔/触摸/RTS 路由已验证。

### Related specs / existing research

- `.trellis/spec/native-desktop/rendering-and-ui.md`：PageControl 独占四个 owned layered HWND，PPT/Whiteboard 发布业务状态；Drawpad owner 链及窗口坐标/命中审查规则。
- `.trellis/tasks/10-12-canvas-l-deadzone-stroke-start/research/l-deadzone-stroke-start.md`：原始几何匹配、创建尺寸、RTS binding 与候选原因分析；此文件保持原样。本补充收紧旧记录中关于实际笔截获的断言，将其明确归为待验证推断。
- 仓库 AGENTS.md：minimal diff、原编码/换行、适量中文注释、默认无 GUI、solution Debug ARM64 和 Codex MSBuild 环境要求。

## Caveats / Not Found

- 本轮仅写研究记录；未修改产品代码、未激活任务、未构建、未启动 GUI、未进行 git 操作。
- 已证实显隐管理冲突，但故障现场四窗可见性、初始矩形滞留、Hide-before-Show 顺序和真实 StylusDown 截获仍无运行时证据。
- 用户授权延续原任务及提供修复方案，不等于已执行或验证修复；进入实施与 GUI 验证须按主会话及用户授权范围处理。
