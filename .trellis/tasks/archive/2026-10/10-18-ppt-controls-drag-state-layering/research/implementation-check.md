# 独立实施审查记录

任务：10-18-ppt-controls-drag-state-layering；日期：2026-10-18。

## 阶段 A：生产冻结前只读审查

- 已读取完整 hook 保存文件、check.jsonl 所列上下文、PRD/design/implement 和主会话更新的三份规范 diff。
- 本阶段未修改生产、测试或规范，未构建/执行二进制、启动 HWND/GUI/Office、提交或推送；本记录为唯一写入。
- 已审核 PageControl.cppm 新增的 Up 判定、pair ownership、像素几何 guard 和真实生产候选策略，以及 page_control_tests.cpp 的相应用例。
- 暂未发现阻断实施的纯策略缺陷。吸附复用 BarBottomDockCenterThresholdDip，按当前布局屏幕归一化 DPI 只换算一次；先用当前拟合倍率 Clamp，再检查中心碰撞，中心不可行时仍保留可行自由候选。测试覆盖阈值内/等于/外、正负偏移、96/144/192 DPI、独立倍率、左右对称、奇偶尺寸、负屏幕原点和中心/自由候选不同碰撞结果。
- 旧 const bool presentationLockAvailable 伪竞争断言已移除。headless 中的新 ownership/geometry 断言只证明生产纯策略，不能证明私有 AnimatedBounds、真正呈现锁竞争、捕获转移或实际 HWND 已通过。
- 三份规范更新维持唯一根 TOPMOST、COM/session/epoch 与拖动不写盘边界，未把未知页码伪造为 ready。最终仍需按完整生产 CPP 和实际验证结果校准措辞。

## 尚待完成

- 生产冻结后审查完整 PageControl.cpp 的 Snapshot、Configure/Retarget、双面提交/续帧、Down 锁外 capture 小事务、Up 终点与取消/触摸路径。
- 核对内部无 HWND 离屏用例是否真实执行生产 bounds/仲裁，包含 released pending、第二面失败与回滚、旧帧、新发布、同步旧 Cancel 和新 tracking 顺序。
- 构建/测试由主会话统一运行；本记录尚无 lint/type-check/运行通过结论。
- 实际 USER32 鼠标/触摸 capture、故障设备局部停住、Office/WPP 页码及 Z 序均 NOT VERIFIED；不得从纯策略或注入测试外推实机通过。

## 阶段 B：完整 diff 审查

六个 tracked diff 均已审核：两份生产 PageControl 文件、一份 headless 测试和三份规范。另读 implementation.md、test-implementation.md 与 verification-setup.md，核对真实入口。

### Finding R1：连续手势下 raw / runtime-fit 身份混用（已修正并复核）

- 文件：PageControl.cpp 的 BeginDragTracking / BeginSurfacePointer，以及 cppm 的 BeginPptDragTracking / IsPptDragFrameOwned。
- 反例：第一次候选的 tracker 与 publishedPpt 都保留 raw 用户倍率，已 released 但因 P 锁忙尚 pending。立即再次 Down 时，BeginSurfacePointer 用 runtime-fit 布局建立新手势，而 BeginPptDragTracking 在保留旧 pending/revision 的同时覆盖 tracker.layout。若用户倍率与实际拟合倍率不同，SnapshotForRender 比较 raw 与 fit scale，ownership 变 false。第二次零位移 Up 或受限且 unchanged 的 Up 不会发布新 candidate，因而不会修正这份身份；同 pair 的旧目标仍可进入 0.4 秒位置动画。
- 影响：几何 guard 最终仍可到位，并非必然永久 pending；明确违反 released pending 和连续手势的立即落定合同。
- 已向主会话提交源码条件链，主会话认可并派原生产责任代理最小修正：D→S 内用 publishedPpt.layout 作为 tracker canonical 身份，手势起点与 feasible 保持 runtime fit，并加真实生产离屏回归。本检查代理按并行边界不覆盖该生产修正。

### 已核对的生产与验证边界

- SnapshotForRender 保持 D→S；未发现新增 S→D 或 D→T 反向边。owner 直移仍仅 P.try_lock，renderer 在 P/Window Service 同步提交前释放 T。Flush 与方向/预览 callback 位于事务锁外。
- render 的实际 logical RECT、scale、presentation RECT 对本帧 runtime-fit 目标证明到位，并在呈现及 Window Service 成功、第二次版本门通过后才提交 surface mask。正常显隐/倍率/工作区动画仍保留。
- Down 的真实 SetCapture 在 T 外，旧 Cancel 完成后才重新映射同一屏幕点、复核和建立 tracker；Up 先采终点并在 T 内结束手势/tracker，再锁外 ReleaseCapture。触摸仍按锁定 id/primary 过滤和取消，不把 Cancel 合成合法 Up。
- 新离屏测试调用真实生产 UpdateDragLocked、MovePairWindowsDirect、ConfigureSurface、AnimatedBounds 与 capture 小事务，窗口操作只在明确模板接缝注入；真实另一线程持 P，不再用布尔常量冒充竞争。窗口失败注入验证 0→1→0 回滚与单面 mask。
- 已独立核对 IdtMain.cpp:1307 的精确 offscreen flag 早退；既有 Bar.EraserAttribute.Test.cpp 只启动共享 WARP/D2D/字体资源并调用 RunOffscreenTests，新用例未创建 HWND、调用真实 capture 或 Office。
- 第 4 项无 Window Service 改动，COM/ABI/DLL/TLB 及页码门禁无变更；`-/-` 的主要回归仍归入既有测试，不把合法未知填成旧页 ready。

### 静态检查

- git diff --check：exit 0。
- 六个 tracked 文本文件 UTF-8 解码、BOM 与基线一致、CRLF：6 files，errors=[]。
- 构建、type-check 和两种无窗口测试尚未执行，由主会话等最终生产冻结后统一运行；不能提前记为 pass。

## R1 修正后复核与最终源码冻结

- 已修：PageControl.cpp:353–362 的生产 wrapper 在同一 D→S 临界区核对 session/epoch 后，使用 publishedPpt.layout 作为 canonical tracker 身份；runtime fit 只用于 state.dragStartLayout/feasibleLayout 与实际目标，不将其倍率写回同 revision 的 raw 候选。
- 已核新增 PageControl.cpp:2906–2967 的真实内部离屏用例：raw middle scale=3，共享预算 35 DIP/512 使实际 fit 明确不同；两面的实际旧几何 B 与第一段 pending 目标 C 非零平移；第一次窗口提交失败并 Up，第二次 Down/零 Move Up 不发布新 revision、没有额外 move、保留 committedLayout 与 raw ownership；首个 Configure 即实际 fit C，两面提交并在后续 16/80/600ms 稳定。该用例若恢复原 fit 身份写法，会失去 ownership 并使真实 bounds 动画断言失败，能覆盖本次缺口。
- 该语义修正经主会话批准，由原生产责任代理完成；审查代理没有并行覆盖其文件。
- 修正后没有发现其他阻断实施的源码/测试/规范问题，已通知主会话最终源码冻结并可开始统一构建/验证。
- 最终验证状态仍待主会话真实执行结果，不能将静态审查通过写成编译/运行通过。

## 统一验证已取得的证据

- Type-check / compile：已直接读取 Build/Validation/ppt-controls-20261018/solution-debug-arm64.result.json 与 stdout。完整 InkeysRepo.sln Debug|ARM64、ARM64 原生 MSBuild，监督超时 900 秒，exit 0、29.02 秒；PageControl.cpp/.cppm 与 page_control_tests.cpp 确实编译，本次文件无相关 warning/error。既有 hashlib++ C4267、IdtMain C4244 和非致命 pwsh PATH 提示不属本次源码问题，未为其修改代码或工具链。
- Headless：已直接读取 headless-no-window.result.json 与 stdout，精确参数 --no-window，exit 0、99.48 秒，PASS animation correctness。该结果包含生产 cppm 策略和既有页状态回归，不代表 cpp 私有 bounds/capture 离屏用例已运行。
- Spec：native-session-ui3.md 的 canonical raw tracker / runtime-fit 说明已由主会话补入原位置合同，本检查者复核与最终 wrapper 一致。
- 生产 offscreen 已取得并独立读取真实结果，详见下方最终 Verification。

## Findings (fixed)

- File: Inkeys/Inkeys/UI/PageControl/PageControl.cpp:353（新增回归约在2906）。
- Issue: R1 连续手势把 runtime-fit 倍率写进同 revision 的 canonical raw tracker，released pending 的新零位移手势可失去 pair ownership 并恢复位置缓动。
- Fix: 原生产责任代理按主会话认可的最小方案，在 D→S 内验证 session/epoch 后以 publishedPpt.layout 建立 tracker，保留 runtime-fit 起点/可行几何和旧 pending/revision/committedLayout；真实内部离屏回归覆盖 B→C 非零平移、raw≠fit、再次 Down/零 Move Up、首帧双面到位及多帧稳定。独立审查复核后统一构建与两种测试全部通过。
- Spec: native-session-ui3.md 原位置合同已同步 canonical raw / runtime-fit 边界，三份相关规范与最终实现一致。

## Findings (not fixed)

- 没有遗留阻断项或本次范围内应修而未修的源码缺陷。
- 用户所报“仅拖动控件停住、主栏仍正常”的现场唯一根因仍未实机复现；本轮修复明确的状态/条件性 capture 风险，保留碰撞/边界，并补限频数值诊断。不得把自动化通过表述成原设备卡住已复现并全覆盖。
- 真实鼠标/触摸、USER32 SetCapture/ReleaseCapture 重入、四 HWND 实际层级/命中及 Office/WPP/损坏 COM 设备均 NOT VERIFIED，因本轮无 GUI/Office 授权。第4项窗口层级不新增修改；页码 -/- 回归既有控制/未知/epoch 合同，合法未知仍保留，未重复更改 COM 或开放墨迹输入门禁。

## Verification

- Lint: pass（git diff --check，最终复查 exit 0；此 C++ solution 无本轮单独 lint target）。
- TypeCheck: pass（主会话统一完整 InkeysRepo.sln / Debug|ARM64；原生 ARM64 MSBuild；solution-debug-arm64.result.json exit 0，29.02秒；本次 PageControl 与测试文件无相关诊断）。
- Tests: pass（InkeysHeadlessTests.exe --no-window，headless-no-window.result.json exit 0，99.48秒，PASS animation correctness，stderr 0字节）。
- Tests: pass（Inkeys.exe --bar-eraser-offscreen-test，product-offscreen.result.json exit 0，24.11秒；stderr 明确 [PageControlScene] failures=0；Build/eraser-b/offscreen-results.log 末 [EraserVisual] failures=0）。
- 两项运行由主会话执行，检查者直接读取真实结果 JSON/stdout/stderr；没有另起重复构建/运行或使用 GUI。前者执行 cppm 纯策略及既有页码回归，后者确实执行 cpp 私有生产 Scene/bounds/锁竞争/capture注入，二者不混淆。
- 文本格式：六个 tracked 文件 UTF-8、BOM 与基线一致、CRLF，检查 errors=[]；代码与测试冻结后无额外修改。规范后补 canonical 身份句只影响文档。
- 范围：仅 PageControl.cpp/.cppm、page_control_tests.cpp、三份必要规范及本任务文档；无 COM/DLL/TLB、Window、项目/SDK/依赖或配置改动，无 commit/push。
- 既有非任务 warning 与非致命 pwsh PATH 提示留在构建日志，未通过修改源码/工具链绕过。真实设备验证限制保持上列 NOT VERIFIED。

最终结论：R1 已修并验证，完整源码/测试/spec审查完成，当前没有遗留阻断项。允许主会话提交本轮实现结果供用户评审；本任务未获得新的 commit/push 或 GUI 授权。
