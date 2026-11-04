# Touch 修复实现记录

- 日期：2026-11-03
- 状态：产品接线已实现；实现子代理未执行构建或 GUI，统一验证由主会话进行。
- 范围：主栏与四个分页 HWND 的独立 Touch Context、薄跨窗交接、Pointer 兼容副本过滤、取消身份与诊断。保留原有共享样式改动和已交付诊断；未修改 Hook/Draw3/布局/资源 lease 规则。

## 实现边界

1. `Input/UiTouchRouting.h` 与 `UiPointerInput.cpp`：
   - 每个 adapter 持有自己的 Context；五槽 native 登记表只存窗口、回调和注册身份，不存第二份全局活动手势。
   - `HandleMessage` 一次读取并 RAII 关闭原始 HTOUCHINPUT；`DispatchDecoded` 按值交目标 adapter，未转发原句柄、未递归调用目标 WndProc。
   - Down 无 owner 时使用真实 TOUCHINPUT 屏幕点做 WindowFromPoint，只接纳查询前登记、查询后仍为同注册的恰好五个目标之一。只查询该候选的 Probe，不找其后层。
   - QueryScope 仅使 Page 的可见锁定背景在这次 Touch 查询中阻挡；物理 Mouse NCHITTEST 原策略保留。
   - Move/Up 按 hSource/contact 查本地 Down owner，不能因 Hover 或原接收 HWND 改变而迁移。
   - 原生 MSG time 用于时序门与业务时间；提供方 dwTime 保留诊断和可选 haptic 关联，不要求其严格递增。略早于最近 Move、但不早于 Down 的 Up 仍结束，位置不倒退；允许同毫秒新接触和驱动 dwTime=0。
   - primary 优先同批选择；只有候选 Probe Accept 后才替换 fallback，Miss/Blocked 不抢 lease。
   - query/probe/capture/cancel 回调均无 provider/registry/Scene 锁；注册、local.mutation、route 身份在可重入调用后复核。退休先摘旧登记，回调重建的同 HWND 新登记保留。

2. `Bar.Interaction.cpp`：
   - 本地 barTouchContext 与已有 barTouchProducer、MouseChannel、worker Session 对接。
   - 原 WM_TOUCH 大块解码改为共用入口；真实 Mouse/Raw 分支保留。
   - Probe 接点：`ProbeBarTouch -> barUISet.ProbeTouchScreen`，并复核 HWND 可见/可用、成功呈现 alpha、退出中/MessageBox 阻挡。Down 在 SetCapture 后再次 Probe。
   - AcquireBarNativeCapture 在 SetCapture 重入返回后先确认 Touch route，再发 capture ticket，旧 Down 不覆盖或释放重入新 Down 的捕获。
   - Bar 触觉仍为可选元数据：Down 匹配原因/候选数量、Move/Up token 诊断保留。触觉继续原先取整坐标和 provider time 关联；业务样本采用共用屏幕点和原生消息时间。
   - worker 冻结 current/gesture Touch identity；语义取消发对应旧手势的 native cancel，不使用 acceptedNewest 误伤已排队新手势。

3. `PageControl.cpp/.cppm`：
   - 四个 PointerClient 各有 Touch Context，原 nativeTouches 状态删除。
   - 删除 Pen 兼容鼠标业务入口；统一 IsPointerGeneratedMouseMessage，72d/7ad 低位变化只用于诊断。
   - 抽出原有 `ProcessNativePointer` 准入与入队段，供归一 Mouse 和按值 Touch adapter 复用，未为 Touch 递归 WndProc。
   - Probe 接点：`ProbeTouch -> CaptureInputSnapshot`，保持 scene 的已呈现圆角、popup/菜单背景资格；可见 locked 背景返回 Blocked，隐藏/透明区域 Miss。
   - Down SetCapture 后复核 route、语义、region、可见/可用；捕获失败 ticket=0 仍可走 Touch。Touch 活动有效性 timer 建立失败则完整取消。
   - PPT Down 立即翻页、无 Move 也继续的开关长按、移出/Up 停止，以及白板有效 Up 单次翻页继续使用原 worker handler/tick。未添加 Page 震动。

4. `IdtMain.cpp`：
   - 原 WindowSpec.created 回调完成 RegisterTouchWindow 后，调用 Bar/Page RegisterNativeTouchInput。此时 Window Service 已登记 HWND，不在 Page WM_CREATE 时依赖尚未可用的 Index。

## 取消与旧代隔离

- 完整身份为 HWND + uint64 registration + uint64 local route generation。
- worker accepted 捕获和 route 在同一短锁下整体保存/复制，不用多项无序原子拼身份。
- 五槽取消邮箱按登记槽固定映射；额外镜像仅存 HWND/registration。worker 只写完整值意图并 Post 私有 wake，不读写 native Context，不跨线程释放捕获。
- 零 capture ticket 也发 route cancel；迟到旧 registration/generation 不覆盖较新取消或清新 Down。
- `CancelTargetWindow` 专用于 WM_CAPTURECHANGED，避免 B 从 A 接管捕获时因 origin=A 误取消 Preparing B。
- `CancelWindow` 用于隐藏、销毁、语义/显式取消，既取消该目标，也取消原始接收窗绑定的在途交接。Destroy 退休注册。
- Up/Cancel 释放原 lease；Voluntary ReleaseCapture 不生成第二次取消；旧 release ticket 保持原票据校验。

## 诊断接点

共用 `touch_read` 记录 decode 成功/失败和 Win32 error；逐点 `touch_select/touch_probe/touch_deliver/route_down/route_cancel` 记录 origin、目标、flags/mask/extra、原始与消息时间、语义、64 位身份、Preparing、接纳/拒绝原因。旧包、重复、无 owner、未登记候选、注册/本地代次重入变化、Miss/Blocked 均可区分，Move 复用已有 250ms limiter。Bar 业务接受与 haptic 接受继续分开输出。

## 回归与静态检查

- 实现者静态执行 `git diff --ignore-submodules --check`：通过。
- 对本轮所改源/header 检查 BOM 与 CRLF：原 IdtMain UTF-8 BOM 保留，其余原无 BOM 文件保持无 BOM；无 bare LF。
- 真实共享 worker overflow 测试补齐零票据和大于 32 位 registration/route 保真。
- Page RunOffscreenTests 增加真实 native adapter 拒绝 Down、完整 Cancel、后续 channel 可重取 lease 的无 HWND 回归。
- 独立 worker 在 ui_pointer_tests.cpp 覆盖真正 DispatchDecoded/登记/取消及查询/Probe/Preparing/Up 回调重入、来源锁定、fallback/primary、时序边界和取消邮箱；其记录见独立测试报告。
- Bar 已呈现 Probe 由独立 worker 在 Button/Main/RenderLoop 发布边界实现并另行记录。
- 完整 Debug ARM64、Release x64、headless/offscreen 验证未由本实现子代理执行，待主会话统一构建和独立 check。

## 实机待确认

数位板在同一 in-range 内跨五窗并点击；按住跨窗后 Up；PPT 触摸/笔静止长按及移出停止；白板仅 Up 单次；主栏属性连续操作和可选触觉。静态与自动回归不能代替 M6/另一个数位板的最终驱动行为复核。
