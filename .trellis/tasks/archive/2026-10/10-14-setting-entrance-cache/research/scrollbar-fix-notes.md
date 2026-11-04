# 开屏缓存滚动条跳变修复

## 根因与边界

`ScrollbarStyleScope` 会在 child 缺席后把160ms条宽/alpha tween重置为0；条宽直接决定native ScrollbarSizes、WorkRect与正文可用宽。开屏快路径长期不生成children，书写完成只排布两个真实帧后捕获，实际截到了接近零宽的滚动条状态；末尾恢复children再次触发同一重置。之前WARP fixture关闭了row动画，遗漏该差异。`NoInputs`只门禁拖动目标，不直接隐藏视觉；生产无输入时CancelHomeEntryOnInput返回false，仅取消首帧/导航屏障使用NoInputs，修复未扩大这个屏障。

最小修复：BeginScrollChild增加默认false的settleScrollbarLayout末参；只有主页Entry Pending/Playing的实际完整排布帧启用，当前帧嵌套content child继承。Begin前条宽、padding、grab alpha直接使用native终态，Finish按实际ScrollbarY写回0或1；无溢出不保留退出装饰或永久reserve。恢复两帧采用同一策略，所以恢复后的普通下一帧从已经确定的终态值继续。其他页面、普通进入/退出滚动条动效保持默认160ms；未改动画时序、暖帧数、vendor/设备/限帧。

## 回归夹具

现有Frame补齐生产NoInputs取消flags、PageMaximumWidth及居中；新EntryLayoutProbe仅供现有headless fixture用真实row动画/时钟、冷root、多viewport。CheckEntryScrollbarContinuity覆盖浅/深各3场景：1000×1100/scale1、760×1100/scale1.25窄窗、1000×1100/scale0.6无溢出。

每例：冷准备两次成功 → 12次实际child缺席 → 原两次capture准备 → 缓存归位 → 原两次restore → 首个真正实时帧。比较ScrollbarY/reserve、正文宽、clip、outer/page WorkRect完整坐标值、hero尺寸/位置、brand目标、按钮bounds完全一致；使用production foreground切分移除蒙版/运动字标，读取真实GPU缓存，和首个实时帧除唯一内联brand AA包围矩形外整target精确RGBA对照。该矩形坐标本身严格比较，右侧滚动条及全部其他页面像素均包括在内。

另外验证恢复后的原生SetScrollY和实际Tab取消首帧NoInputs/次帧解除屏障；普通未开启settled入口的animated冷布局控制组应输出尚未达到终态条宽的width-gap，证明回归不会因关闭row动画而通过。现有CPU HomeScrollFollowup仍覆盖普通滚动条出现/消失和快速反向动效。

## 静态检查

4个修改源码均UTF-8无BOM、CRLF，无bare LF或double CR；git diff --check通过。此代理没有执行build/test/GUI/commit/push。动态结果由主会话完整ARM64 Solution及无窗口验证补充；若捕获第二帧多层AutoResize尚未稳定，新回归会精确失败，不能以放宽像素误差/额外暖帧掩盖。

## 首轮失败证据及补充就绪合同

主会话首轮build退出0（23.0087s）。真实animated冷夹具发现条宽/hero等已相同，但溢出场景仍有342/354/326/348个RGBA差异；无溢出两例精确通过。定位诊断明确：scale1浅色捕获outer ContentSizeY=1214而本帧测量1199，page ContentSizeY=1199而本帧测量1348，outer WorkRectMaxY1274；首个live均消费1348，WorkRectMaxY1408，ScrollMaxY202→336，thumb底896→811。342像素包络全部位于右侧(994,842)..(997,927)。原普通条宽控制宽度704→696，gap8px。

因此仅固定条宽不足：native Begin消费上一帧测量，多层AutoResize内容高度传播仍会改变thumb。新增CompleteHomeEntryLayout在outer End前检查当前实际提交content子树的ContentSize/ContentSizeIdeal是否与当前DC测量一致，精确复用native ContentSizeExplicit、collapsed/Hidden保留和ImTrunc64规则；不扫描全局窗口。每个Begin先清layoutReady，只有本帧完整正文提交后可重写，失败/key/资源失效也重置。capture与restore均要求至少两次成功提交加真实layoutReady；沿用6次有界准备预算，restore增加同样的6次非收敛收束，不将2改成任意固定3/4。

回归保持第一live WorkRect/精确RGBA门槛，按实际测量记录capture/restore帧数；CPU状态机新增旧尺寸不能发布、尺寸就绪才发布及非收敛有界终止断言。实际离屏Frame统一调用同一生产尺寸核查，所以人工capture/restore benchmark也不能跳过门禁。此阶段代理仍未build/run；等待主会话验证。
