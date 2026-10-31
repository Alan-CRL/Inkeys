# Window owner/Z 序后续修复（2026-10-17）

## 修改前边界

本轮禁止任何 HWND 创建、桌面操作和 GUI 启动。既有 11 条失败不能以本轮静态检查冒充通过。

已确认：原 A/B 中 SetWindowLongPtr owner 成功，根与 Setting 均 topmost，但 Setting 仍低于两个画布。PrepareSettingZOrder 只刷新根就核验相对顺序；缺少 owned Show 中已有的 Setting 带内排序操作。修复将根刷新完成后单独投递 Setting 所属线程 HWND_TOP 排序，使用 SWP_NOOWNERZORDER 保留根位置；仍完整核验 owner/band/上下关系，失败返回 false 并保留原回滚。不得给 Setting 独立 HWND_TOPMOST，不调用 Show/激活来修复隐藏 attach，不增加设置线程到 overlay 的同步等待。

官方依据：[SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)：HWND_TOP 修改顺序；SWP_NOOWNERZORDER 保留 owner 位置；topmost band 由根的既有策略管理。文档不保证一次已置顶根刷新会重建所有后来接入成员的相对顺序。实际补丁效果仍待真实 HWND 回归。

## 11 条失败映射

| 断言 | 边界 |
| --- | --- |
| competing topmost starts above continuous owner tree | attach 前已失败；连续性与竞争窗相对顺序没有原始逐项快照，未证明根因 |
| overlay root receives topmost style | RequestTopmostRefresh 返回 true 但样式断言失败；原日志没有当时根 style/错误码/外部操作身份，未证明根因 |
| hidden root refresh raises continuous owner tree above competitor | 与前两项同属根组；不能归因 Setting attach |
| setting attach owner | 原 A/B 证实后置顺序核验回滚；本补丁直接覆盖该缺口 |
| unchanged correct setting owner avoids repeated root refresh | 首次失败的级联；补丁保留幂等 |
| setting joins drawpad owner chain | attach 回滚的级联 |
| visible selection setting immediately rises above both canvases on attach | 直接覆盖相对层级准备 |
| owned setting reshow immediately stays above both canvases | 首次 attach 回滚使 Show 无 owned 顺序保证；保留 Show 验证 |
| setting capture: set setting owner | capture fixture 前置 attach 失败；本补丁覆盖准备缺口 |
| setting capture: owned setting sees other window capture in attached input queue | owner 回滚后输入队列未关联的级联；不改 capture 清理 |
| setting capture: attach before show shutdown race | fixture 前置 attach；新排序在 shutdown 后拒绝，已有 Hide 清理不变 |

三个根组断言不能由当前证据安全设计行为修复。默认测试 root/setting 使用真实 Service，无白板模式、无自定义 root WndProc，root 命令确实调用 SetWindowPos；没有观察到源码将其替换成 no-op。只新增失败时事实采样（不放宽断言），为后续获准窗口回归区分连续性、style 与 band 提供证据。
## 实施与检查

Window.cpp 新增内部 RaiseOwnedSetting owner-thread 命令：PrepareSettingZOrder 根刷新返回之后才投递；shutdown 阶段拒绝，不持 queue mutex 执行 USER32，不增加 Setting 到 overlay 的等待。仍由调用端 matches 验证最终身份/样式/顺序。实际 Show 的焦点语义不变。window_tests.cpp 新增隐藏 attach 回归与根失败状态采样，旧 11 条断言全部保留。

本子代理仅执行 git diff --check、UTF-8/CRLF/BOM 核验与调用链审查，结果通过。未启动任何窗口，未运行默认窗口测试或窗口探针；完整编译及 --no-window 由主会话串行执行。无窗口纯模型无法验证 USER32 Z 序，故不为本项复制模拟算法冒充运行验证。八条 Setting 相关断言仍待实际 HWND 回归，三条根组原因仍未确认；不宣称 11 项已全修复。
