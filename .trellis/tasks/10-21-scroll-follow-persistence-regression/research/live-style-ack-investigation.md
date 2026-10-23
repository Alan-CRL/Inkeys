# Research: 样式发布后精确窗口 ACK 不收敛

- Query: 样式 setter 在普通主栏/快捷键外层是否总会额外刷新窗口 ACK，还是可见夹具发现真实 publication/reconcile 合同缺口。
- Scope: internal
- Date: 2026-10-22

## Findings

### 正常主栏调用链

只读结果：**没有统一的额外窗口刷新**。不能用“完整UI3正常运行就会补齐”解释全部样式路径。

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:1707–1724`：`SetInteractionPenColor/SetInteractionPenWidthIfRevision/SetInteractionPenWidthPresetIfRevision` 分别调用原 setter，成功后仅 RequestInteractionMemorySave。没有 SyncDraw3State、ReconcileDraw3Presentation、RequestScrollFollowPresentationRefresh。
- 色盘拖动约4460调用SetInteractionPenColor，完成取色约4663同路径；普通颜色按钮约7487也走此wrapper。UI后续UpdateRendering/QueueInteractionVisualFeedback只通知Bar/RenderPipeline，不负责IdtState窗口对的ACK。
- 粗细微调Commit约4013调用SetInteractionPenWidthIfRevision；几何粗细preset约6039、普通粗细preset约7616直接调用wrapper。即使随后UpdateRendering，也只进入Bar图更新及Bar请求，不写follow ACK。
- 某些明确工具动作确有 `FollowActionGuard`：Interaction.cpp1368–1388、约7785/7826/7863/7899 等，Complete传真实ScrollFollowActionReceipt到Business::CompleteAction；后者`ScrollFollow.cpp:422`调用RefreshPresentation。这**覆盖这些工具事务，不覆盖上述全部颜色/粗细操作**。
- 全局搜索原shortcut/keyboard文件未找到普通SetPenWidth/SetPenColor调用。已有快捷键工具/画布命令有自己的原事务；没有依据声称快捷键为任意颜色/粗细setter普遍补ACK。

### 当前精确 ACK 的含义与可用性纠正

Core已经定位：样式 setter 的Publish及modeRevision变化不必引发Host runtime/output/content变化，StateMonitoring只在runtimeChanged或retryPending时Reconcile，而Acknowledged要求mode/bridge精确匹配。普通UI外层不能保证另一个runtime事件发生。因此样式发布之后，旧ACK过期且没有确定的重新提交请求，是实际新增精确ACK合同的缺口。

但不能扩大结论为“下一次视图调整Available必然false”：

- `ScrollFollow.cpp:379–388` 的Available仅检查shutdown、capability、ProductRunning、capture.ready、!capture.occupied、!Freeze、Desktop workspace；**不读Acknowledged**。
- 下一次真正RequestToggle到初始化F0后，会通过Project(true)走RequestScrollFollowProjection→RequestScrollFollowPresentationRefresh，置dirty并重发真实桥状态，可能自行修复ACK。
- 已证/可推断的问题是稳定样式后的精确ACK不自动收敛，以及依赖该receipt的观测/事务可能缺唤醒；不是当前已证“按钮必然禁用”。Bar工具事务的CompleteAction又有自己的刷新，不应说全部动作已死锁。

### 最小正确修复归属

建议在 `IdtState.cpp` 中央 `PublishDraw3State` 完成真实PublishProductState并记录publication receipt后，**只设置已有 `draw3PresentationRetryPending=true`**，确保StateMonitoring在原cadence内对最新mode/bridge重新核对/提交原WindowService成对ACK。

这属于状态发布者保证对应呈现重核的责任，不要让每个主栏样式caller或fixture自行额外调刷新；否则直连生产setter仍有遗漏。

- 不要在持stateModeTransitionMutex时同步Reconcile（它会再次读取mode lock并调用窗口owner），避免新增死锁。
- 不建议中央调用 `RequestScrollFollowPresentationRefresh()`：它不仅置retry，还置followProjectionDirty。StateMonitoring消费dirty时会再次PublishDraw3State；若Publish总再置dirty会形成重复发布循环。此处只需retry，不需重新发布相同工具/projection。
- 不改ACK条件去接受旧mode/bridge，不fake ACK、不触渲染参数、不增加新的UI循环。原250ms等待是有界cadence；是否另需即时唤醒可另按已有接口判断，不为此扩大修改。
- 需要并发正确性：设置retry应在真实发布后进行，避免状态线程拿到旧期望先清retry；Reconcile对latest revision已有原guard。实现者/独立checker应核在publication与旧Reconcile并发时不会丢最新retry，如必要在Reconcile开始/结束按已读revision重核；本研究未实施/实测并发修复。

### fixture 与真实产品的区别

当前fixture在A Down前等待PrimaryReady+精确ACK，先调用真实宽/颜色setter，仅靠睡眠不会触发保证重新ACK。这个严格等待暴露了真实合同缺口；直接在fixture补RequestScrollFollowPresentationRefresh会隐藏它。

普通UI可能因画笔预览、输入、工具变化或CompleteAction恰好发runtime/重核，所以肉眼不一定立刻看到失败；这些偶然事件不能充当稳定状态最终收敛的保证。软件定向验证应在没有新笔划/额外工具/外部runtime事件的状态下，只调用真实style setter并验证真实ACK最终恢复，记录mode/bridge确切receipt。

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：颜色/粗细wrapper、色盘/微调/preset实际caller、仅部分工具动作有FollowActionGuard。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:285–311`：UpdateRendering只负责UI/Bar RenderPipeline请求，不是Window ACK入口。
- `Inkeys/Inkeys/Business/ScrollFollow.cpp:379–422`：Available无ACK门，CompleteAction有明确RefreshPresentation。
- `Inkeys/IdtState.cpp:274/830/838/2160–2177`：状态发布、精确ACK、dirty与retry、实际监控重核。

## External References

纯内部只读调查，无build、GUI、截图或外部资料。

## Related Specs

`.trellis/spec/native-desktop/scroll-follow-product.md`：实际publication receipt、mode/projection/bridge的精确ACK及原owner提交路径。

## Caveats / Not Found

- 未复现普通完整UI下按钮禁用，且当前Available实现明确不支持该结论。
- 未实施修复或执行并发测试；中央retry置位方案还需实现/check核publication与旧Reconcile并发不丢更新。
- 此报告只调查正常caller和新feature门语义，不重复Core已完成的setter/Host ApplyState调查。

## 追加：retry 是否反馈，以及并发收敛

再次阅读全文，原实际次序是：

1. StateMonitoring先WaitForRuntimeRevision(revision,250)，off则结束。
2. **仅**followProjectionDirty.exchange(false)返回true时调用PublishDraw3State。
3. ReconcileSettingOwnerDesiredState只做Settings owner命令，不调用Sync/Publish。
4. 读Runtime并比较revision；runtimeChanged或retryPending则调用ReconcileDraw3Presentation。
5. Reconcile521–710没有调用PublishDraw3State或SyncDraw3State：等待桥/targetready、guard/API失败处设retry=true；Host不运行526清false；成功673–677存followAckBridge/followAckMode/followAcknowledged，之后清retry=false。

因此 **Publish后只置retry不会导致本身再次Publish**，不存在retry→Sync→Publish→retry的反馈。只要没有并发新业务变更，真实最新期望成功一次后flag变false，后续无新runtime不再进入Reconcile，不会变永久250ms Window命令轮询。

`Draw3.Bridge.cpp:31–53 StateBridge::PublishState` 没有内容dedupe，每次running时都state.revision=old+1。因此若把中央Publish接到RequestScrollFollowPresentationRefresh（会置dirty），才会导致dirty→Publish→dirty的反馈和不停变bridge revision，必须排除该方案。

**并发 caveat：单置retry不能独自保证不丢唤醒。** 原Reconcile在约654的after/latest检查之后，到677清false之前，另一线程可以Publish新revision并设retry=true，随后旧成功Reconcile把它清掉。原精确ACK会因bridge不同而false，但没有新runtime就可能再次无人重核。所以“中央retry一次”只能解决普通顺序案例，不应把全部并发收敛已保证。

最窄事实驱动方案可在StateMonitoring原if谓词中增加精确ACK是否过期：

```cpp
runtimeChanged || retryPending ||
!ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch())
```

这不Publish、不改任何epoch、只允许已有Reconcile对真实最新状态做实际Window提交。稳定成功后Ack=true且pending=false，退出额外重核；旧成功吞掉retry时Ack仍为false，下一原250ms cadence会再尝试，避免纯bool丢通知。`ScrollFollowPresentationAcknowledged` Host不运行时返回true，不会仅因Host Off强拉窗口；offSignal仍原循环先退出。dirty由同线程先消费，正常投影仍原语义。

这比新增事件/线程或给每个setter加同步窗口调用窄。若采用中央retry并重构Reconcile末尾清除协议，则必须对最新bridge/mode/projection在清除前后做无丢失校验，并遵守原modeLock→presentationLock顺序；不能反向持presentation锁再等mode锁。最终归属由implement/check决定，必须有实际style→ACK恢复和无自发bridge revision增长的验证。
