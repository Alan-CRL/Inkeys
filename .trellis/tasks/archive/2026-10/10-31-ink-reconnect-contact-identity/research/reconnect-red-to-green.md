# 生产接管身份回归：红灯与修复

## 红灯证据（父会话执行）
- 完整 `InkeysRepo.sln Debug|ARM64` 构建退出0，日志 `Build/InkReconnectValidation/red-build.log`。
- `Inkeys.exe --draw3-control-fence-test` 退出17；`Build/InkReconnectValidation/red.stderr.log` 中17项失败均来自新增 `[Draw3ReconnectIdentity]`。
- 四个现场ID分别失败于新接触Up遗留身份和同ID再次落笔被抑制，共8项；连续接管残留与ID复用共9项。
- 原有 `[Draw3EraserIdleWake]`、`[Draw3WorkspaceTransition]` 均PASS；普通Touch、真实slot generation复用、无关Touch保留及非Touch key0未报失败。
- 红灯时生产成功接管与回归共用 `AdoptReconnectedContact`，helper只执行原有handle赋值，未补写手势key。

## 最小修复
`AdoptReconnectedContact` 在原handle赋值旁同步 `touchGestureKey`：Touch取新handle的CanvasTouchKey，非Touch归零；不清空FSM，不修改匹配算法、几何、时序或其它接触。

## 待验证
父会话执行修复后的完整Debug|ARM64构建与无窗口回归。当前未将静态分析当作绿灯，也未新增GUI/真实设备验证结论。
