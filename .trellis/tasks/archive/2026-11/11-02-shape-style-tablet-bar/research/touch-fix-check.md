# R2 five-context Touch independent check

## Findings (fixed)

1. Bar Probe先接受strict原生候选与已呈现区域，却又因Setting::IsVisible拒绝全部Touch；设置窗在Bar后方也会阻断。移除此额外可见性闸门，保留WindowFromPoint严格五窗候选、真实probe、IsWindowEnabled和既有模态条件，不加入canvas fallback或视觉变化。
2. WindowFromPoint/probe会同步重入，旧实现只复核目标；源窗idle期间隐藏/取消/注销不会让尚未建route的Down失效。Context增加本地sourceEpoch，CancelWindow即使源idle也推进，查询与probe后复核源registration+epoch。仅拒这一次在途变更，不按源当前可见性永久拒绝随后稳定的新Down。capture-only CancelTargetWindow不推进源epoch。
3. 原本fallback可在后续包观察到primary后升级；新router只在Down设置primary会让已经升级的接触被另一primary抢占。owned同contact观察primary时只升级，不降级。
4. Bar/Page在SetCapture同步重入取消Preparing route后，新的native票据尚未产生；原early-return可能留下无route/票据的实际捕获。共享小判据ShouldRollbackPreparingCapture仅允许同一last identity/registration、没有新native票据、此前非本窗而当前仍为本窗的已取消准备回收；新Touch identity、Mouse票据、其他窗捕获和预存本窗捕获均不碰。两端均按原voluntary release协议释放，不生成成功Up。

5. primary 接替旧 fallback 时，CancelTarget 回调可再次取消或注销接收源，原实现返回后只检查目标。现回调后立即复核 sourceCurrent，失效时记录 origin_changed_during_fallback_cancel 并拒绝旧 Down；原目标 mutation/Active/注册校验保留。

## Test changes

直接使用生产router/helper的无窗口回归：

- query期间source CancelWindow或Retire/re-register，目标为另一有效窗；旧Down不投递，下一次稳定Down正常。
- target probe期间source变更同样拒绝；随后稳定转交恢复。
- fallback Down后同contact Move升级primary，另一primary不能抢占，原Up仍完成一次。
- 旧fallback取消回调中取消来源窗、来源窗注销重登记两变体：旧primary不投递，下一次稳定Down正常转交。
- 实际route delivery内同步cancel，zero-ticket捕获回滚判据允许回收；reentrant新Touch、更新Mouse票据、其他窗或预存捕获均受保护。

其他既有测试仍覆盖多点primary/fallback、五套独立Context、来源与注册代次、native MSG时间/driver dwTime仅诊断、Up略早Move、cancel snapshot不清新代、请求取消槽有界、接收窗与锁定目标分离。worker metadata对零票据overflow整体保存route身份。

## Findings (not fixed)

未发现另一个确定的业务正确性问题。保留严格native alpha边界：窗口原生alpha0且非五窗候选的触点不回退到canvas或背后窗。FineDial新增Down走廊主要由实际Draw背景覆盖，最外圆角alpha0不接纳符合批准边界。Bar probe owned文件由原实现代理独立自检，未在本轮重写。

不对驱动消息目标根因/真实数位板行为作静态通过声明；五窗实现仍需M6复测。旧CSO/其他任务内容未覆盖或清理。

## Verification

- git diff --check --ignore-submodules=all PASS。
- 所有修改使用原UTF-8 BOM身份与CRLF；新UiTouchRouting.h继续UTF-8无BOM/CRLF。
- 本代理不build、不GUI、不commit；全部源已冻结，完整ARM64编译、生产探针、headless与Release x64由主会话统一执行，最终结果见其validation记录。

## 最终增量核对与验证（2026-11-03）

- 只读复核fallback取消后的sourceCurrent guard及两变体：原Active/目标登记/mutation规则保留，复核位于同步CancelTarget回调之后、建立新route之前。
- 核对新增主栏真实PreparePresentedButtonHits/PublishPresentedButtonHits/ProbeTouchScreen生产自测：由RunSharedInteractionWorkerTests调用，未创建窗口/设备或使用全局Bar；覆盖首次未呈现、只准备未发布、Rigid/Body/Grip三映射、圆角/透明区域、disabled遮挡、退场阻挡、候选失败保持旧快照等；产品RenderLoop仅在原成功commit边界传入同帧StateMode。入口与当前任务范围一致，未发现确定问题。
- 主会话报告完整Debug ARM64构建exit0（41.04秒），五个生产探针全exit0，新UI3TouchProbe生产自测PASS；完整headless --no-window exit0（306.60秒），包含新增fallback取消来源失效的两个真实router变体。以上实际运行由主会话完成，本代理未重复运行。
- Release x64仍在构建，不提前标记通过；M6真实设备复测仍pending，不把无窗口结果当作现场修复确认。最终构建/测试/交付证据见[touch-fix-validation.md](../touch-fix-validation.md)。
- 最终git diff --check --ignore-submodules=all通过；源码保持冻结，本轮仅更新本审查记录。

主会话最终补记：Release x64 exit0/115.29秒，PE 0x8664及预期诊断标识核验通过；产物hash和设备待验收边界见上列最终验证记录。
