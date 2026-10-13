# 再次短1误修：精确现场回放与修复

2026-10-13。沿用阶段三任务，保持人工验收门；6按用户要求暂不处理。未运行GUI、未commit/push。

## 现场对应及根因

`Inkeys-Draw3-tail-33568.log`第26笔与最新截图短1精确对应：原模型起点(104.995,1645.984)、修正起点(103.928,1661.985)、Up(91.011,1749.014)，减去截图画布偏移y=1032后，分别对应起笔y614、青色起点y630、末点y717。不是只凭记录顺序猜测。

测试时EXE为此前0a4ac855…版本，进程/日志时间晚于生成时间。日志头的10:48日期来自未变化的几何TU，不能据此推断用户运行旧版。新增fixture用当前修复前求解器复现Apply，重构97.418px，最大位置变化5.067px。

raw本身记录了小回退，模型pin起点等于raw11，随后raw12和Up形成真实尾部输入分支。其与模型尾边的双向路径差异不到产品实际可见尺度0.6px。旧raw一致性保护只容许0.15px（可见尺度的1/4），且仅检查顶点，因此把这一输入支持的小回退当成模型异常，扩窗修改了正常前段。改用稳定切向单独不足以解决。

## 修复及保护边界

在可见末端异常进入拟合前，确认模型/原始末点均为实际Up、原始尾段存在唯一可信起点，然后在该原始后缀与模型末段间验证双向连续距离。使用距离函数1-Lipschitz上界和double中点有限细分，线段内部也必须通过；无法证明则继续原拟合，匹配预算耗尽则原子拒绝。

旧顶点严格吻合判据仅作为证据标记，不再独立早退；它可为仅含直接末边的真实输入角点提供支持，但仍须连续和Up检查。少于3个raw点、仅Up、原始分支不可信时不能使用新保护。没有修改全局可见阈值、拟合权重、扩窗参数、公共API或13个上游TU。

## 验证

- 既有测试solution Debug ARM64编译成功；9245项算法检查、0失败。完成态集成0失败。
- 现场26笔NoChange、空patch；旋转和3倍等比例变体同样保持。相同案例无raw、仅Up、两raw仍Apply。
- 原始输入可见绕行反例不被端点吻合掩盖；不匹配Up、35次匹配预算耗尽、实际末端90°角点等回归通过。
- 原8笔case完整JSON与修复前精确相同，原Modeler128412项检查通过，基线SHA256仍为0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646。
- 完整`InkeysRepo.sln Debug|ARM64`构建退出0，产品SHA256：d6d4ed7bdd2841cdc24c939876bb7acbd4baedb11e03bf94db02a71c276b27f3。
- 新产品`--draw3-pen-taper-test`退出0，334062项检查、0失败。
- 独立检查见remaining-straight-check.md；diff检查通过，C++文件CRLF保持。

回放报告在`Build/modeler-baseline/stage3-source-native/ARM64-Debug/remaining-straight-before/`及`remaining-straight-final/`。产品probe在`Build/modeler-baseline/stage3-main-native/ARM64-Debug/remaining-straight-product-probe.log`。

效果图`Build/terminal-tail-preview/remaining-straight/before-after.png`只绘制上述实际C++输出，左右同坐标；不是另一次离线拟合或GPU截图。

本轮更新Debug测试程序；未重新构建Release、其他架构或重复无直接依赖的Headless全套。之前的全套结果不能冒充本轮执行。阶段三等待用户实体Draw3复测，不宣布所有其他输入情况已穷尽。

## 用户人工确认（2026-10-13）

用户确认最新问题已解决并授权提交。该确认补足本次短1现场误修的设备效果验收；不将其外推为所有输入/工具/平台的验证。临时反色和日志仍在当前代码内，下一阶段关闭后须复核正常颜色与发布版本。
