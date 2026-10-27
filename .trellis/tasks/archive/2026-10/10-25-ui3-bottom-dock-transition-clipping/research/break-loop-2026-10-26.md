# Bug Analysis: 吸附修复反复失效与测试自证

## 1. Root Cause Category

- **B / 跨层合同**：输入代理抓手、渲染仿射、成功快照和窗口位移对“当前有效抓点”没有统一含义。手动捕获只保留旧绝对底边，MainButton 实际 Y 与输入代理又采用不同映射。
- **C / 修改传播不完整**：接管提交修了脱离、吸收和容量，却未修改手动捕获与真实抓点；直移本地 awaited 与全局 deferred 也没有统一门禁。
- **D / 测试覆盖缺口**：离屏输入前清 `committedWindowScreenBoundsReady` 绕过 SetWindowPos；脱离只查高度，没有位置/真实抓点断言；底边退化仿射也能自证通过。
- **E / 隐含假设**：把“旧底边不动就是连续”“c=e 可泛化”“没有提前直移就是修复”误当不变量。

## 2. Why Fixes Failed

1. **早期 serial/barrier 修复**：能阻挡旧帧误确认，但没有解决当前有效指针与真实局部抓点，不能替代形状交接。
2. **接管提交 `1ba08698`**：脱离、吸收和容量修复有效，但目标偏向相邻缺陷；手动捕获和真实抓点未改，测试又绕开关键路径，导致软件 PASS 被误当作用户症状消失。
3. **本轮初版真实抓点修复**：质量审查继续发现 Down 后高度变化重算 q0，以及底边抓点长期停留在最小高度。说明只引入归一化抓点仍不够，冗余约束必须使用可积分的表示。
4. **测试自身修正**：历史 base 中心和旧红探针位置曾错误，均保留失败记录并修正夹具，没有改生产公式迁就测试或把编译/夹具失败当产品通过。

## 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | 可执行几何合同 | Down 从同一成功 mapping/实际高度锁存 q0/base 比例；有效指针、session、两轴与 translation 同 seqlock | DONE |
| P0 | 一般形状交接 | `ResolveBarBottomDockShiftedShape` 先随有效抓点平移，再由新 base/仿射播种；禁止把 c=e 当一般解 | DONE |
| P0 | 可积分底边表示 | 底边及取整邻域使用高度偏移弹簧，负高度 constrained 显式重基并清速度 | DONE |
| P0 | 直移双屏障与顺序证据 | 本地 awaited + 全局 deferred + 稳定 serial；同时断言无提前直移且屏障后仍有合法直移 | DONE |
| P0 | 位置/真实抓点回归 | 历史 1773/1824/+47/r=38/81 先红后绿；覆盖 r=33/81、38/81、中心、边界和至少 90 帧恢复 | DONE |
| P1 | 完整质量门 | 两个离屏入口、相关选择集、完整 ARM64 Solution、完整 no-window、full-scope trellis-check | DONE |
| P1 | 规范与交接 | 更新 `rendering-and-ui.md` 的 signatures/contracts/matrix/tests/wrong-correct，保留本复盘和验证记录 | DONE |
| P1 | 真实视觉验收 | GUI/Office/WPS/真实 ULW/DWM、多屏和主观手感 | TODO：未授权、未执行 |

## 4. Systematic Expansion

- **Similar Issues**：其他“输入目标—动画—成功快照—窗口提交”链也可能把代理点当真实点，或只检查终态/尺寸而不检查实际轨迹。显示环境变化、快速反向和失败候选都应使用同一成功基线。
- **Design Improvement**：对冗余几何约束优先选择可积分表示，不用硬钳制制造貌似稳定但不可恢复的退化状态。写者/读者/执行动作都必须核对同一稳定 serial，本地手势状态不能代替全局屏障。
- **Process Improvement**：每个“修复完成”声明必须列出用户症状的独立可观察断言、旧红证据、未覆盖路径和真实视觉边界。测试 harness 的每个跳过/替换都要说明它仍证明了什么、没有证明什么。
- **Knowledge Gap**：height-only、source 合法和最终收敛都不是位置连续性；合法直移被全部禁用也不是通过，必须同时有正向放行证据。

## 5. Knowledge Capture

- [x] `.trellis/spec/native-desktop/rendering-and-ui.md` 已加入真实抓点、一般形状交接、底边恢复、直移双屏障及相应测试合同。
- [x] `research/review-2026-10-26.md` 记录接管修复缺口。
- [x] `research/review-fix-validation-2026-10-26.md` 记录旧红、实现、质量审查和最终自动化结果。
- [x] 本复盘记录，防止再次修错目标和测试自证。
- [ ] 真实 GUI/Office/WPS/ULW/DWM 验收另行授权后执行，当前不宣称视觉验收完成。
- [ ] 未执行 git commit；用户未要求提交。
