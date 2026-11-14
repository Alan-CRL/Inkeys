# 多文稿清理与重绑行为核对

用户正在测试最终关闭候选，本轮仅只读核对并记录，不修改正在测试的业务代码或 EXE。

## 清理范围

FullCleanup（PptCOM.cs:528）失效当前 binding/会话缓存，停用并解绑本服务对 Application 的事件，按 window → presentation → application 配平本模块长期取得，字段置空、页码置 -1，并做原 GC。没有遍历并关闭 Application.Presentations，也没有 Quit/Close/FinalReleaseComObject。GC 作用于 Inkeys 的托管进程，不是进入 PowerPoint 进程删除其文稿。

因此 App X 下 A/B/C 多文稿，关闭 B 不会由本代码主动关闭 A/C；但若当前绑定的是 A，Application 级 2031 通知仍会触发 A 的联动绑定清理。当前 raw 回调只处理 DISPID 和 binding，没有区分事件参数 Pres 是否等于当前 pptActivePresentation。

## 当前重绑链

1. ObserveBoundShow:1395 见最终关闭标记后返回 false。
2. PptComService finally 停止 owner/完成旧请求，FullCleanup 及 CP 重试后返回 0 或 -1。
3. Native GetPptState（IdtPlug-in.cpp:552–555）对 tmp<=0 等待 21×100ms，即约 2.1 秒，再创建新的 PptCOMServer。不是创建新的 PowerPoint.Application。
4. 新服务从 ROT 枚举 PowerPoint/WPS Application，按各 Application.ActivePresentation 打分：有活动文稿 1，有其放映窗口 2，活动/前台关联放映 3。
5. 严格 currentPriority>highestPriority 才替换 best，相同优先级由 ROT 遍历先后决定；同一 Application 内不逐个遍历所有 presentations 选择目标。
6. MaintainBinding（PptCOM.cs:1247）以 bestApp.ActivePresentation 建立新绑定。原应用还在且只有一个合适候选时通常会回来，但没有保证回到原 Application。

## 已确认的绑定副作用/边界

- 清理/重绑本来就存在，但本候选扩大了触发范围：旧逻辑主要在当前文稿变化、断连或 Count0 时清理；现在绑定 Application 内任一文稿的最终关闭通知都让当前服务退出，即使被关闭的是不相关后台文稿。
- 页码/descriptor/会话与命令会暂时失效；至少有原 2.1 秒重试等待，外加 GC/ROT/COM 耗时。不能承诺关闭后台文稿对当前 A 的联动无感。
- 旧绑定在时的 targetPriority/ShouldKeepCurrentBindingForSamePresentation 保护，在 FullCleanup + 新服务后已没有当前 root 可用，不能保留原实例的相同优先级黏性。
- 多个 PowerPoint/WPS 实例并存或同优先级候选时可能重新选中另一实例。单个 Application 多文稿也只取 ActivePresentation；不是所有放映文稿的完整仲裁。

## 后续应收窄的方向（未实施）

在退出阻塞的实机验证完成后，考虑区分最终关闭的 Pres 与当前绑定：仅相关文稿关闭时退休；不相关文稿关闭时保留当前绑定。识别不能通过关闭中的对象模型查询，原始事件标识/健康期身份需独立设计并验证所有权。

必要退休后的重新发现应考虑原实例优先，使用纯值线索，不能为记住目标而强持有旧 Application RCW，否则会重新阻碍最后文稿退出。无法恢复原目标时再走现有全局优先级。

不能简单改成永久保留 Application/事件 CP；最后一份文稿关闭时仍必须释放这些取得。
