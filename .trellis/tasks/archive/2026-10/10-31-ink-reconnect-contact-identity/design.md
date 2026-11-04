# 技术设计

## 原因与所有权
绘制 owner 线程独占 RuntimeStroke 和 CanvasTouchGestureState。CanvasTouchKey 使用 tablet context/contact ID；RTS 可循环复用 ID。initializeStroke 会先为新 Down 在 FSM 登记 key，再尝试重连接管。旧 runtime 的原 contact 已在 Up 时退出 FSM。
成功重连当前仅替换 handle；touchGestureKey 留在旧 key。新 contact Up 清错 key，FSM 残留新 key；ID复用被 OnTouchDown 的重复 key 防护拒绝。

## 最小修复
在唯一成功重连接管点同步新输入 handle 与 touchGestureKey，保持新建笔画相同的 Touch/非Touch规则。不要在全局 Reset 手势、引入 generation 改造或放宽重复 key 防护。若为可测试性抽取接管接缝，仅集中现有身份赋值和新增 key 同步；原有模型、Recycle、样本及诊断顺序不变。

## 测试边界
复用现有 --draw3-control-fence-test 或同等无窗口生产 probe，不启动 Host HWND。测试必须调用真实生产接管接缝而非复制修复赋值。先保留旧行为运行回归得到红灯，再应用修复得到绿灯；使用真实 coordinator 的 contact slot/generation复用及真实FSM以避免纸面测试。

## 风险与回滚
其他接触可与重连并存；不得清空整个 FSM。非Touch维持key0。只回退本次少量接管赋值/接缝即可回滚；不修改构建/依赖/性能开关。
