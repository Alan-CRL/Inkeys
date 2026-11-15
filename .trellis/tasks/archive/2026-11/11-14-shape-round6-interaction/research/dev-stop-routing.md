# dev 合并后的生产者停止握手修复

远程基线 origin/dev c754e20020b9e0ece44580f3851ddf9467575302。通过 git show 核对，该版本 Product.cpp 已遗漏新触控板 Stop 的产品闸门放行，不是本轮多指控件产生的生产者退出错误。

## 证据与根因
- 最新形状隐藏窗口回归全部功能断言完成后，StopProduct 退出码 0xE143001D。
- FailedCleanupDeadline.cpp 的 kProducerStopUnprovenExitCode 正是此码，表示没有收到生产者停止证明。
- StopProduct 先置 productStopping=true；Host::StopInputProducer 调 StopTouchpadNavigation，后者以 WM_APP+0x3D5、wParam低位1向窗口 owner 请求停止，并等待已存在的5秒确认。
- DrawpadMsgCallback 和 ForwardProductMessage 两处闸门只放行 MouseInputOwner Stop；触控板 Stop 被丢给 DefWindowProc，事件无人确认。
- 当前 DM 隔离不等于从未发布 owner update；停止仍必须确认排队的 attach 代消息已退休，不能跳过。

## 最小修复
- 在已有 Draw3.TouchpadNavigation.h 统一消息常量及 IsTouchpadNavigationStopMessage。
- WindowControl 复用该常量，owner 的 this/attach identity 校验、退栈后确认与失败保护保持。
- Product 两处停止闸门仅增加精确的触控板 Stop 分类，普通 update 不放行。
- HiddenWindowTest 增加分类断言（Stop允许、Update/其它消息拒绝）、停前真实接触计数和 Stop 返回后的证明。
- 不改生产超时、不恢复 DM、不吞失败、不改变 RTS 生命周期。

此前 SF44b 另有测试竞态：editorOpen 只查 object，构造预览也满足，导致 Complete 未被消费就开始下一接触。现排除 construction.active/drawingPreview，并保留原始零步骤 Undo 和 Cancel 验收；失败时闭合注入接触后清理。

运行验证由主代理在最终构建后记录；本记录不提前声称通过。
