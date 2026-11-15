# 第七轮独立审查

审查范围：本轮六项、prd/design/implement/check.jsonl；native-desktop shapes、ui-pointer-session、rendering-and-ui、cpp-conventions、build-and-compatibility 合同。

## 已确认
- 首次圆点准入由已呈现 editOpacity 判断，浮条动画门仅作用于浮条按钮；保留成功呈现快照，未用目标布局做输入。可覆盖首个按下穿透及四角被误认为本体的问题。
- 位置拖动恢复同接触相邻 Move 合并。保留 Down/Up/Cancel 边界、捕获票据、生命周期/呈现代次，并补足 source 和 pen 身份；弧端/旋转及触摸本体多指保留轨迹。3D 底面 Z 控件是 Vertex，按绝对位置求角，因此可合并。
- 几何面板 Touch 白名单从退役 GeometryAttributeBar 切换到同帧 frame_ 的 panel，继续成功提交后发布；面板退场可阻挡而不执行动作。新增测试经过真实 Touch Probe，不只是业务 Pointer。
- 圆的两个翻转项由 wanted/hide 同时影响布局和输入，其余预设保留。胶囊镜像时重选最近框边并保持切线方向，普通旋转不会触发变号处理。

## 最终复核
1. 镜像控件初始化缺口已修复：新 scene 接收 det<0 对象时选屏幕最高框边并规范切线方向；新增重新打开镜像对象的停靠/左右顺序断言。
2. 原先提出“两次翻转在一次 UI 观察间都生效”的疑虑，经追踪生产动作身份排除：Flip 属于属性动作，ShapeEditorActionIdentityMatches 严格要求 action.identity==published.identity；每次成功属性操作后 publishShapeUi 推进 generation/object revision。同一已呈现 UI 帧发出的第二次翻转仍携带旧身份，会被拒绝；若第二次操作取得了新已呈现帧，dock 已观察到第一次翻转。因此该疑虑不是当前用户路径的未修复问题，不应增加猜测旋转矩阵的启发式。
   证据：Draw3.ShapeRuntime.inl 的 ShapeEditorActionIdentityMatches；Draw3.ShapeRuntimeActions.inl 属性候选前校验及末尾 publishShapeUi；既有 ShapeRuntimeTests 的 old property action cannot target later object revision。
3. 逐柄测试已删除首柄后 break；每个 Scale、矩形 Vertex 与 3D 底面 id1001 均强制验证独立命中、真实 Contact 事务及 geometry/parts 实际变化，避免仅 revision 改变的假阳性。
4. 第七轮 spec 已记录输入门、同帧 Touch、身份/终态合并、镜像重开和验证边界；本次复核没有遗留源码阻塞项。

## 验证边界
本代理未运行构建、GUI 或测试。根代理统一运行完整 Debug ARM64、编辑器离屏、几何面板离屏、native input、input worker。队列合并数量证明消除逐样本积压路径，不等于真实硬件触控延迟测量。
