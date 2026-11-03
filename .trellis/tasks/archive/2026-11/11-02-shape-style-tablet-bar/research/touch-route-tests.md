# R2 Touch 路由集成测试

本轮仅修改 `InkeysHeadlessTests/ui_pointer_tests.cpp`，新增 `RunTouchRoutingTests()` 并从既有 `RunUiPointerTests()` 末尾调用。既有 providerLease、Mouse、Raw、haptics 与诊断测试保持原样。

## 测试边界

`TouchRouteFixture` 拥有五份真实 `Touch::Context`。唯一替换项是 `Touch::Testing::SetCandidate` 提供的原生命中 HWND；注册、代次、选择、投递、退休及取消直接调用生产 `UiPointerInput.cpp` 实现。fake HWND 不创建窗口；adapter 只记录调用并同步触发确定性重入，不复制路由算法。析构时先移除回调，再在本地 Context 存活期间调用 `Touch::Testing::Reset()`。

单 UI 接触政策保持不变，不引入五接触并发。页面业务时机、实际 lease/capture 和真实 worker 由主实施者的生产探针负责；此处只断言路由是否进入 adapter、身份/原始元数据及生命周期。

## 覆盖组与源码锚点

| 锚点（ui_pointer_tests.cpp） | 覆盖内容 |
| --- | --- |
| 43 / 133 | 五本地 Context fixture 与测试入口 |
| 140 | 原接收窗 Bar，实际 Down 目标 Page；后续换接收窗/悬浮目标仍锁定 Down owner；完整 TOUCHINPUT、origin、messageTime、semantic 保留；同 range 同 ID 多次切换目标，各本地 generation 独立 |
| 188 | 未登记 native 候选、Miss、Blocked 不投递，不搜索后方 UI；Down 在 UI 外后移入不补 Down |
| 205 | 跨 origin 重复 Down、重复 Move/Up 去重；同毫秒不同坐标 Move 保留；退休后的旧 Down 拒绝；同毫秒 Up 后合法新 Down 可开始 |
| 228 | Down100/Move120/Up119 正常收尾，轨迹不回退但原始 TOUCHINPUT 不改写；新 Down130/旧 Up125 不结束新代 |
| 244 / 254 | TOUCHINPUT.dwTime 恒零而原生 messageTime 推进仍能反复接触；DWORD 原生时钟回绕 |
| 267 | 不同 hSource 同 ID 不夺取/结束现接触，前一接触结束后可正常开始 |
| 281 / 291 / 305 | 同批 primary 优先于旧 fallback Up；Blocked primary 不取消有效 fallback；Accept primary 只取消一次；无 primary 时仅首 fallback |
| 311 | 大于 32 位的 generation、无 native capture ticket 仍能取消；迟到旧 cancel 不覆盖当前有效 cancel，也不清后继代次 |
| 330 | 同 HWND 重登记：旧 registration cancel、旧终态不清新接触 |
| 345 | 生命周期绑定不可变 Down origin，后续 Move 接收窗不成为生命周期 owner |
| 356 / 372 | cancel 回调同步新 Down；CancelWindow 按调用开始的身份快照取消，不清回调中新建且同 origin 的接触 |
| 385 | RetireTarget 回调重建同 HWND 登记，外层退休返回不擦除替代登记 |
| 397 / 412 / 426 | Preparing deliver 中取消+新 Down、重登记+新 Down；Up deliver 中取消+新 Down；旧外层返回不覆盖新状态 |
| 441 / 456 / 464 | Probe 中同 registration 完整重入接触改变 local mutation；Probe 与 native candidate 回调中重登记；原始选择均失效 |
| 474 | B 接管捕获引发旧 origin A 的 CancelTargetWindow(A) 时，不能清 B 的 Preparing；源 A 隐藏的 CancelWindow(A) 仍取消 B |
| 492 | adapter 拒绝 Down 回滚一次；第六登记槽被拒绝，固定五槽 |
| 1569 | 在原 UI pointer 测试入口中运行路由用例 |

## 核对与验证状态

- 已静态逐项核对当前生产 API；通过 `git diff --check -- InkeysHeadlessTests/ui_pointer_tests.cpp`。
- 原文件保持 UTF-8 无 BOM、CRLF；检查未出现裸 LF 或重复 CR。diff 只有新增 include、fixture/用例与入口调用，无原有用例删除。
- 按分工未执行编译、测试程序或 GUI，不能将本记录视为用例运行通过；由主会话进行 solution/headless 验证并根据实际输出复核。
- 向主实施者反馈的 Candidate/Probe/Retire/CancelWindow 重入反例及 capture loss 与 origin hide 的区别均已纳入上述真实 helper 回归。
