# 技术设计：白板相对排序与纯层级恢复

## 证据与范围

20261021a 与 77f76038 的直接父版本：白板模式只提交 CanvasPolicy，非置顶刷新没有完整隐藏 PPT 全组置前；普通命令失败没有通用画布恢复屏障。77f76038 同时引入完整组成功硬门、两种 band 均逐成员 HWND_TOP，以及 snapshot.canvas=group。

此前获准 GUI 证据详见 research/authorized-gui-evidence-20261027.md。初次 mode 失败重试发生在背景启动前；进入后稳定 Active 但 Recovery 持续，真实隐藏 PPT 在 Bar 上方且 Primary 显隐变化。实际九成员连续，不能只移除 continuous。未捕获最早某次 SetWindowPos BOOL，因果判断基于确定的新增控制流、实际内部违约及切换前台后恢复的对照，不声称 Windows 内部调用细节已被捕获。

用户已禁止后续 GUI/运行时采样。只实施已被历史及现场共同支持的三点：白板后台内部排序、纯 Z 恢复写集、同一意图 UI 准备去重。保留真实 Draw3 首帧/背景/可见性准入，不用伪成功。

## A. 白板有限相对排序

Desktop/PPT 正常 Refresh 保留原政策。白板分支固定捕获角色 HWND+generation，核对命令及身份；必要时修复 owner 和 band，根降为 NOTOPMOST。基础 Mag→Freeze→Presentation→Drawpad 及其他 owned 关系仍核验。

Bar 保持当前全局位置，不调用其 HWND_TOP。捕获请求前 PPT 顺序，promoted 只调整目标子序列。anchor=Bar，按期望 PPT 从上到下处理；若 PPT 在 anchor 上方，仅用 NOACTIVATE|NOOWNERZORDER 相对下调到 anchor 后。每次实际写入后重新采样，防止旧 ordinal 使后续判断失真。没有 Bar 时以所需首个 PPT 为锚，只下调其他兄弟，不能把 owned PPT 放在其 owner 之下。最多两轮，身份或当前命令改变立即失败。

生产路径和无窗口测试共用 Window.ZOrderPolicy.h 内有限 Ops helper。成功要求实际 owner、band、基础内部序、Bar>PPT、PPT 子序列和代际；白板不要求外部全局连续性。Setting 不加入写序。保留既有 Shell 组绝对状态和一次显式异步激活。

## B. Z-only 恢复提前分流

Z-only 命令为 RefreshTopmost、PromotePpt、SetOverlayTopmost、SetOverlayFullscreen。快照保存角色身份、owner/band、原 PPT 子序列及对应政策旧值。Restore 在任何公共逐窗 FRAMECHANGED/Show/CanvasPolicy 之前显式分流；仅 canvas=false 不足以隔离。

纯 Z 恢复只修复实际不符的 owner/band/内部序，仅 SetOverlayTopmost 恢复其 topmost 政策、SetOverlayFullscreen 恢复其 fullscreen 政策；不写 mode/minimized、不恢复原几何/显隐/非 TOPMOST 样式，不碰 CanvasPolicy。PPT 恢复请求前兄弟序，但不刻意恢复非法 Bar/PPT 前态。

恢复核验 HWND/generation 和最新安全覆盖，不复用已过期原命令的 Current guard。恢复自身使用内部物理合同，不依赖外部全局连续或前台许可；普通 Desktop/PPT Refresh 的严格政策保持。真正物理失败仍持有原唯一恢复责任，成功后退休失败回执；禁止超时清空。后到 Hide/穿透、窗口退休/新代际、stop 不被旧快照覆盖，Shell 仍发布最新绝对状态。

完整模式/最小化/Canvas 几何或显隐事务仍走原完整恢复。不得通过全面删除恢复/准入解决闪烁。

## C. 同一进入意图只准备一次 UI

StateMonitoring 本地持有已准备 epoch 和原生模式重试时间。仅新进入 epoch 执行 CollapseAuxiliaryPanels 和 CancelPointerCapture；同 epoch 回退 Inactive/Exiting 后不重复。原生失败设置 steady_clock 250ms 期限，提前 runtime 唤醒不重复提交，判断先于设置 Entering。取消、新意图不受旧期限阻挡。真实 Active→Exiting 的清理保留。

抽取最小私有生产 helper，加入既有 --state-mode-production-test：多次失败只准备一次、虚拟时间退避、新 epoch/取消。保持 IdtState UTF-8 BOM+CRLF，不改状态、背景协议或公开 API。

## 不在本轮扩大

旧方案中的背景 token/ABA 重构、公有 Show/Hide/Bounds guard 扩展、最小化退出后台 ACK、成对 Bounds 快照及通用诊断系统不属于本次已证实回归主链，暂不实施。已有修复保持，不以顺手改动增加风险。

## 验证边界

真实生产 helper 故障注入覆盖后台排序拒绝、隐藏 Middle、外部穿插、代际/owner/band/readback、纯 Z 重试写集及安全覆盖；完整 solution、生产状态 CLI、无窗口全套验证。新结果不能证明 USER32/各系统修复后实机表现；按用户要求不再做 GUI 验收。
