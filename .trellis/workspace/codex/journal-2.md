# Journal - codex (Part 2)

> Continuation from `journal-1.md` (archived at ~2000 lines)
> Started: 2026-10-13

---



## Session 75: 交互模式切换入口与三语文案
<!-- trellis-session: v=2 fp=ac4f72ba4ed0e742 -->

**Date**: 2026-10-13
**Task**: 交互模式切换入口与三语文案
**Branch**: `feature/mode`

### Summary

模式选择改为直接互斥选项和实时状态；采用三语标题、名称及自适应两条说明；所有自动启动判定保留。ARM64完整solution与Headless --no-window在sandbox外通过，i18n和12组模式回放通过，独立审查无发现。未GUI/commit/push。

### Git Commits

(No commits - planning session)

### Status

[OK] **Completed**

### Next Steps

- 按用户后续需求继续产品模式与启动预设任务。


## Session 76: 修复设置模式切换卡死并恢复下拉卡片
<!-- trellis-session: v=2 fp=4d9b1fbbf61221c7 -->

**Date**: 2026-10-13
**Task**: 修复设置模式切换卡死并恢复下拉卡片
**Branch**: `feature/mode`

### Summary

定位ImGui锁内同步等待Setting HWND的循环等待，统一Sync入口延后到现有FIFO，覆盖间接橡皮setter；恢复下拉与信息说明卡片，审查补齐原子模式字段/单次load。最终ARM64 solution、Headless、生产线程回归和12组三语缩放回放通过。未GUI/commit/push。

### Git Commits

(No commits - planning session)

### Status

[OK] **Completed**

### Next Steps

- 按用户后续产品模式与启动预设需求继续当前任务。


## Session 82: 启动位置预设与环形启动进度实现
<!-- trellis-session: v=2 fp=8b24b34ce4bf28e8 -->

**Date**: 2026-10-13
**Task**: 启动位置预设与环形启动进度实现
**Branch**: `feature/mode`

### Summary

三种启动位置与24 DIP等边距、Mini冻结、角落折叠初始化、环形进度和三语预设页面完成；完整ARM64/Headless/独立审查通过，保留当前任务进行中。

### Main Changes

- main.json新增UI.Bar.StartupPlacement，预设combo仅下次启动生效，Mini同一选择传Preview及Bar。
- 共用工作区/DPI/zoom/描边几何，角落80 DIP超椭圆与终态初始化，缓存始终发布展开宽度；三位置32/3 DIP确定式环形、保留反光与顺序交接。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 完整InkeysRepo.sln Debug|ARM64与InkeysHeadlessTests --no-window均exit0；i18n340/340、12组页面/侧栏回放、生产状态提取回放、40份真实D2D/WARP像素和独立Trellis gate PASS。
- [OK] 编码/BOM/CRLF及git diff --check通过；未GUI验证真实HWND首帧/嵌入字体/现场混合DPI，边界已记录。

### Status

[OK] **Completed**

### Next Steps

- 任务保持in_progress，等待用户下一轮启动预设反馈或修改；本批不commit/push/归档。

2026-10-14：用户授权提交上述启动位置预设改动；保留任务 in_progress，不执行归档或 push。


## Session 75: 阶段三现场8笔日志分析与修复规划
<!-- trellis-session: v=2 fp=96501f25e5683a7b -->

**Date**: 2026-10-13
**Task**: 阶段三现场8笔日志分析与修复规划
**Branch**: `bugfix/penstroke`

### Summary

现场8笔均求解但无Apply：3笔候选末角超10度、5笔位移14–30px超3px；规划有界完成态重采样、来源边界映射和自适应形变预算。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 完整8条日志解析与源码分支核对，独立研究审查规划；仅文档/离线统计，本轮不构建、不改产品算法。

### Status

[OK] **Completed**

### Next Steps

- 按现场回归→曲线/采样与预算→窗口→完整链验收执行；阶段三人工未通过。


## Session 83: 交互模式与启动预设人工验收结案
<!-- trellis-session: v=2 fp=1c19ac5831005d06 -->

**Date**: 2026-10-14
**Task**: 交互模式与启动预设人工验收结案
**Branch**: `feature/mode`

### Summary

用户确认人工测试通过，提交最终动画样式与安全故障测试入口，完成并归档interaction-modes-startup任务；未push。

### Main Changes

- 交互模式切换死锁与三语命名、三种启动位置及Mini冻结完成；居中保留旧条形进度，角落使用环形，反光统一45度。
- 故障观察入口通过现有测试编译门和精确双环境值隔离，默认Release不编入；仅一次重试，无循环或配置写入。

### Git Commits

| Hash | Message |
|------|---------|
| `ecfee6ab` | fix(settings): resolve interaction mode switch deadlock |
| `18b4c8fb` | feat(startup): add placement presets and circular preview progress |
| `519afa01` | fix(startup): restore preview styling and gate failure tests |

### Testing

- [OK] 完整Debug|ARM64 solution、Headless --no-window、独立审查及75份当前离屏像素通过；最终人工验收由用户于2026-10-14确认。
- [OK] 圆角像素断言误采样经真实BMP对照修正；编码/CRLF与git diff检查通过，原始日志保留ignored Build目录。

### Status

[OK] **Completed**

### Next Steps

- 当前任务已完成归档，当前会话任务指针清除；其他任务不变，不推送。


## Session 76: Draw3可见末钩自适应窗口与原始输入内侧辅助
<!-- trellis-session: v=2 fp=168a05e574b521cc -->

**Date**: 2026-10-13
**Task**: Draw3可见末钩自适应窗口与原始输入内侧辅助
**Branch**: `bugfix/penstroke`

### Summary

修复现场8笔无发布；自由末向、有限扩窗、raw提示入口和防外鼓约束；第6/8笔真实C++同尺度对比。Debug/Release完整构建、8284回放、128412基线、334062产品probe和Headless通过。阶段三继续等待人工验证，不提交。

### Main Changes

- 有序重采样及r/time和committed边界映射；模型物理窗口与raw独立；保留反色和有界日志

### Git Commits

(No commits - planning session)

### Testing

- [OK] Debug/Release输出case精确一致；Modeler原基线SHA256不变；PE shader资源301–304有效
- [OK] Release完整Finalize预热后30轮，中位0.102ms；1000点稳定前缀0.32705ms，诊断I/O关闭

### Status

[OK] **Completed**

### Next Steps

- 人工检查第6/8笔内缩过渡及第3/4筆较长修正范围；阶段三不归档


## Session 97: 选择与穿透操作模式完成
<!-- trellis-session: v=2 fp=b5e7f8d8dc9d5d03 -->

**Date**: 2026-10-14
**Task**: 选择与穿透操作模式完成
**Branch**: `feature/mode`

### Summary

合并式/分离式、稳定穿透与历史按钮显隐、紧凑三语说明和穿透再次点击恢复工具均完成；用户确认完成，任务已归档。Debug ARM64和无窗口回归通过，未执行GUI测试或push。

### Git Commits

| Hash | Message |
|------|---------|
| `3f3946cf` | feat(mode): add combined and separate click-through modes |
| `d444f6a7` | fix(mode): stabilize toolbar visibility and tighten mode guidance |
| `7d5a003f` | feat(mode): toggle click-through back to remembered tool |

### Status

[OK] **Completed**


## Session 98: 完成主栏图标顺序与展开方向任务
<!-- trellis-session: v=2 fp=0a06fb1ca7cabb88 -->

**Date**: 2026-10-14
**Task**: 完成主栏图标顺序与展开方向任务
**Branch**: `feature/mode`

### Summary

主栏排序与展开策略、拖动释放第三光源、按钮统一状态派生均完成；自动验证通过，用户确认人工验收通过，任务已归档。

### Main Changes

- 默认位置不足时换边，四种图标横向顺序；保留现有停靠与上下倒转。
- 修复第三光源拖动释放闪烁，以及分离式增删组件覆盖定格2×1；主栏尺寸/显隐统一派生、等宽动画失效和历史同帧视图。
- 用户于2026-10-14确认修改到位及人工验证通过；feature/mode提交完成，任务completed并清除活动指针，未push。

### Git Commits

| Hash | Message |
|------|---------|
| `35afa7ee` | feat(toolbar): add icon order and expansion direction settings |
| `2dd3b344` | fix(toolbar): keep cursor light stable after dragging |
| `8515d469` | fix(toolbar): centralize button presentation updates |
| `bec78a29` | chore(task): archive toolbar-order-expansion |

### Testing

- [OK] 最终Debug|ARM64完整solution退出0；Bar离屏480组组件矩阵失败0；Headless --no-window与state-mode-production退出0。
- [OK] 第三光源24坐标组/4组D2D像素差异0，diff/UTF-8/BOM/CRLF与Trellis检查通过；人工验证由用户确认。

### Status

[OK] **Completed**


## Session 77: 修复近直线抬笔回退误触发
<!-- trellis-session: v=2 fp=ee321ec749819105 -->

**Date**: 2026-10-13
**Task**: 修复近直线抬笔回退误触发
**Branch**: `bugfix/penstroke`

### Summary

用户限定只修1/7，6暂不处理。以真实法向距离替代截断角度可见性估算，保留异常检测与既有拟合。现场55和多方向回退NoChange，可见钝角仍Apply。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 8421回放、128412基线、完成态集成及334062产品probe通过；原8笔输出精确不变；完整ARM64 Debug产品构建通过

### Status

[OK] **Completed**

### Next Steps

- 使用更新后的Debug程序人工复测1/7，阶段三保持进行中，不结束或提交


## Session 78: 修复原始输入支持的小回退导致短1误修
<!-- trellis-session: v=2 fp=a6f37666585eb2ba -->

**Date**: 2026-10-13
**Task**: 修复原始输入支持的小回退导致短1误修
**Branch**: `bugfix/penstroke`

### Summary

精确对应33568日志26笔与截图cyan范围；旧严格顶点门误将真实输入小回退当模型末钩。新增有序尾部双向连续距离保护，修复前97px扩窗改为NoChange。6暂不处理。

### Main Changes

- 唯一原始锚点/Up一致性、1-Lipschitz有界内部检查及旧早退安全修订；反色保留

### Git Commits

(No commits - planning session)

### Testing

- [OK] 9245回放、128412基线及334062产品probe通过，原8笔完整输出精确不变；完整ARM64 Debug构建通过

### Status

[OK] **Completed**

### Next Steps

- 更新Debug产品人工复测；阶段三继续in_progress，不提交和归档


## Session 79: 阶段三人工效果验收与提交准备
<!-- trellis-session: v=2 fp=dea01dd78b836724 -->

**Date**: 2026-10-13
**Task**: 阶段三人工效果验收与提交准备
**Branch**: `bugfix/penstroke`

### Summary

用户确认最新短1误修已解决并授权提交。记录阶段三核心效果人工通过，提交当前自适应重构、原始路径保护、产品接入和全部回归证据。下一步关闭临时调试并做发布/生命周期验收。

### Main Changes

- 父子任务人工验收状态同步，未关闭阶段三或创建新任务

### Git Commits

(No commits - planning session)

### Testing

- [OK] 最终产品哈希仍d6d4ed7b；既有9245算法、128412基线、334062产品probe和人工反馈有效，无新源码修改不重复编译

### Status

[OK] **Completed**

### Next Steps

- 阶段四工程收尾：默认正常颜色/日志策略、接续和存储、最新Release多架构及性能；物理Up预览按实测需要决定


## Session 80: 阶段四正常颜色抬笔长斜线调查与显式诊断交付
<!-- trellis-session: v=2 fp=4f541197f3798382 -->

**Date**: 2026-10-13
**Task**: 阶段四正常颜色抬笔长斜线调查与显式诊断交付
**Branch**: `bugfix/penstroke`

### Summary

阶段三签名提交重试成功；阶段四人工发现抬笔长斜线，已交付默认关闭且正常颜色的有界来源与完成几何诊断，根因未确认，用户稍后测试，任务不归档。

### Main Changes

- 默认反色与轨迹I/O关闭，显式INKEYS_DRAW3_TAIL_TRACE=1收集输入与最终Stored；接续身份、旧终态和PID复用日志已审查修正。

### Git Commits

| Hash | Message |
|------|---------|
| `71858b0b` | fix(draw3): reconstruct pen-up tails and preserve raw-supported returns |

### Testing

- [OK] 完整Debug ARM64 solution、334115生产断言、诊断开关几何一致、control fence、模块9245/128412基线、生命周期/UInk通过；未运行GUI或Win7。

### Status

[OK] **Completed**

### Next Steps

- 等待用户正常颜色复现日志，定位长线首现阶段并建立回归后修复；随后继续最终Release/跨架构及人工验收。


## Session 81: 阶段四验收完成、签名提交与父子任务归档
<!-- trellis-session: v=2 fp=7480dca109fa79a0 -->

**Date**: 2026-10-13
**Task**: 阶段四验收完成、签名提交与父子任务归档
**Branch**: `bugfix/penstroke`

### Summary

阶段四工程验收及鼠标、实体笔、触摸人工验收通过。现有SSH签名重试成功，完成工作提交和阶段三、阶段四、父任务归档；旧偶发斜线与6小末钩按用户决定暂缓，不宣称已修复。

### Main Changes

- 默认关闭临时反色和尾轨迹I/O，保留显式开启的128条/2MiB正常颜色诊断与真实接触、Up、完成态Stored关联。
- 保持已有尾段算法、参数、Modeler公共API、vcpkg Abseil 20250512.1#0、CRT和工具集，补充完成态存储、历史、接续及Reset回归。
- 签名工作提交62f1e6b；归档完成任务并校正上下文路径，五个阶段/父任务manifest验证通过，活动任务指针清空。
- 保留程序/PDB、输入、报告、依赖和当前ARM64 Debug缓存，释放9个Release obj目录共6.500GiB；此前旧缓存约2020.6MiB。

### Git Commits

| Hash | Message |
|------|---------|
| `62f1e6b0d0bd67bb084d958da7fb9cafd6ed12b9` | fix(draw3): finalize tail diagnostics and release validation |
| `17694165fcefcfb07d963a8c3d7db1cf56eb3fc5` | chore(trellis): archive completed Draw3 tail tasks |

### Testing

- [OK] ARM64 Debug/Release、x64 Release、Win32 Release主solution及demo/test共8次构建退出0；各配置生产334115、尾段9245、原Modeler128412断言均0失败，精确基线hash一致。
- [OK] 最终InkeysHeadlessTests --no-window退出0（98.36秒）；控制围栏、保存/读入、undo/redo、composition、等待/接续/Reset回归通过。
- [OK] 四架构shader资源301-304为有效非空DXBC；x64/Win32产品及demo/test静态imports与原基线无增删；默认/on诊断几何一致。
- [OK] 用户确认鼠标/实体笔/触摸未出现异常；38916的92条Mouse和9812的107条Pen完整日志检查通过，触摸体验来自人工确认。
- [OK] 无诊断I/O完整FinalizeStoredStroke预热后30轮：ARM64 Release中位0.0471ms（无前缀）、0.1464ms（1000点前缀），不含GPU或端到端Up延迟。

### Status

[OK] **Completed**

### Next Steps

- 本次四阶段范围完成，无需继续修改；旧事件再现时用显式诊断捕获并建立对应回归。真实Win7、D3D Debug Layer、Release GUI及原生x64/Win32体验未执行，不推送远程。


## Session 82: Draw3尾段修复最终人工验收确认
<!-- trellis-session: v=2 fp=c4f423fa866d747d -->

**Date**: 2026-10-14
**Task**: Draw3尾段修复最终人工验收确认
**Branch**: `bugfix/penstroke`

### Summary

2026-10-14用户明确宣布人工验证通过并要求结束任务及提交。已在归档父任务、阶段三、阶段四及既有验收报告补记最终人工确认，维持completed，不新建阶段五。

### Main Changes

- 增加最终人工验收通过标记及2026-10-14确认日期，保留原工作提交和归档日期。
- 本次仅更新验收文档和任务元数据；旧偶发斜线与数字6暂缓事项保留，不追加未说明的设备或平台覆盖声明。

### Git Commits

| Hash | Message |
|------|---------|
| `e154707ce72e95c0c4c20297afefbbaa07f04d14` | docs(draw3): confirm final manual acceptance |

### Testing

- [OK] 三个已归档任务的JSON与Trellis上下文校验通过；git diff --check通过，修改保持原编码与换行，仅4份验收记录。
- [OK] 纯验收记录更新，未修改代码或构建配置，未重复编译、运行测试或打开GUI。沿用阶段四已通过的构建和自动验收证据。

### Status

[OK] **Completed**

### Next Steps

- 任务已完成并归档，继续正常使用或按原流程发布；问题再现时采集诊断证据。未推送远程。


## Session 110: PptCOM 修复提交归档与覆盖层置顶调查
<!-- trellis-session: v=2 fp=a934026df8791edd -->

**Date**: 2026-10-18
**Task**: PptCOM 修复提交归档与覆盖层置顶调查
**Branch**: `bugfix/pptcom`

### Summary

按用户授权提交 PptCOM 修复并归档任务，保留实机验收未验证；只读核查覆盖层 owner、根置顶与 PPT 穿插风险。

### Main Changes

本次按用户明确要求创建本地提交并结束 PptCOM Trellis 任务；保留 SSH 签名，不执行 push。

- WORK: 0eb1c7c587b8b284d103ae865ea29ea59e444677，dynamic 属性/方法访问、确定性 COM 所有权、raw IDispatch 事件连接和解绑重试、busy/基础控制与墨迹能力隔离、测试及 DLL 同步。
- ARCHIVE: 184cf86e，任务已 completed 并移入 .trellis/tasks/archive/2026-10/10-18-pptcom-dynamic-compatibility，清除活动指针并关联 WORK 提交。
- 既有最终验证：x64/ARM64 托管真实 COM 各 43 cases、0 failures；完整 InkeysRepo.sln Debug|ARM64 和同一产物 --no-window 通过；DLL/嵌入资源哈希匹配，TLB 与原版一致。
- 本轮仅更新收尾记录及修复新文档 EOF 空行；暂存 diff --check 通过。未重复运行已完成构建，未启动 GUI/Office。
- PRD AC10 与真实 PowerPoint2007/2010、WPP2013、现代 Office、损坏 COM 反馈设备、进程及时退出及下一文稿实机验收仍 NOT VERIFIED。按用户指示归档不等于将其标成通过。

用户另要求只读调查覆盖层置顶。此次未改动该功能，也未新建 Trellis 任务。

静态确认：
- 实际编译 Inkeys/Inkeys/Window/Window.cpp 与 Window.Legacy.cpp；Inkeys/IdtWindow.cpp 仅为 None 项，不作为当前置顶实现依据。
- OverlayRoot 优先 MagnifierHost，其次 Freeze/Drawpad/Bar；普通链为 MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad。Bar 及四个 PptBottom/Middle Left/Right 均以 Drawpad 为 owner，是同级顶层 WS_POPUP，而非互相拥有的链。
- Window.cpp:1870 RefreshTopmost 只对根调用 HWND_TOPMOST/HWND_NOTOPMOST，SWP_NOACTIVATE；keepTopmost = overlayTopmost && !whiteboardWindowMode。Win32 将 topmost band 传播给 owned popup，所以“没有对每个窗口独立 TOPMOST”不代表上方窗口不是 topmost。
- Window.cpp:1696 与 1890 的 Show/PromotePpt：先 Bar HWND_TOP，再将目标翻页窗插到 Bar 后；未显式逐一对四窗和两个画布排序。USER32 仍可能连带调整 owner，不能将两次 API 调用理解为只会移动两个 HWND。
- PageControl.cpp:2004 每个成功可见呈现独立调用 Show，2325 pointer down 调用 PromotePptWindow；DirectMove 使用 NOZORDER/NOOWNERZORDER，移动自身不负责修复被外部改变的顺序。
- Ppt.cpp:30/243：首次 visible 提交请求根刷新，失败继续 pending；成功清除 pending，visible 保持 true 时不因外部遮挡重触发。
- Window.Legacy.cpp:155-189 周期根刷新，ticks*10ms；配置默认 topSleepTime=3，即 3 秒，可选 100ms/500ms/1s/3s/5s/10s/30s。topWindowNow 可提前唤醒等待。
- IdtPlug-in.cpp:478-502 业务操作可将焦点交给当前 PPT；没有在交接成功后立即刷新整组窗口。SetForegroundWindow 本身不证明 PPT 是 topmost。
- Window.cpp:1878-1887 返回值依据根 SetWindowPos，未复核全组 style、GW_OWNER 或相对 PPT 的实际 Z 序。Window.Legacy.cpp:49-76 仅失败/恢复转换采样整组，Ppt.cpp 日志采根；API 成功而视觉遮挡不一定触发诊断。
- 白板显式 NOTOPMOST，IdtState.cpp:1833 进入降层，1968 退出恢复；需区分是否发生白板切换，而非把所有情形等同根刷新失败。
- 仓库 window_tests.cpp:507-532 已有隐藏 ownerless topmost 竞争窗及根刷新后整组高于竞争窗/连续性的断言；本轮未执行创建 HWND 的测试，因此不能当作本轮实测结果。

判断及边界：
- owner 规则保障 owned 高于自己的 owner，不承诺外部窗口永不进入 owned 同级窗口之间；topmost 保障高于普通窗口，不能保证永远高于另一个 topmost 窗口。
- “主栏/右翻页在 PPT 上，左翻页/画布在下”与外部层级变化后局部显示/提层、缺少整组实际顺序校验的机制相容，但还没有故障时刻窗口快照，不能据此宣称该次具体根因已经证实。更不能只凭没有显示判断窗口真的在下层。
- 若当时 PPT 非 topmost 且覆盖层所有窗口确实 topmost/可见/有内容，普通 Z 序不应造成该遮挡；应检查模式降层、owner/style 被改变，或者显隐/呈现失败。COM basic 降级也可能保留翻页而不启用墨迹画布，不能混为同一证据。
- 后续若继续修复，应先在异常时记录全组与 PPT 的 hwnd/PID/TID、GW_OWNER、topmost/visible、bounds、前后台 Z 邻居及绘制/白板/放映能力、present 结果，核对根刷新/单窗 Show/点击提层/焦点交接的前后顺序；不直接改成所有节点独立 TOPMOST。

参考原始资料：
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
- https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows
- https://devblogs.microsoft.com/oldnewthing/20051121-10/?p=33263


### Git Commits

| Hash | Message |
|------|---------|
| `0eb1c7c5` | fix(pptcom): restore dynamic compatibility and deterministic COM cleanup |
| `184cf86e` | chore: archive pptcom dynamic compatibility task |

### Status

[OK] **Completed**
