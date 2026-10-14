# 拖动释放第三光源 Trellis 检查

- 基线：feature/mode / 35afa7ee；延续 toolbar-order-expansion，保持任务进行中。本角色不 commit、push、启动 GUI/CU 或单独构建。
- 已读取完整保存的 hook、check 上下文、任务 PRD/design/implement、第三光源研究与实施记录，并补读原 spec 中偶数 tuple、第三光休眠和旧新 damage 合同。

## 生产链检查

- PrepareFrameLighting 声明、定义、生产唯一调用和三个旧离屏调用一致；新增 fixture 也调用真实 Prepare，未增加公开测试注入接口。
- RenderLoop 在 release 交接未完成时 Retry；吸收直移后重新捕获同一稳定偶数 tuple，AdvanceLayoutAndInherit 锁存 frameTranslation。发生自动写回资格失效时，在光照准备前 RequireVisualRetry 并早退。新增 origin 使用既有同帧 monitorOrigin 与锁存 translation，与属性几何入口相同，不重读 live/presented 位移。
- 输入线程原有屏幕点/序号/available/ready 发布锁保留；Prepare 在同一锁内取值并以物理屏幕点减布局原点，未除 zoom，未逐帧查询系统光标，未竞争写回输入缓存。
- 原 FinishLightingFrame 比较前后局部点，因此 serial 不变的坐标重基准仍标记 CursorLight；RenderLoop 沿 MarkChanged、当前光圈与控件扩展边界相交、Observe、旧新联合/失败保留路径生成脏区。主体/抓手/覆盖层光源逆映射保持原入口。
- PageControl 发布链继续直接读取 screenPoint、半径、强度、可见性；测试侧同一 Bar module 内声明的 Surface 内部函数与定义/生产调用签名一致。没有新增 Surface 生命周期或输入源。
- 普通过期帧、首次形态屏障及最终 presentedTranslation 选择逻辑未改；本次不把并发直移后的整帧呈现光照一致性宣称为已动态验证或已修复。

## 回归检查

- 六组原点 × 四种 zoom，共 24 组真实 Prepare：负显示器、两轴、跨原点、零吸收、实际残差和不变原点；screenPoint/serial=41 不再更新，释放后持续 120 帧。live/presented 干扰值不会成为隐含位置来源。
- 四组真实 D2D Shape 像素回归以固定屏幕几何独立定位形状，调用真实 Prepare 后绘制；关闭第一光、填充和普通边框，仅留第三光，验证实际非零贡献与吸收前后 maxDelta≤2，并以第三光休眠全透明图作负对照。没有用 SetFrameLightingSnapshot 替代新增回归的生产光照入口。
- Inside/Grace、Dormant、未 ready、动态关、动画关、总关测试只验证已发布输入对应的生产帧门禁/淡出，不声称执行 Raw Input 注册、真实定时器、消息或 HWND。
- 红测证据已由主会话执行：旧产品完整编译成功，真实离屏回归 exit 1，恰好 48 条新增预期失败；最终产品/新增像素断言仍待主会话统一构建与执行。

## Findings

没有发现本轮需要机械自修的源码问题；本角色未修改产品源码或现有测试。主会话已同步第三光屏幕点/同帧原点与真实 Prepare 回归规范。

## Verification

- 静态检查：git diff --check 通过；四个变更源码可按严格 UTF-8 解码，BOM 与原格式检查一致，均为 CRLF（bare LF=0）；检索全部 Prepare 调用点通过。
- 编译/类型检查：由主会话统一动态定位 ARM64 原生 MSBuild，按完整 InkeysRepo.sln Debug|ARM64 执行；此记录写入时 pending，不重复并行构建共享产物。
- 动态测试：最终 Bar 离屏与 Headless --no-window 由主会话统一执行，结果写入 validation-drag-light.md。
- 人工待验：实际鼠标/触摸拖动抬手、展开/折叠、浮动/停靠/换边、DPI/多显示器和并发拖动视觉。本回归不执行整个私有 RenderLoop、真实输入消息或 ULW。

## 主会话最终动态验证

统一完整solution Debug ARM64构建退出0、0 errors；--bar-eraser-offscreen-test退出0，24组静止坐标域测试stable=1，4组隔离第三光像素maxDelta=0/offLit=0，EraserVisual/PageControlScene failures=0。Headless --no-window退出0，全套通过。此前pending描述为检查当时状态，最终证据见../validation-drag-light.md；真实输入/ULW与普通stale-frame边界保持人工待验。
