# 技术设计：三态失败补偿与 DComp 生命周期

## A. 源预览补偿

在 `IdtState.cpp` 增加私有恢复步骤，读取当前协调快照和最近命令回执。只有 `CommandPhase::Rejected` 证明原命令已失败且 owner 恢复已退休后才执行；Executing 不处理。仅当 `committed == Whiteboard && !committedMinimized` 时恢复 expanded、页状态、Freeze surface ownership 和当前 revision 的背景 active，并继续要求真实 prepared/visible/bounds ACK。Pending/failed、输入 fence 和显式重试保持不变，不恢复 Bar/Committed，不循环 Show/Hide。

## B. 等待故障

在 StateMonitoring 局部维护等待阶段和进度签名：runtime revision、prepared product revision、输出 revision、背景请求/ACK。相同签名按现有 250/500/1000ms 退避计入失败预算，实际进度重置计数。非原生等待与 Native Cancelled/Expired 分开。用生产纯 helper 和虚拟时钟在 `--state-mode-production-test` 验证，不复制协调状态机。

## C. 有界 DComp 退休

运行期 DComp reset 分为解挂提交和退休等待。独立 worker 持有共享退休状态，等待 `WaitForCommitCompletion`，默认期限 1000ms。成功后释放引用；超时返回 `CommitRetirementTimedOut`，保留隔离状态，禁止第二 worker、ULW 和新 HWND，进入受保护退出。状态不得借用栈或未保活 presenter，迟到完成只清理旧状态。生产接缝注入成功/失败/阻塞/迟到完成。

## D. 真实 ULW 回退

退休成功后先尝试原 ConfigureWindow。若实际样式允许清除，沿用原回退；若拒绝，将 `RequiresLegacyWindowRebuild` 传到产品恢复层，复用 `StartDraw3ProductWithFallback` 的 StopProduct→Window owner join→清样式→重建窗口链→`allowDirectComposition=false` 路径。重启必须保存当前文档身份，重取工作区/PPT target 和新首帧。旧 generation 的 completion 不得安装；完整重建失败进入受保护故障。

## 验证边界

headless 验证生产 helper、实际恢复入口、身份和写集，不模拟 USER32 成功。完整 Solution、生产 CLI 和 no-window 为交付门槛。人工正常切换和真实 DComp 故障另行记录，Win7 SP1+KB2670838 到 Win11 不由本机结果外推。
