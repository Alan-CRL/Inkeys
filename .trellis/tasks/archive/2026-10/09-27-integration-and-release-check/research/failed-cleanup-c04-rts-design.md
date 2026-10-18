# C04：真实RTS停止与失败退场的独立范围（待审）

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01 root只读实际RealTimeStylusDisabled/Shutdown及原C3契约。当前无新增selector/代码/运行；C3-B保存失败和B3先行，不用纯RtsStateGate/hidden mailbox替provider静止证明。

## 需求/验收

分开三种证据：真实成功Stop/回调交接、Disable HRESULT拒绝分支、Remove HRESULT拒绝分支。后两者允许在真实API已返回后显式一次注入失败，但必须记录originalHRESULT与injectedHRESULT，标签仅branch coverage，不能冒自然provider失败或仍有late callback事实。ordinaryClose仍原15秒；已知startup失败仍原失败scope合同。对象借用/临时removedPlugin强引用保留到真join或noreturn死亡。

## 当前代码与限制

RealTimeStylusDisabled在真实RTS通知中先取得RtsStateWriterGuard，再ResetDecoderLifecycleState(true)/PublishDefaultPenCursor；Shutdown依次put_Enabled(FALSE)、RemoveStylusSyncPlugin、CloseAll、释放plugin/stylus。FAILED且有生产failedCleanup publisher时已FailUnprovenProducerStop，不允许释放后Reset。成功API/现state gate是静止基础，没有全callback inFlight证据，仍不能称真笔Packet竞争全部验证。

## 最小下一实施提案

- 仅真实显式private C04 capability，Host不能skipStylusInitialization；保留RTS实际Enable/Disable/Remove与Win7API/COM apartment，未可用provider为prerequisite/NOT VERIFIED。
- 当前同步Disabled真实方法体中的一次owner控制gate，先报告真实进入及threadId，hold/release由独立strong上下文控制；固定有限回调类型，不从任意外部消息调用插件或假source。
- release在Disabled被真实put_Enabled(FALSE)或Remove通知时短暂停顿后放行，验证两原HRESULT、oldHost Stop/Window真join；newHost只在旧全链静止后Start，旧generation不影响新hidden contact。该数据只证Disabled与Host交接，真实硬件Packets/压力保持人工。
- hold在正式Close已Arm后不放行，真实Stop堵在RTS通知链，保留Host/plugin/coordinator/window/mapping；原15秒自然退场，父强杀不PASS。如不能取得真实通知，不造成功数字。
- 两合成FAILED分别在真实Disable或Remove结果边界触发已有noreturn；默认hook为空、无新增clock或capture数组。原产物/接触语义不变，不全面重写插件或关闭RTS。
- 具体普通header POD、case enum、Host传递及借用寿命必须由root/RTS唯一writer先冻结、独立审查；现3HANDLE/1024B协议不可随意扩大。Header字段不足先给固定扩展设计，不能塞pointer或强用任意enum。

## 顺序/门禁

当前仅设计待独立review，不是运行许可。后续先核所有plugin真正访问coordinator/cursor的callback持门与失败路径，再明确writer、最小实现/独立actualdiff安全审、所有source冻结后全Debug/相关旧C3与RTS helper回归。release前置、hold三轮、两FAILED各真实原API+注入结果；最终Release三架构/Win7组合单列。任何不能动态证明的provider静止、真笔、COM故障能力继续未验证，不扩实现只为造PASS。
