# 拖动释放第三光源实施记录

## 产品修复
- 基线 feature/mode / 35afa7ee；前一轮已验证功能由主会话提交，本延续修复不 commit/push/结束任务。
- 真实 PrepareFrameLighting 新增显式 POINT layoutScreenOrigin；生产 PrepareLightingAndDemand 传入 state.monitorOrigin + state.bottomDockFrameTransitionTranslation，来自与布局相同的稳定帧 tuple。
- 仍在 borderCursorLightMutex 内取得 screenPoint、serial 与 available/ready，用 screenPoint 减显式同帧原点重算物理像素局部点。无需新鼠标消息，不除 zoom，不读取 live 或旧成功呈现 translation，也不改输入缓存/Raw Input/timer 生命周期。
- 沿用 FinishLightingFrame 局部位置比较和旧新 dirty 光圈、主体/刚性逆映射。分页共享 screenPoint/radius/intensity 发布链未改；普通 stale-frame 呈现屏障策略未改。
- 同步全部现有 Prepare 调用点，旧离屏原点场景显式传 {}。

## 确定性修复前证据（由主会话执行）
- 先只增加 fixture/旧真实 Prepare 调用而不改产品；完整 solution red build exit 0。
- --bar-eraser-offscreen-test exit 1，恰好 48 条新增预期 FAIL，已有回归没有新增失败；PageControlScene failures=0。
- 例如固定 screenX=900/serial=41，原点300→0，但旧 localX 600→600，120帧 stable=0。
- 日志：Build/ToolbarOrderExpansionValidation/drag-light/red-solution.log、red-offscreen-results.log、red-bar.stderr.log（生成日志不纳入 Git）。

## 最终回归范围
- 测试侧派生 BarUISetClass 只种入受保护输入，不增加产品公开注入接口；调用真实 PrepareFrameLighting。
- 六组原点 × zoom0.6/1/1.5/2，包含正负双轴/负显示器/显示器原点改变/吸收基准/非零实际残差/原点不变；screenPoint与serial始终静止，先淡入90帧，释放后再连续120帧。
- 刻意设置与本帧 origin 不同的 live translation 和成功呈现 residual，验证 Prepare 不从 owner 重读拼帧；断言位置、强度/半径/可见性和 lightChanged/damage 连续。
- 由真实 Prepare 屏幕快照调用生产 ResolveBarSurfaceFrameLightingSnapshot，比较分页本地映射、强度/半径/可见性未变；不安装 HWND 或产品输入线程。
- D2D Shape 使用真实 Prepare 状态，关闭第一光、基础边框与填充；几何从独立固定屏幕点/各帧origin计算，再用D2D变换对齐同一512×512目标。对比吸收前后BGRA maxDelta≤2并要求实际非零贡献；第三光休眠后的对照图必须全透明，排除底色或普通边框空通过。
- 生产 Prepare 门禁回归涵盖 Inside/Grace已发布输入、Dormant/未ready、动态关/动画关/总关；只验证帧门禁和淡出，不声称执行真实 Raw Input 注册/注销或5秒计时器。
- 未调用 SetFrameLightingSnapshot 构造新增回归状态，未改变 input/presentation/window owner，不GUI/CU。

## 验证状态和边界
- 实施代理完成调用点检索、diff --check、原BOM/CRLF静态核对；不执行并行完整构建，以免PDB/共享产物冲突。
- Source ready 后由主会话安排 Trellis check、完整ARM64 solution和Headless/Bar真实离屏复验，最终通过情况见 task validation-drag-light.md。
- 真实鼠标/触摸拖动抬手与PPT/停靠/多显示器视觉仍人工待验；回归不执行整个私有RenderLoop，只验证真实Prepare、共享映射与真实D2D Shape，加上生产同帧接线静态审查/完整编译。

最终统一验证：完整solution退出0，真实Bar离屏退出0、24组stable=1、4组像素maxDelta=0且关闭对照透明；Headless --no-window退出0。具体日志与人工边界见../validation-drag-light.md。
