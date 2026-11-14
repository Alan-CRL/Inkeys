# Research: Touch末位置到Up的跨渠道时序

- Query: 用户复测反馈触摸抬手像被快速顶住；确认约50ms末平台位于哪一层，是否有应用人为等待、较早可用的接触终态或Up后的处理停顿。
- Scope: internal；当前冻结源码与本轮固定生产日志，只做解析/离线统计，不改产品/tests/spec，不build、运行GUI、Git或commit。
- Date: 2026-11-14（Asia/Shanghai）。
- Input: `Build/Logs/pan-touch-feedback-2026-11-14/console.snapshot.log`，65855行/14737778字节，SHA256 `ac5a7c2ab6564f44fa027b06979baf40f8a9fb6b8534c49fc431511d7cfafd15`。

## Findings

### 1. 约50ms主要已在交付给应用的Touch位置/终态序列里，不是Up之后排队50ms

本轮Touch末端平台包含多次真实RTS `Packets`回调，之后才收到`StylusUp`。可同时匹配release-quality和原始input Up的77个Touch释放中，56个在最后空间进展后仍有Move记录，53个运动age落在45–55ms。严格只取Ready且字段齐全的子集为72条，其统计如下；其余4个Ready缺少必要日志，不补造。

| 时间段 | Touch Ready（72）中位/最大 ms | Pen Ready（53）中位/最大 ms |
| --- | --- | --- |
| 最后空间进展→最后raw Move | 41.0565 / 44.3026 | 0 / 0 |
| 最后raw Move→Up样本QPC | 8.2890 / 11.7268 | 3.1058 / 14.7097 |
| 最后空间进展→Up样本QPC | 49.4748 / 50.9901 | 3.1058 / 14.7097 |
| Up样本QPC→导航消费QPC | .08655 / .3277 | .2059 / 1.3191 |

每个样本的前两段可相加为第三段，但表中各自中位数不必可加。这里的Up样本QPC在RTS回调内部、取得state writer后调用QueryQpc得到，不是回调最入口或物理离屏时间；不能把`.08655ms`称为全输入延迟。

全77条Touch（含非Ready）对应中位数为40.8450、8.2827、49.4052、.0872ms；完整逐例与关联方法在[JSON](touch-lift-timing-2026-11-14.json)、[脚本](touch-lift-timing-2026-11-14.py)。

### 2. key26完整拆分：平台在Up前，Up到消费很快

| 事件 | 快照行 | QPC / 其它事实 |
| --- | ---: | --- |
| 最后真实空间进展 | 21634 | 155325545829，位置(1411.729,1468.120)px |
| 最后移动held帧 | 21635 | 有非零delta，frame 1103；父会话/质量复审负责速度折损 |
| 第一份重复raw | 21639 | 155325632474，相同位置 |
| 后续重复raw | 21643/21647/21651/21659 | 不同ordinal/QPC，相同位置；总计5个尾部重复Move |
| 最后raw Move | 21659 | 155325979911 |
| RTS Up arrival | 21663 | Cursor tick15532578，RTS线程39832 |
| input Up被消费 | 21664 | Up样本155326046003、consume155326046628 |
| Touch contact end | 21665 | tick15532578，count=0 |
| RTS Up published诊断 | 21666 | 同tick15532578、published=1 |
| release-quality | 21670 | raw-last/motion-last分别对应上述两个时钟 |
| 首个非零coast | 21676 | 在Up消费后紧接发生，详见父会话实际coast统计 |
| 兼容Mouse Up | 21684 | WM_MOUSE `0x0202`，tick15532578；发生在RTS Up后 |
| pointer leave | 21680 | id1166/type2，发生在RTS Up后 |

该例精确分解：

- 最后空间进展→最后raw = **43.4082ms**；中间确有真实`rts-batch reported=1 retained=1 published=1`，对应每个重复Move。
- 最后raw→Up样本 = **6.6092ms**。
- 最后空间进展→Up样本 = **50.0174ms**。
- Up样本→导航消费 = **.0625ms**。

因此约50ms不能归因于Controller收到Up后才迟迟消费，也没有观察到一个已到达Up被应用保留50ms。日志的跨线程输出顺序不可当严格调用先后：PublishUp调用会先发布/唤醒，`published`文本在返回后才输出，渲染线程input日志可以先出现在文件中。

本例平台期间render的delta为0、无新Present；其后的新coast确实提交。约46ms的成功submit间隔落在Up前的平台阶段，而非Up后额外等一帧或等50ms。本研究不把软件Present返回当物理屏幕测量。

### 3. 没有观察到更早、可确认为同一接触Up的其它通道

- 全快照的`pointer message`记录只见Touch type2 `0x0249` ENTER（184条）及Pen type3同类3条；未见`WM_POINTERUP/DOWN/UPDATE`可提供更早的Touch终态。部分日志有LEAVE，但LEAVE本来不是同义Up；key26的LEAVE还在RTS Up之后。
- `touch-end`不是独立物理终态来源。它由RTS StylusUp在PublishUp后调用，不能拿它作为“比RTS早知道抬手”的证明。
- key26提升的Mouse Up在RTS Up后同一粗tick出现，没有更早终态证据。位置提升的MouseMove在同位平台期间不再前进，也不能据此测量物理离屏。
- 这些观察不证明Windows永远不提供较早Pointer Up，更不授权用第二producer或按近似时间桥接Pointer/RTS身份；当前快照没有这项证据。

### 4. 源码未发现生产Touch专用50ms延后/补旧位置；有限锁等待与未知上游仍需区分

- `Draw3.RealtimeStylus.cpp:1451–1506`：Packets每回调读取本次lastPacket，Decode成功即PublishMove；QueryQpc是回调到达/处理时间，不是packet生成时间。key26相关回调均reported=1，因此这几个回调内部不存在丢弃其它packet的解释。
- 同文件`:994–1048`：Decode直接读本packet X/Y，写本次snapshot.qpc/phase；缺decoder、属性/索引无效或坐标不finite会失败不发布，没有“沿用旧坐标再更新QPC”的回退。
- 同文件`:1338–1408`：StylusUp先记录arrival，取得writer后Decode(Up,QueryQpc)→直接coordinator.PublishUp→NotifyTouchContactEnd→published诊断；没有等待50ms或由定时器延后触摸终态。state writer锁可能有实际等待，不能仅凭静态代码保证零等待。
- `Draw3.ContactInput.cpp:886–891,595–650`：PublishUp直接Close，关闭producer route、等待在途writer、发布终态并追加原始导航记录；只有非有限终态才借最后有效位置闭合，保持终态QPC。没有为正常Up插入末尾重复Move或50ms驻留。
- 同文件`:609–618,716`的Close测试暂停hook默认nullptr，只用于测试，不是生产延时器。
- `Draw3.InkPrediction.cppm:82–86`：断触注入Simulation constexpr=false；RTS中的随机Up/丢Move/恢复Down分支不在当前生产路径执行。不能把保留的模拟器类当本次故障原因。
- `Draw3.WindowControl.cpp:70–77`已有禁用press-and-hold/flicks的固定窗口标志；这只能说明应用设置，不证明所有上游栈都没有延迟。

所有可配对RTS Up arrival/published的Cursor粗tick差中，Touch device0共176对（全部可见RTS Up，非仅末指移动释放子集），中位和p90均0、最大16ms；Pen device1共52对中位/p900、最大15ms。tick来自GetTickCount64，粒度比QPC粗，**0不等于实际0ms**，且arrival文本在锁之前、snapshot QPC在锁之后。统计不支持“回调进入后应用固定拖住50ms”，但不构成锁等待的高精度上界测量。

### 5. 可以得出的结论与不能命名的根因

确定：Touch末空间进展到Up的约50ms分成“约41ms重复位置回调+约8ms到下一终态”；同一部分Pen轨迹没有这种尾部重复，Up样本到消费很快。当前算法把这个间隔同时用于呈现静止和释放年龄衰减，能与用户报告的交接停顿共同解释，速度折损细节由质量报告负责。

不能确定：物理手指到底在何时离屏、平台是用户实际停步还是OS/驱动/设备交付语义、原始硬件采样是否冻结。不能把49ms直接叫成已证实的“RTS固定抬手延迟”、驱动故障或应用定时器；当前没有较早且可关联的其它Up信号。

若后续需要继续分层取证，最小字段是回调入口QPC、取得writer/Decode QPC、原packet X/Y与可用的packet时间/序号能力，保留现有arrival；这样能量化锁/发布与上游事实。没有真实生成时钟或手动标注，仍不能补造物理动作时间。本轮只列取证缺口，不改代码或开启GUI。

## Files Found

- `Build/Logs/pan-touch-feedback-2026-11-14/console.snapshot.log`：本轮完整冻结生产日志。
- `Draw3.RealtimeStylus.cpp`：Packets、StylusUp、Decode与Touch begin/end通知。
- `Draw3.ContactInput.cpp`：原始snapshot发布、终态Close、导航环与默认关闭测试hook。
- `Draw3.Diagnostics.cpp:719–742`：Cursor日志在生产者读取GetTickCount64，异步sink不重写前缀时间。
- `Draw3.WindowControl.cpp`：Pointer观察、Touch计数、提升Mouse来源过滤与窗口手势标志。
- 本主题`.py/.json`：只读分解统计，可从同一快照复算。

## External References

无。本报告只使用当前本地源码/日志，不以平台常识推断物理离屏或系统固定延迟；没有新增API或后端建议。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：RTS是导航producer，Pointer/Mouse光标反馈不能当contact准入证据。
- `.trellis/spec/native-desktop/canvas-pan.md`：原始导航历史、水位、QPC/phase次序、呈现与Up消费。
- `.trellis/spec/native-desktop/canvas-pan-release-quality.md`：raw/position-progress/Up/frame时钟分工及30–70ms年龄。

## Caveats / Not Found

- 77个Touch具备quality+inputUp，Ready子集72，不冒充全81/76；Cursor sink存在contention丢行，缺日志不等于输入丢失。
- Cursor tick与QPC不是同一绝对基准，未直接相减；粗tick差只描述callback日志内部跨度，0不代表零时间。
- 坐标来自日志三位小数，重复在日志精度下；key26支持值吻合零投影分支，但没有原始packet整数来证明物理测量恒定。
- 没有检查/修改用户设备、驱动或GUI，没有构建/运行产品，没有Git操作；仅写本任务research。
