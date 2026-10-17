# HiMsg 普通点击与 Raw 拖动补充修正：独立检查

检查范围：Input UiPointerInput、Bar、四个 PageControl 的完整工作区实现和生产策略 headless 测试。以 correction.md 为最新合同，旧 Raw 独占入口条款不再适用。代码于本轮检查完成后交主会话统一构建；未启动 GUI、隐藏 HWND 或改动任何 hook。

## 已修复

1. **候选点击被 Raw-only Replaced 取消。** Bar 和 PPT 原先将可选通道的 Replaced 一律提升为业务 Cancel；但 Provider 在缺少旧 Raw Up、又出现下一 Raw Down 时也使用 Replaced。现仅真实拖动的传输失败取消业务，普通候选保持原生 Up。真正 Touch/Pen 接管由独立 SourceReplaced 租约版本取消，与 Raw 可用性无关。补充测试验证原生 Up 不受 Raw-only replacement 影响。
2. **队列中的原生 Down 能跨已结束的触摸接管复活。** Bar 以往到交互线程出队时才快照来源版本。现原生 Down 入队时只存值型 MouseGesture 来源准入快照；实际业务会话与 Raw 认领仍仅由交互线程推进。出队检查快照是否失效，因此 Down 入队后、出队前发生并结束的跨窗触摸也能拒绝旧 Down，触摸期间被拒绝的 Down 不会在租约结束后复活。生产 helper 值拷贝测试覆盖两种边界和后续新 Down 恢复。
3. **迟到终态倒退已接受的拖动位置/时间。** MouseGesture 保留明确 Up 与原生身份，但比最新已接受移动更旧的终态沿用最新屏幕点、messageTime、tick。Bar 命中坐标与 PPT finalMove 均消费归一化结果，不能回读旧消息位置。测试断言两个终态来源均结束一次、位置和时间不倒退。
4. **同毫秒 first-source-wins 丢失 Raw 的真实 excursion。** 确认可选 Raw Down 后，只有私有 Raw Move 驱动连续手势轨迹，原生 Down/Up 继续保留；绑定前及候选补充失败后原生 Move 回退。由此 native-first 的合并 Move 不再压掉已绑定 Raw 同毫秒的 50→0 轨迹，也不重放两套完整重复路径。迟绑定的历史回放不得越过已经消费的原生时间段，详见以下限制。

5. **新测试的时间赋值产生 C4244。** 14 处链式赋值改为先赋 DWORD messageTime、再扩展到 ULONGLONG tick；保持测试值不变并消除本次新增的窄化警告。

## 已审查的保留行为

- 普通 Bar 按钮不准备 Raw；拖动候选、颜色连续选择、粗细滑轨/浮窗/预览、FineDial 和主按钮只在对应入口准备补充。
- 普通 Down/Move/Up 保留 HiMsg 入口；Raw 从共享私有通道直接进入 owner 交互状态机，没有进入 HiMsg。
- MouseGesture 原生 Begin 不读取 Raw 可用性或匹配结果；Raw Up 不提交未过阈值的点击；有效原生 Up 始终可正常结束。两种 Up 顺序与旧代次均有生产 helper 测试。
- PPT 原生 Down 立即进入本窗业务；箭头/长按不启用 Raw。dragPending 只有超过原阈值才 BeginDrag，立即拖动区沿用既有规则。末段位移仍先验证终态身份。
- 来源切换、桌面失效、模式/折叠/窗口生命周期继续显式 Cancel；确认实际拖动传输失效不会伪造成功 Up。候选传输失败仅撤销补充。
- PPT pair try_lock/latest-wins/pending/保存入口未改写；所有 UI 鼠标按住流程不读取 GetAsyncKeyState 或 IsKeyBoardDown。

## 未修复／明确限制

1. **迟绑定前已经合并的轨迹无法重建。** 当 Raw Down 在若干原生 Move 已经消费后才确认，保留原生时间 floor，跳过同毫秒及更早的 Raw 回放，以免重复累加路径/速度。此时间段内原生合并丢失的 excursion 无法凭单点与时间戳可靠重建。绑定之后由 Raw 独占移动，完整保留其有界顺序轨迹。没有加入任意历史 Raw 认领或跨来源轨迹 union 架构。该边界有显式测试，不应声称迟绑定前全部物理轨迹均已恢复。
2. **可选补充仍采用保守 time+point Raw Down 绑定。** 此条件不再参与普通点击成立；不匹配时原生点击可继续，但窗口外 Raw 补充可能不可用。放宽关联会带来无关历史点击串入，需要独立方案，未擅自扩大。
3. **管理员前台、真实 Win7 WM_TOUCH、混合设备与窗口消息实际顺序尚未现场测试。** 无窗口策略测试与编译只能确认代码准入、归一化和有界传输规则，不能证明跨完整性输入始终完整。无 GUI 授权未运行现场测试。

## 验证

- git diff --check --ignore-submodules=all：exit 0。
- 七个相关源码/测试文件 UTF-8 解码与全 CRLF 检查：通过；未改变 BOM。
- 两个 vcxproj 和 filters XML：通过。
- hook 文件路径及安装/卸载所在 Bar.Initialization 未改；Bar/PPT 无 GetAsyncKeyState/IsKeyBoardDown 残留。
- 初次修正完整 Solution 构建：主会话报告 exit 0，26.21 秒；之后仅修正上述测试窄化赋值。
- 最终完整 InkeysRepo.sln Debug|ARM64 增量构建：主会话报告 exit 0、0 warning、0 error。state-mode 生产探针 exit 0。
- Headless --no-window 仍由主会话运行；pen-width 生产探针首次 exit 1，主会话正在采集完整输出定位原因，尚不能归因为代码或环境。本检查者不重复启动构建/探针、不因未知失败修改源码；最终测试结论以主会话验证日志为准。

最终主会话验证：完整 solution 与最终增量编译 exit0；正常环境 headless --no-window exit0，state-mode exit0，正常环境 pen-width exit0。失败/成功日志分别保留，详见 ../validation.md。
