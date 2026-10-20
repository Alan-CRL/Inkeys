# 吸附体验修订实施记录

2026-10-18。用户已追加授权将侧竖中轴吸附改为 20 屏幕 DIP 双门槛，并使用轻量 UI3 动画。本记录覆盖旧实施报告中的初版 40 DIP/吸附直接跳转描述；原 raw/fit canonical、自由拖动直接稳定、COM 和窗口层级边界保持。

## 生产行为

- ResolvePptDragCandidate 沿用一个显式距离参数，生产传现有 BarBottomDockThresholdDip=20。Clamp 后 abs(middlePairHeight) 与 middlePairWidth 都不大于 20×归一化屏幕 DPI 才尝试中心；middlePairWidth 已是扣除现有实际倍率间隙后的额外距离，不再次减间隙、不纳入阴影，不乘控件倍率。仅将 Y 归零，X 保留。
- cpp 只有一个由 T 保护的 MiddleSnapState，记录 session/epoch/pair revision、两面起始与中心 top、实际尺寸/倍率、共享 started 和 latched/active。没有新增线程、timer、配置、模块或公开动画系统。
- 复用 PageControl 现有 UI3 cubic Ease 曲线和 RenderPipeline 呈现/续帧；吸附 Y 约 140ms。两面同一启动时刻、同一进度；X 继续采用最新目标。带内 Move 只关联最新 candidate revision，页发布和 Up 不重启。完成仍保留 latch，带内继续 X 移动不重播。
- 按主会话确认，入口从两面当前实际 bounds 连续过渡到中心，避免先跳到 raw 位置。一次较大输入直接进入捕获带时，允许该次实际 Y 差在唯一的 140ms 内收敛，重复输入不得延长；离带恢复即时自由位置。
- owner 中间直移依然只 try_lock P 并顺序移动两 HWND；成功后更新两个 bounds、光源边界和 directMoveRevision 屏障，不推进 tracker mask，并显式请求 pair。失败/争用仍保留最新 pending。没有用动画恢复自由拖动的 0.4s 余移。
- Configure 在同代 owned、同尺寸/倍率/可见工作区的吸附候选上采用该短 Y 进度；不新增 inputLocked 来源。keepAnimating 包含 snapActive。实际 logical/presentation/scale 尚未到最终拟合目标时不 commit；released pending 持续到两面成功。像素舍入已经到达最终几何可以按既有几何证明完成。
- 离带、活动 Cancel、新 Down 和新 session/layout 使旧状态失效。Up 清 pointer 后 ReleaseCapture 的同步旧 CaptureChanged 不清掉已 released 的 pending snap。raw 用户布局仍是 tracker canonical 身份，runtime fit 仅用于实际几何，旧 R1 二次手势回归保留。

## 已登记的真实内部离屏回归

仍由现有 --bar-eraser-offscreen-test 调用 PageControl::RunOffscreenTests；普通 --no-window 只执行另一代理维护的 cppm 纯策略矩阵，二者分列。

- Up 没有最后 Move 也从实际 Y 启动中间帧，随后短截止点稳定。
- 两个实际 Scene 同进度、中间位置可见、单调到位；动画不会锁输入，中间几何不能推进最终 mask。
- 带内连续 Move 改变 X 立即更新 bounds，candidate revision 可更新但 started 不变；页发布、Up 和同步旧 Cancel 不重启或清掉 released 动画。
- 较旧 candidate 不可提交较新的 X 采样；最终只一面提交仍 pending，另一面成功才完成。
- 完成后继续带内 Move 不重播；离开侧距门槛立即自由跟随；活动 Cancel、新手势、新 session 清退旧动画并保留 canonical 身份。
- 另一线程实际持有 P 时，生产 try_lock fallback 没有窗口调用；第二面移动失败时第一面的非零 X 平移实际回滚。两种情况释放后都依靠 pair 渲染在原时间内收敛，无需新输入。
- 既有四控件自由拖动 Up、raw/fit released pending 二次 Down/零 Move Up、普通倍率/工作区动画等回归继续保留。

窗口和 capture 操作仍在生产接缝处注入；没有真实 HWND、USER32 capture 或 Office/WPP 实测。本角色未构建/执行二进制，所有新增断言由主会话统一构建和运行，不能将登记测试称为通过。

## 本角色静态检查

- git diff --check 通过。
- PageControl.cpp/.cppm 保持 UTF-8 无 BOM、纯 CRLF。
- 唯一生产写入范围仍为两个 PageControl 文件；未修改 Bar 常量、COM/ABI/DLL/TLB、Window Service、项目或配置；未 commit/push。

## 独立审查补正：owner/render 时钟交错

RenderPipeline 的整批 frameTime 在串行 client 回调前固定。PPT callback 即使取得最新 candidate revision，仍可能持有早于 owner 最新直移的时钟，因此 revision 屏障本身不足以阻止吸附 Y 退回。

MiddleSnapState 新增唯一 pair 共享的 sampledAt，由原 T 保护。ResolveMiddleSnapBounds 采用 max(传入时刻, sampledAt) 推进并保存，再相对不变的 started 计算现有 cubic 进度；新 snap 初始化，取消/新代次随原状态一起清退。两面共用同一单调时间，没有额外锁/线程，140ms 截止点不延期。

真实 Scene 回归补充 owner t60 在带内更新 X 和实际 Y，随后用同一最新 revision 的 Configure 执行旧 batch t40，再执行较新 t80。没有手动跳过旧 frame；两面实际 Scene Y 不能低于 owner t60 位置，X 必须保持最新，t80 继续前进且 started 不变，原 140ms 终点和随后稳定断言继续覆盖。该补正仅登记用例，构建/执行由主会话统一完成。
