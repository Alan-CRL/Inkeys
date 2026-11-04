# Window / Whiteboard 背景实现复审

## 范围与结论

审阅冻结的 Window.cpp/.cppm、Window.CanvasPolicy.h、Window.ZOrderPolicy.h、Whiteboard.cpp/.cppm、新 Whiteboard.BackgroundState.h 和 window_tests.cpp；对照本任务 PRD/design/implement、两份状态机合同及 native/ppt 规范（含直接读取超过注入截断位置的新增章节）。本报告只确认此范围静态复审；Coordinator/Host/UI 跨层结论另见相应报告。

本轮指出的 Native/BG 阻断已由所属实现者修复；最后复核 Whiteboard.cpp:144 已删除“role 已替换即可退休 held”的条件。没有发现此冻结范围剩余的确定阻断。本复审者没有修改产品或测试，没有运行 GUI、进程/窗口/PDB 采样或构建/测试。

## Findings (fixed)

修复均由 Native 实现者完成，本复审者只读确认：

1. Window.cpp:2609、2736 与 Window.cppm:97：Prepared 保存 owner 的 safetyRevision；Complete 在写输入/显隐前核对准备时安全覆盖身份。Prepare 后接受的 Hide/开启穿透不会被旧同版本 Complete 清除。同步重入安全动作先锁存，由原 owner 重试负责；普通新政策不越过未完成 Recovery。
2. Whiteboard.BackgroundState.h:39–108：真实关闭释放 owned/held；新的 inactive 版本不会再次隐藏 Desktop/PPT 已复用的 Freeze。非零版本建立后 legacy 0 不能覆盖，显式生命周期 Reset 才重新允许旧入口。请求 serial 继续拒绝相同数字版本的生命周期 ABA。
3. Whiteboard.cpp:135–174：渲染前绑定实际 Freeze HWND/generation；inactive 只隐藏精确持有身份，不能按当前 role 隐藏替代窗口。旧 HWND 仍活着、同代但 role 不同或身份读取不明时保留责任并 Retry；仅销毁或可确认的非零 generation 变化允许退休。新绑定/退休清除 Prepared，下一次需全帧而非沿用旧空 dirty。
4. Window.cpp:2901：Workspace 恢复补回 visibleBeforeMinimize。最小化/恢复在同一政策 Prepare 中执行；系统菜单只投递 view 意图，WM_SIZE 只确认，取消后仍先收束捕获旧代际的准备表面。标准 SW_SHOWNOACTIVATE 可能短暂显示锚点及 owned 窗口，设计已明确此 Win32 边界，后续显式重隐，输入过渡门继续关闭。
5. Window.cpp:536–575：恢复期间仅对实际 owner、band、above 关系已经合法的现有 owned Setting 省略普通全局 Refresh；不允许 owner attach/detach 穿过 overlay Recovery。隐藏的合法 Setting 可经自身线程显示，绘图页/故障通知不必先取得全局 TOP。

## 保留的生产合同

- Prepare 返回实际三表面身份、RECT、相关 monitor/DPI 和安全覆盖；不在 owner 等 Render。Complete 重验相关事实，Frame/Host/UI 由协调层在两次短 owner 调用间取得。无关 Display generation 变化不直接令相同物理目标失效。
- Workspace 快照恢复在公共全窗口恢复前分流，仅恢复捕获旧资源及本次写集；存活子树、代际、owner、band、PPT 原兄弟序、安全覆盖和 stop 仍核验。最小化的相关 Bar/PPT 显隐单独捕获为 viewOnly，不恢复其独立几何。
- 场景 mode/topmost/fullscreen 的物理提交使用内部相对排序；正常 Desktop/PPT 周期 Refresh 的全局策略、Setting 自身 Raise、纯 Z-only 的原序恢复继续独立。没有把前台许可、全局连续或 Shell COM 成功变成场景完成凭据。
- 固定 Mag→Freeze→Presentation→Drawpad→Bar/PPT 链、唯一 Freeze APPWINDOW、Drawpad TOOLWINDOW 与 Setting 独立线程保持。非 root 从捕获 root 继承 band，不独自抬到全局 TOPMOST。
- BG 关闭确认先于 minimized 绘制暂停；正常 active 完成另需实际 visible、bounds、非 iconic 与当前请求，Prepared 只表示像素已提交，不等于可见。完整 rcMonitor 与 DPI 来自同一快照，Desktop/PPT 既有 1 物理像素 region / NonRude 不混改。

## Tests / Quality Check 边界

- window_tests 新增调用生产 PrepareWorkspacePolicy / CompleteWorkspacePolicy 步骤编排、生产相对排序 helper、BackgroundState 和 CompleteBackgroundInactive。故障/取消/安全覆盖、旧帧、legacy、精确 held、新 generation 和关闭重复幂等有断言。
- WorkspaceSteps 的几何/样式/显隐 stage 是替身；上述测试不动态执行私有 Service dispatch、Capture/RestoreWorkspaceSnapshot、真实 USER32、HWND 创建/销毁、DWM/ULW、Shell 或实际系统菜单消息。此部分结论来自源码审查，不能称全 Service 或跨系统实测。
- 本轮主会话统一执行完整 Debug|ARM64 Solution、无窗口 CLI、格式与资产审计；最终执行结果待其记录。本复审者未自行启动编译或测试，不能以此报告声称 Lint / TypeCheck / Tests 已通过。
- 用户独立 Draw3 CSO 和旧设置任务资料不属于本范围，未触碰。Win7/8/8.1/10/11 真机、任务栏让位、前台拒绝及非常规 DPI 最终像素仍未新验收。

## Findings (not fixed)

此范围没有剩余确定缺陷。真实 USER32/驱动/Office/任务栏覆盖缺口属于明确未执行的验证限制，不是虚构的新故障，也不以 helper 测试代替。