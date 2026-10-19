# 吸附体验修订测试：20DIP双条件

2026-10-18，用户在首轮自动验证后追加授权。现行合同为design.md第4节、PRD AC9/11/16/17及research/snap-revision-design.md；本记录不延续旧40DIP验收。

## 实际代码变更

- 只修改InkeysHeadlessTests/page_control_tests.cpp中的两个snap测试，其他Up、capture相关纯策略、tracker、geometry及页码门禁测试保持原覆盖。
- 常量改为既有BarBottomDockThresholdDip=20；ResolvePptDragCandidate接口未改变，一个distance参数用于两轴；X仅作为前提，吸附不强制X归零。
- TestMiddlePairCenterSnap的1152个矩阵组合为：2种monitor×3种DPI×4种倍率×2侧×2种Y方向×3种Y边界×4种X距离。Y为±19.9/20/20.1屏幕DIP；X为19.9/20/20.1/120屏幕DIP。预期仅Y、X都<=20时centerSnapped，中心归零，X保持。
- 每个组合调用生产ResolveSurfaceLayout检查两面同高及对称；主体中心允许正常半像素舍入。额外侧距通过logical RECT距屏幕侧边减去PageControlGapDip*实际controlScale舍入后的既有间隙验证，不能把既有gap或阴影算入20DIP。
- 矩阵内20.1和远侧距120确保“中轴范围内、远离边缘”不吸附；倍率0.5/1/2.5/3和DPI1/1.5/2可区分门槛误乘控件倍率、重复DPI或错误扣gap。

## 夹具修订与可达性

- 原中心碰撞夹具的X=30、Y=±39已不满足新条件。现改X=19、Y=±19，DPI=1、middleScale=0.5；bottomPairHeight分别450、577。仍先调用生产PageControlGroupsOverlap断言自由候选可行、中心不可行，再要求centerBlocked且保留自由候选，不以“没有尝试吸附”冒充碰撞覆盖。
- raw候选碰撞而中心可行的夹具，随Y从39改19将bottomPairHeight从612改592。继续验证另一组新位置、session/epoch/pairVersions来自当前发布，而非旧drag-start。
- 先Clamp再Snap的矮屏从240px高改为200px，DPI1、倍率1；Y范围裁剪到12.5px，确实进入新20DIP带。
- 底部pair反例从39px改19px，处于新20DIP带内仍不得套用中轴吸附，防止阈值缩小后弱化原回归。
- 当前倍率裁剪反例维持1080px高、currentScale0.5、旧candidateScale3、rawY10000，预期裁剪496.25px；它位于吸附范围外，验证当前拟合几何而非吸附。

## 验证状态和责任

本角色仅完成静态核对、UTF-8无BOM/CRLF检查及git diff --check，未构建、执行测试二进制、启动GUI/HWND/Office或commit/push。主会话统一执行最新solution、--no-window及--bar-eraser-offscreen-test；旧一轮exit0不能视为本轮已通过。

生产角色负责cpp私有140ms吸附Y过渡及真实离屏测试：中间Y、两面同步、重复Move/页码/Up不重启、最终收敛、脱离/Cancel/新手势和过期候选。headless仅编cppm，不把上述动画行为算进1152个策略组合。真实USER32输入、故障设备和Office/WPP仍NOT VERIFIED。
